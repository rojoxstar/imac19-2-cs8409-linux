// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HD audio codec driver for Cirrus Logic CS8409 HDA bridge chip
 *
 * Copyright (C) 2021 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#include <linux/acpi.h>
#include <linux/cleanup.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <sound/core.h>
#include <linux/mutex.h>
#include <linux/iopoll.h>

#include "cs8409.h"
#include "../side-codecs/hda_component.h"

/******************************************************************************
 *                        CS8409 Specific Functions
 ******************************************************************************/

static int cs8409_parse_auto_config(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;
	int err;
	int i;

	err = snd_hda_parse_pin_defcfg(codec, &spec->gen.autocfg, NULL, 0);
	if (err < 0)
		return err;

	err = snd_hda_gen_parse_auto_config(codec, &spec->gen.autocfg);
	if (err < 0)
		return err;

	/* keep the ADCs powered up when it's dynamically switchable */
	if (spec->gen.dyn_adc_switch) {
		unsigned int done = 0;

		for (i = 0; i < spec->gen.input_mux.num_items; i++) {
			int idx = spec->gen.dyn_adc_idx[i];

			if (done & (1 << idx))
				continue;
			snd_hda_gen_fix_pin_power(codec, spec->gen.adc_nids[idx]);
			done |= 1 << idx;
		}
	}

	return 0;
}

static void cs8409_disable_i2c_clock_worker(struct work_struct *work);

static struct cs8409_spec *cs8409_alloc_spec(struct hda_codec *codec)
{
	struct cs8409_spec *spec;

	spec = kzalloc_obj(*spec);
	if (!spec)
		return NULL;
	codec->spec = spec;
	spec->codec = codec;
	codec->power_save_node = 1;
	mutex_init(&spec->i2c_mux);
	INIT_DELAYED_WORK(&spec->i2c_clk_work, cs8409_disable_i2c_clock_worker);
	snd_hda_gen_spec_init(&spec->gen);

	return spec;
}

static bool cs8409_is_imac_amp(const struct hda_codec *codec)
{
	return codec->fixup_id == CS8409_FIXUP_IMAC_AMP ||
	       codec->fixup_id == CS8409_FIXUP_IMAC19_2;
}

static void imac_i2c_begin(struct hda_codec *codec, bool continue_on_error)
{
	struct cs8409_spec *spec = codec->spec;

	spec->imac_i2c_error = 0;
	spec->imac_i2c_continue_on_error = continue_on_error;
}

static int imac_i2c_result(struct hda_codec *codec)
{
	return ((struct cs8409_spec *)codec->spec)->imac_i2c_error;
}

static int imac_i2c_latched_error(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	if (!cs8409_is_imac_amp(codec) || spec->imac_i2c_continue_on_error)
		return 0;

	return spec->imac_i2c_error;
}

static void imac_i2c_record_error(struct hda_codec *codec, int error)
{
	struct cs8409_spec *spec = codec->spec;

	if (cs8409_is_imac_amp(codec) && !spec->imac_i2c_error)
		spec->imac_i2c_error = error;
}

static inline int cs8409_vendor_coef_get(struct hda_codec *codec, unsigned int idx)
{
	snd_hda_codec_write(codec, CS8409_PIN_VENDOR_WIDGET, 0, AC_VERB_SET_COEF_INDEX, idx);
	return snd_hda_codec_read(codec, CS8409_PIN_VENDOR_WIDGET, 0, AC_VERB_GET_PROC_COEF, 0);
}

static inline void cs8409_vendor_coef_set(struct hda_codec *codec, unsigned int idx,
					  unsigned int coef)
{
	snd_hda_codec_write(codec, CS8409_PIN_VENDOR_WIDGET, 0, AC_VERB_SET_COEF_INDEX, idx);
	snd_hda_codec_write(codec, CS8409_PIN_VENDOR_WIDGET, 0, AC_VERB_SET_PROC_COEF, coef);
}

static void imac_enable_i2c(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	guard(mutex)(&spec->i2c_mux);

	snd_hda_codec_write(codec, CS8409_PIN_VENDOR_WIDGET, 0,
			    AC_VERB_SET_PROC_STATE, 0x01);

	cs8409_vendor_coef_set(codec, 0x0030, 0x9008);
	cs8409_vendor_coef_set(codec, 0x0002, 0x0080);
	cs8409_vendor_coef_set(codec, 0x005b, 0x0010);
	cs8409_vendor_coef_set(codec, 0x0030, 0x9000);

	spec->i2c_clck_enabled = 1;
}

static void imac_gpio_reset(struct hda_codec *codec)
{
	codec_info(codec, "iMac gpio reset start\n");

	/* Ubuntu enable_GPIforUR(codec, 0xd) equivalent */
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_MASK, 0x0d);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_DIRECTION, 0x00);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_WAKE_MASK, 0x01);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK, 0x01);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    0x7f0, 0x003000b7);

	/* Ubuntu cs42l83_external_control_GPIO(codec, 0xf) clear */
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_DIRECTION, 0x02);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_DATA, 0x00);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_MASK, 0x0f);

	usleep_range(2000, 4000);

	/* Ubuntu cs42l83_external_control_GPIO(codec, 0xf) set */
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_DIRECTION, 0x02);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_DATA, 0x02);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_MASK, 0x0f);

	usleep_range(2000, 4000);

	/* Ubuntu setup_gpio_set_10 equivalent */
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_DIRECTION, 0x12);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_DATA, 0x12);
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_MASK, 0x1f);

	codec_dbg(codec, "iMac GPIO DATA after reset = 0x%x\n",
		   snd_hda_codec_read(codec, codec->core.afg, 0,
				      AC_VERB_GET_GPIO_DATA, 0));

	codec_dbg(codec, "iMac GPIO MASK after reset = 0x%x\n",
		   snd_hda_codec_read(codec, codec->core.afg, 0,
				      AC_VERB_GET_GPIO_MASK, 0));

	codec_dbg(codec, "iMac GPIO DIRECTION after reset = 0x%x\n",
		   snd_hda_codec_read(codec, codec->core.afg, 0,
				      AC_VERB_GET_GPIO_DIRECTION, 0));

	codec_info(codec, "iMac gpio reset end\n");
}

/*
 * cs8409_enable_i2c_clock - Disable I2C clocks
 * @codec: the codec instance
 * Disable I2C clocks.
 * This must be called when the i2c mutex is unlocked.
 */
static void cs8409_disable_i2c_clock(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	guard(mutex)(&spec->i2c_mux);
	if (spec->i2c_clck_enabled) {
		cs8409_vendor_coef_set(spec->codec, 0x0,
			       cs8409_vendor_coef_get(spec->codec, 0x0) & 0xfffffff7);
		spec->i2c_clck_enabled = 0;
	}
}

/*
 * cs8409_disable_i2c_clock_worker - Worker that disable the I2C Clock after 25ms without use
 */
static void cs8409_disable_i2c_clock_worker(struct work_struct *work)
{
	struct cs8409_spec *spec = container_of(work, struct cs8409_spec, i2c_clk_work.work);

	cs8409_disable_i2c_clock(spec->codec);
}

/*
 * cs8409_enable_i2c_clock - Enable I2C clocks
 * @codec: the codec instance
 * Enable I2C clocks.
 * This must be called when the i2c mutex is locked.
 */
static void cs8409_enable_i2c_clock(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	/* Cancel the disable timer, but do not wait for any running disable functions to finish.
	 * If the disable timer runs out before cancel, the delayed work thread will be blocked,
	 * waiting for the mutex to become unlocked. This mutex will be locked for the duration of
	 * any i2c transaction, so the disable function will run to completion immediately
	 * afterwards in the scenario. The next enable call will re-enable the clock, regardless.
	 */
	cancel_delayed_work(&spec->i2c_clk_work);

	if (!spec->i2c_clck_enabled) {
		cs8409_vendor_coef_set(codec, 0x0, cs8409_vendor_coef_get(codec, 0x0) | 0x8);
		spec->i2c_clck_enabled = 1;
	}
	queue_delayed_work(system_power_efficient_wq, &spec->i2c_clk_work, msecs_to_jiffies(25));
}

/**
 * cs8409_i2c_wait_complete - Wait for I2C transaction
 * @codec: the codec instance
 *
 * Wait for I2C transaction to complete.
 * Return -ETIMEDOUT if transaction wait times out.
 */
static int cs8409_i2c_wait_complete(struct hda_codec *codec)
{
	unsigned int retval;

	return read_poll_timeout(cs8409_vendor_coef_get, retval, retval & 0x18,
		CS42L42_I2C_SLEEP_US, CS42L42_I2C_TIMEOUT_US, false, codec, CS8409_I2C_STS);
}

/**
 * cs8409_set_i2c_dev_addr - Set i2c address for transaction
 * @codec: the codec instance
 * @addr: I2C Address
 */
static void cs8409_set_i2c_dev_addr(struct hda_codec *codec, unsigned int addr)
{
	struct cs8409_spec *spec = codec->spec;

	if (spec->dev_addr != addr) {
		cs8409_vendor_coef_set(codec, CS8409_I2C_ADDR, addr);
		spec->dev_addr = addr;
	}
}

/**
 * cs8409_i2c_set_page - CS8409 I2C set page register.
 * @scodec: the codec instance
 * @i2c_reg: Page register
 *
 * Returns negative on error.
 */
static int cs8409_i2c_set_page(struct sub_codec *scodec, unsigned int i2c_reg)
{
	struct hda_codec *codec = scodec->codec;

	if (scodec->paged && (scodec->last_page != (i2c_reg >> 8))) {
		cs8409_vendor_coef_set(codec, CS8409_I2C_QWRITE, i2c_reg >> 8);
		if (cs8409_i2c_wait_complete(codec) < 0)
			return -EIO;
		scodec->last_page = i2c_reg >> 8;
	}

	return 0;
}

/**
 * cs8409_i2c_read - CS8409 I2C Read.
 * @scodec: the codec instance
 * @addr: Register to read
 *
 * Returns negative on error, otherwise returns read value in bits 0-7.
 */
static int cs8409_i2c_read(struct sub_codec *scodec, unsigned int addr)
{
	struct hda_codec *codec = scodec->codec;
	struct cs8409_spec *spec = codec->spec;
	unsigned int i2c_reg_data;
	unsigned int read_data;
	int ret;

	ret = imac_i2c_latched_error(codec);
	if (ret)
		return ret;

	if (scodec->suspended)
		return -EPERM;

	guard(mutex)(&spec->i2c_mux);
	cs8409_enable_i2c_clock(codec);
	cs8409_set_i2c_dev_addr(codec, scodec->addr);

	if (cs8409_i2c_set_page(scodec, addr))
		goto error;

	i2c_reg_data = (addr << 8) & 0x0ffff;
	cs8409_vendor_coef_set(codec, CS8409_I2C_QREAD, i2c_reg_data);
	if (cs8409_i2c_wait_complete(codec) < 0)
		goto error;

	/* Register in bits 15-8 and the data in 7-0 */
	read_data = cs8409_vendor_coef_get(codec, CS8409_I2C_QREAD);

	return read_data & 0x0ff;

error:
	imac_i2c_record_error(codec, -EIO);
	if (spec->imac_i2c_continue_on_error)
		codec_dbg(codec, "%s() readiness probe failed 0x%02x : 0x%04x\n",
			  __func__, scodec->addr, addr);
	else
		codec_err(codec, "%s() Failed 0x%02x : 0x%04x\n",
			  __func__, scodec->addr, addr);
	return -EIO;
}

/**
 * cs8409_i2c_bulk_read - CS8409 I2C Read Sequence.
 * @scodec: the codec instance
 * @seq: Register Sequence to read
 * @count: Number of registeres to read
 *
 * Returns negative on error, values are read into value element of cs8409_i2c_param sequence.
 */
static int cs8409_i2c_bulk_read(struct sub_codec *scodec, struct cs8409_i2c_param *seq, int count)
{
	struct hda_codec *codec = scodec->codec;
	struct cs8409_spec *spec = codec->spec;
	unsigned int i2c_reg_data;
	int i;

	if (scodec->suspended)
		return -EPERM;

	guard(mutex)(&spec->i2c_mux);
	cs8409_set_i2c_dev_addr(codec, scodec->addr);

	for (i = 0; i < count; i++) {
		cs8409_enable_i2c_clock(codec);
		if (cs8409_i2c_set_page(scodec, seq[i].addr))
			goto error;

		i2c_reg_data = (seq[i].addr << 8) & 0x0ffff;
		cs8409_vendor_coef_set(codec, CS8409_I2C_QREAD, i2c_reg_data);

		if (cs8409_i2c_wait_complete(codec) < 0)
			goto error;

		seq[i].value = cs8409_vendor_coef_get(codec, CS8409_I2C_QREAD) & 0xff;
	}

	return 0;

error:
	codec_err(codec, "I2C Bulk Write Failed 0x%02x\n", scodec->addr);
	return -EIO;
}

/**
 * cs8409_i2c_write - CS8409 I2C Write.
 * @scodec: the codec instance
 * @addr: Register to write to
 * @value: Data to write
 *
 * Returns negative on error, otherwise returns 0.
 */
static int cs8409_i2c_write(struct sub_codec *scodec, unsigned int addr, unsigned int value)
{
	struct hda_codec *codec = scodec->codec;
	struct cs8409_spec *spec = codec->spec;
	unsigned int i2c_reg_data;
	int ret;

	ret = imac_i2c_latched_error(codec);
	if (ret)
		return ret;

	if (scodec->suspended)
		return -EPERM;

	guard(mutex)(&spec->i2c_mux);

	cs8409_enable_i2c_clock(codec);
	cs8409_set_i2c_dev_addr(codec, scodec->addr);

	if (cs8409_i2c_set_page(scodec, addr))
		goto error;

	i2c_reg_data = ((addr << 8) & 0x0ff00) | (value & 0x0ff);
	cs8409_vendor_coef_set(codec, CS8409_I2C_QWRITE, i2c_reg_data);

	if (cs8409_i2c_wait_complete(codec) < 0)
		goto error;

	return 0;

error:
	imac_i2c_record_error(codec, -EIO);
	if (spec->imac_i2c_continue_on_error)
		codec_dbg(codec, "%s() readiness probe failed 0x%02x : 0x%04x\n",
			  __func__, scodec->addr, addr);
	else
		codec_err(codec, "%s() Failed 0x%02x : 0x%04x\n",
			  __func__, scodec->addr, addr);
	return -EIO;
}

static void imac_cs42l83_reset(struct hda_codec *codec)
{
	struct sub_codec cs42l83;
	int ret;

	codec_info(codec, "iMac cs42l83 reset start\n");

	memset(&cs42l83, 0, sizeof(cs42l83));

	cs42l83.codec = codec;
	cs42l83.addr = 0x90;
	cs42l83.paged = 1;
	cs42l83.last_page = 0;
	cs42l83.suspended = 0;

	ret = cs8409_i2c_read(&cs42l83, 0x1301);
	codec_dbg(codec, "iMac cs42l83 read 0x1301 = 0x%x\n", ret);

	ret = cs8409_i2c_read(&cs42l83, 0x1303);
	codec_dbg(codec, "iMac cs42l83 read 0x1303 = 0x%x\n", ret);

	ret = cs8409_i2c_read(&cs42l83, 0x130b);
	codec_dbg(codec, "iMac cs42l83 read 0x130b = 0x%x\n", ret);

	ret = cs8409_i2c_read(&cs42l83, 0x130d);
	codec_dbg(codec, "iMac cs42l83 read 0x130d = 0x%x\n", ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2001, 0x01);
	codec_dbg(codec, "iMac cs42l83 write 0x2001 = 0x%x\n", ret);

	codec_info(codec, "iMac cs42l83 reset end\n");
}

static void imac_cs42l83_patch(struct hda_codec *codec)
{
	struct sub_codec cs42l83;
	int ret;

	codec_info(codec, "iMac cs42l83 patch start\n");

	memset(&cs42l83, 0, sizeof(cs42l83));

	cs42l83.codec = codec;
	cs42l83.addr = 0x90;
	cs42l83.paged = 1;
	cs42l83.last_page = 0;
	cs42l83.suspended = 0;

	ret = cs8409_i2c_write(&cs42l83, 0x4a21, 0x0021);
	codec_dbg(codec, "iMac cs42l83 write 0x4a21 = 0x%x\n", ret);

	ret = cs8409_i2c_read(&cs42l83, 0x4a21);
	codec_dbg(codec, "iMac cs42l83 READ BACK 0x4a21 = 0x%x\n", ret);

	ret = cs8409_i2c_write(&cs42l83, 0x5001, 0x0001);
	codec_dbg(codec, "iMac cs42l83 write 0x5001 = 0x%x\n", ret);

	ret = cs8409_i2c_read(&cs42l83, 0x5001);
	codec_dbg(codec, "iMac cs42l83 READ BACK 0x5001 = 0x%x\n", ret);

	codec_info(codec, "iMac cs42l83 patch end\n");
}

#ifndef CS8409_VENDOR_NID
#ifdef CS8409_PIN_VENDOR_WIDGET
#define CS8409_VENDOR_NID CS8409_PIN_VENDOR_WIDGET
#else
#define CS8409_VENDOR_NID 0x47
#endif
#endif

static unsigned int imac_cs8409_coef_read(struct hda_codec *codec,
					  unsigned int idx)
{
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_SET_COEF_INDEX, idx);
	return snd_hda_codec_read(codec, CS8409_VENDOR_NID, 0,
				  AC_VERB_GET_PROC_COEF, 0);
}

static void imac_cs8409_coef_write(struct hda_codec *codec,
				   unsigned int idx, unsigned int val)
{
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_SET_COEF_INDEX, idx);
	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_SET_PROC_COEF, val);
}




static int imac_tdm_extra_sync_setup(struct hda_codec *codec)
{
	struct sub_codec dev;
	int ret;

	codec_info(codec, "iMac extra TDM sync setup start\n");

	memset(&dev, 0, sizeof(dev));
	dev.codec = codec;
	dev.addr = 0x64;
	dev.paged = 0;
	dev.last_page = 0;
	dev.suspended = 0;

	ret = cs8409_i2c_write(&dev, 0x0014, 0x00e4);
	if (ret < 0)
		return ret;
	codec_dbg(codec, "TDM DEV 0x64 WRITE 0x0014 PCMModeConfig = 0x%x\n", ret);

	ret = cs8409_i2c_read(&dev, 0x0014);
	if (ret < 0)
		return ret;
	codec_dbg(codec, "TDM DEV 0x64 READ BACK 0x0014 = 0x%x\n", ret);

	memset(&dev, 0, sizeof(dev));
	dev.codec = codec;
	dev.addr = 0x28;
	dev.paged = 0;
	dev.last_page = 0;
	dev.suspended = 0;

	ret = cs8409_i2c_write(&dev, 0x0005, 0x0000);
	if (ret < 0)
		return ret;
	codec_dbg(codec, "TDM DEV 0x28 WRITE 0x0005 value=0x0000 ret=0x%x\n", ret);

	ret = cs8409_i2c_read(&dev, 0x0005);
	if (ret < 0)
		return ret;
	codec_dbg(codec, "TDM DEV 0x28 READ BACK 0x0005 = 0x%x\n", ret);

	ret = cs8409_i2c_write(&dev, 0x0004, 0x0051);
	if (ret < 0)
		return ret;
	codec_dbg(codec, "TDM DEV 0x28 WRITE 0x0004 value=0x0051 ret=0x%x\n", ret);

	ret = cs8409_i2c_read(&dev, 0x0004);
	if (ret < 0)
		return ret;
	codec_dbg(codec, "TDM DEV 0x28 READ BACK 0x0004 = 0x%x\n", ret);

	codec_info(codec, "iMac extra TDM sync setup end\n");
	return 0;
}

struct imac_tas5764_amp_config {
	unsigned int address;
	unsigned int channel_select;
	unsigned int digital_volume;
};

struct imac_tas5764_model_config {
	const char *name;
	struct imac_tas5764_amp_config amps[4];
	unsigned int analog_control;
};

/*
 * The address/channel-select pairs come from Apple-observed TAS5764L I2C
 * traces.  TAS5764L is not publicly documented; do not reinterpret register
 * 0x03 using the similar, but not identical, TAS5760L register map.
 *
 * The iMac18,3 entry retains the imported trace's 0 dB digital volume.  The
 * iMac19,2 entry is deliberately model-specific: -18 dB (0xab) on all four
 * paths provides symmetric headroom in the absence of Apple's proprietary
 * crossover, EQ, compressor and limiter.  V2 proved that 0xab is accepted by
 * this machine and materially reduces its early saturation.
 */
static const struct imac_tas5764_model_config imac18_3_tas5764 = {
	.name = "iMac18,3",
	.amps = {
		{ 0xd8, 0x00, 0xcf },
		{ 0xda, 0x02, 0xcf },
		{ 0xdc, 0x01, 0xcf },
		{ 0xde, 0x03, 0xcf },
	},
	.analog_control = 0x55,
};

static const struct imac_tas5764_model_config imac19_2_tas5764 = {
	.name = "iMac19,2",
	.amps = {
		{ 0xd8, 0x02, 0xab },
		{ 0xda, 0x00, 0xab },
		{ 0xdc, 0x03, 0xab },
		{ 0xde, 0x01, 0xab },
	},
	.analog_control = 0x55,
};

static const struct imac_tas5764_model_config *
imac_tas5764_model(struct hda_codec *codec)
{
	if (codec->fixup_id == CS8409_FIXUP_IMAC19_2)
		return &imac19_2_tas5764;

	return &imac18_3_tas5764;
}

static int imac_tas576_boot_reset_setup(struct hda_codec *codec)
{
	const struct imac_tas5764_model_config *model = imac_tas5764_model(codec);
	struct sub_codec amp;
	int i, ret;

	codec_info(codec, "%s TAS5764L boot reset setup start\n", model->name);

	for (i = 0; i < 4; i++) {
		memset(&amp, 0, sizeof(amp));

		amp.codec = codec;
		amp.addr = model->amps[i].address;
		amp.paged = 0;
		amp.last_page = 0;
		amp.suspended = 0;

		ret = cs8409_i2c_write(&amp, 0x0001, 0x00fc);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x BOOT WRITE 0x0001 value=0x00fc ret=0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_write(&amp, 0x0002, 0x0004);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x BOOT WRITE 0x0002 value=0x0004 ret=0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_write(&amp, 0x0003, 0x0080);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x BOOT WRITE 0x0003 value=0x0080 ret=0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_write(&amp, 0x0004, 0x00cf);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x BOOT WRITE 0x0004 value=0x00cf ret=0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_write(&amp, 0x0006, 0x0051);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x BOOT WRITE 0x0006 value=0x0051 ret=0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_write(&amp, 0x0008, 0x0000);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x BOOT WRITE 0x0008 value=0x0000 ret=0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_write(&amp, 0x0013, 0x0000);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x BOOT WRITE 0x0013 value=0x0000 ret=0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_write(&amp, 0x0014, 0x0002);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x BOOT WRITE 0x0014 value=0x0002 ret=0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_read(&amp, 0x0008);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x BOOT READ BACK 0x0008 = 0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_read(&amp, 0x0014);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x BOOT READ BACK 0x0014 = 0x%x\n", amp.addr, ret);
	}

	codec_info(codec, "%s TAS5764L boot reset setup end\n", model->name);
	return 0;
}

static int imac_tas576_tdm_slot_setup(struct hda_codec *codec)
{
	const struct imac_tas5764_model_config *model = imac_tas5764_model(codec);
	struct sub_codec amp;
	int i, ret;

	codec_info(codec, "%s TAS5764L channel setup start\n", model->name);

	for (i = 0; i < 4; i++) {
		memset(&amp, 0, sizeof(amp));

		amp.codec = codec;
		amp.addr = model->amps[i].address;
		amp.paged = 0;
		amp.last_page = 0;
		amp.suspended = 0;

		ret = cs8409_i2c_write(&amp, 0x0002, 0x0044);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x WRITE 0x0002 value=0x0044 ret=0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_write(&amp, 0x0003,
					0x0080 | model->amps[i].channel_select);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x WRITE 0x0003 channel-select=0x%04x ret=0x%x\n",
			   amp.addr, 0x0080 | model->amps[i].channel_select, ret);

		ret = cs8409_i2c_write(&amp, 0x0006, model->analog_control);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x WRITE 0x0006 analog=0x%04x ret=0x%x\n",
			  amp.addr, model->analog_control, ret);

		ret = cs8409_i2c_write(&amp, 0x0008, 0x0018);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x WRITE 0x0008 config=0x0018 ret=0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_write(&amp, 0x0004,
					model->amps[i].digital_volume);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x WRITE 0x0004 digital-volume=0x%04x ret=0x%x\n",
			   amp.addr, model->amps[i].digital_volume, ret);

		ret = cs8409_i2c_write(&amp, 0x0013, 0x0000);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x WRITE 0x0013 value=0x0000 ret=0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_write(&amp, 0x0002, 0x0044);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x WRITE 0x0002 again value=0x0044 ret=0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_write(&amp, 0x0001, 0x00fd);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x WRITE 0x0001 enabled=0x00fd ret=0x%x\n",
			   amp.addr, ret);

		ret = cs8409_i2c_read(&amp, 0x0001);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x READ BACK 0x0001 POWER = 0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_read(&amp, 0x0002);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x READ BACK 0x0002 DIGITAL = 0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_read(&amp, 0x0003);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x READ BACK 0x0003 CHANNEL_SELECT = 0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_read(&amp, 0x0004);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x READ BACK 0x0004 DIGITAL_VOLUME = 0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_read(&amp, 0x0006);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x READ BACK 0x0006 ANALOG = 0x%x\n", amp.addr, ret);

		ret = cs8409_i2c_read(&amp, 0x0008);
		if (ret < 0)
			return ret;
		codec_dbg(codec, "TAS5764L 0x%x READ BACK 0x0008 CONFIG_ERROR = 0x%x\n", amp.addr, ret);
	}

	codec_info(codec, "%s TAS5764L channel setup end\n", model->name);
	return 0;
}


static void imac_cs8409_tdm_setup_amps12(struct hda_codec *codec)
{
	codec_info(codec, "iMac CS8409 TDM amps12 setup start\n");

	imac_cs8409_coef_write(codec, 0x0019, 0x0800);
	/*
	 * The iMac19,2 four-channel path uses 32-bit TDM cells carrying
	 * 24-bit payloads at bit positions 0/32/64/96.  The imported iMac18,3
	 * experiment used 0x0818 (start position 24) for ASP1.A right; keep
	 * that unrelated model unchanged, but restore the Apple-observed and
	 * upstream-CS8409 position 32 for the model-specific diagnostic path.
	 */
	imac_cs8409_coef_write(codec, 0x001a,
				 codec->fixup_id == CS8409_FIXUP_IMAC19_2 ?
				 0x0820 : 0x0818);
	imac_cs8409_coef_write(codec, 0x001b, 0x0840);
	imac_cs8409_coef_write(codec, 0x001c, 0x0860);

	imac_cs8409_coef_write(codec, 0x0003, 0x8000);
	imac_cs8409_coef_write(codec, 0x0004, 0x08ff);
	imac_cs8409_coef_write(codec, 0x0005, 0x0001);

	imac_cs8409_coef_write(codec, 0x0002, 0x0280);
	imac_cs8409_coef_write(codec, 0x0082, 0x5400);
	imac_cs8409_coef_write(codec, 0x0001, 0x0220);

	imac_cs8409_coef_write(codec, 0x0008, 0x0042);
	imac_cs8409_coef_write(codec, 0x0007, 0x10ff);

	codec_dbg(codec, "CS8409 COEF READ BACK 0x0008 = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x0008));
	codec_dbg(codec, "CS8409 COEF READ BACK 0x0007 = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x0007));

	imac_cs8409_coef_write(codec, 0x006b, 0x001f);
	codec_dbg(codec, "CS8409 COEF READ IMMEDIATE 0x006b = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x006b));

	imac_cs8409_coef_write(codec, 0x0071, 0x400f);
	codec_dbg(codec, "CS8409 COEF READ IMMEDIATE 0x0071 = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x0071));

	codec_dbg(codec, "CS8409 COEF READ BACK 0x0019 = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x0019));
	codec_dbg(codec, "CS8409 COEF READ BACK 0x001a = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x001a));
	codec_dbg(codec, "CS8409 COEF READ BACK 0x001b = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x001b));
	codec_dbg(codec, "CS8409 COEF READ BACK 0x001c = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x001c));
	codec_dbg(codec, "CS8409 COEF READ BACK 0x0003 = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x0003));
	codec_dbg(codec, "CS8409 COEF READ BACK 0x0004 = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x0004));
	codec_dbg(codec, "CS8409 COEF READ BACK 0x0005 = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x0005));
	codec_dbg(codec, "CS8409 COEF READ BACK 0x0002 = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x0002));
	codec_dbg(codec, "CS8409 COEF READ BACK 0x0082 = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x0082));
	codec_dbg(codec, "CS8409 COEF READ BACK 0x0001 = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x0001));
	codec_dbg(codec, "CS8409 COEF READ BACK 0x006b = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x006b));
	codec_dbg(codec, "CS8409 COEF READ BACK 0x0071 = 0x%x\n",
		   imac_cs8409_coef_read(codec, 0x0071));

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0, 0x7f0, 0x00b6);

	codec_info(codec, "iMac CS8409 TDM amps12 setup end\n");
}

static void imac_cs42l83_configure_serial_port(struct hda_codec *codec)
{
	struct sub_codec cs42l83;
	int ret;

	codec_info(codec, "iMac cs42l83 configure serial port start\n");

	memset(&cs42l83, 0, sizeof(cs42l83));

	cs42l83.codec = codec;
	cs42l83.addr = 0x90;
	cs42l83.paged = 1;
	cs42l83.last_page = 0;
	cs42l83.suspended = 0;

	ret = cs8409_i2c_read(&cs42l83, 0x2505);
	codec_dbg(codec,
		   "CS42L83 READ 0x2505 before update = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2505, 0x0005);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2505 value=0x0005 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2505);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2505 after update = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2502);
	codec_dbg(codec,
		   "CS42L83 READ 0x2502 before update = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2502, 0x0005);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2502 value=0x0005 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2502);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2502 after update = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2503);
	codec_dbg(codec,
		   "CS42L83 READ 0x2503 before update = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2503, 0x008a);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2503 value=0x008a ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2503);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2503 after update = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2506);
	codec_dbg(codec,
		   "CS42L83 READ 0x2506 before update = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2506, 0x00ca);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2506 value=0x00ca ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2506);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2506 after update = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x1208);
	codec_dbg(codec,
		   "CS42L83 READ 0x1208 before update = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1208, 0x0002);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1208 value=0x0002 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x1208);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x1208 after update = 0x%x\n",
		   ret);

	codec_info(codec, "iMac cs42l83 configure serial port end\n");
}

static void imac_cs42l83_set_input_sample_rate(struct hda_codec *codec)
{
	struct sub_codec cs42l83;
	int ret;

	codec_info(codec, "iMac cs42l83 set input sample rate start\n");

	memset(&cs42l83, 0, sizeof(cs42l83));

	cs42l83.codec = codec;
	cs42l83.addr = 0x90;
	cs42l83.paged = 1;
	cs42l83.last_page = 0;
	cs42l83.suspended = 0;

	ret = cs8409_i2c_read(&cs42l83, 0x130b);
	codec_dbg(codec,
		   "CS42L83 READ 0x130b at sample rate start = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2601);
	codec_dbg(codec,
		   "CS42L83 READ 0x2601 before update = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2601, 0x004a);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2601 value=0x004a ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2601);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2601 after update = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2503);
	codec_dbg(codec,
		   "CS42L83 READ 0x2503 keep natural sample = 0x%x\n",
		   ret);
	codec_dbg(codec, "CS42L83 SKIP WRITE 0x2503 sample keep natural\n");

	ret = cs8409_i2c_write(&cs42l83, 0x120a, 0x0000);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x120a value=0x0000 ret=0x%x\n",
		   ret);
	ret = cs8409_i2c_read(&cs42l83, 0x120a);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x120a = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x1209);
	codec_dbg(codec,
		   "CS42L83 READ 0x1209 before = 0x%x\n",
		   ret);
	if (ret < 0)
		return;

	ret |= 0x1;

	ret = cs8409_i2c_write(&cs42l83, 0x1209, ret);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1209 OR value ret=0x%x\n",
		   ret);
	ret = cs8409_i2c_read(&cs42l83, 0x1209);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x1209 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x130b);
	codec_dbg(codec,
		   "CS42L83 READ 0x130b at sample rate end = 0x%x\n",
		   ret);

	codec_info(codec, "iMac cs42l83 set input sample rate end\n");
}

static void imac_cs42l83_inithw(struct hda_codec *codec)
{
	struct sub_codec cs42l83;
	int ret;

	codec_info(codec, "iMac cs42l83 inithw start\n");

	memset(&cs42l83, 0, sizeof(cs42l83));

	cs42l83.codec = codec;
	cs42l83.addr = 0x90;
	cs42l83.paged = 1;
	cs42l83.last_page = 0;
	cs42l83.suspended = 0;

	ret = cs8409_i2c_read(&cs42l83, 0x1103);
	codec_dbg(codec,
		   "CS42L83 READ 0x1103 = 0x%x\n",
		   ret);
	ret = cs8409_i2c_write(&cs42l83, 0x1103, 0x0022);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1103 value=0x0022 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1129, 0x0001);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1129 value=0x0001 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1320, 0x000f);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1320 value=0x000f ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x131b);
	codec_dbg(codec,
		   "CS42L83 READ 0x131b = 0x%x\n",
		   ret);
	ret = cs8409_i2c_write(&cs42l83, 0x131b, 0x0003);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x131b value=0x0003 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2302, 0x003f);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2302 value=0x003f ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1f01, 0x0000);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1f01 value=0x0000 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1121, 0x00a6);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1121 value=0x00a6 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1316, 0x0000);
	ret = cs8409_i2c_write(&cs42l83, 0x1317, 0x0000);
	ret = cs8409_i2c_write(&cs42l83, 0x1318, 0x0000);
	ret = cs8409_i2c_write(&cs42l83, 0x1319, 0x0000);
	ret = cs8409_i2c_write(&cs42l83, 0x131a, 0x0000);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1316-0x131a value=0x0000 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x131c);
	codec_dbg(codec,
		   "CS42L83 READ 0x131c = 0x%x\n",
		   ret);
	ret = cs8409_i2c_write(&cs42l83, 0x131c, 0x001a);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x131c value=0x001a ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x131e, 0x0000);
	ret = cs8409_i2c_write(&cs42l83, 0x131f, 0x0000);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x131e-0x131f value=0x0000 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x1101);
	codec_dbg(codec,
		   "CS42L83 READ 0x1101 inithw = 0x%x\n",
		   ret);
	ret = cs8409_i2c_write(&cs42l83, 0x1101, 0x00fe);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1101 inithw value=0x00fe ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x1b75);
	codec_dbg(codec,
		   "CS42L83 READ 0x1b75 = 0x%x\n",
		   ret);
	ret = cs8409_i2c_write(&cs42l83, 0x1b75, 0x009f);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1b75 value=0x009f ret=0x%x\n",
		   ret);

	codec_info(codec, "iMac cs42l83 inithw end\n");
}

static void imac_cs42l83_audio_output(struct hda_codec *codec)
{
	struct sub_codec cs42l83;
	int ret;

	codec_info(codec, "iMac cs42l83 audio output start\n");

	memset(&cs42l83, 0, sizeof(cs42l83));

	cs42l83.codec = codec;
	cs42l83.addr = 0x90;
	cs42l83.paged = 1;
	cs42l83.last_page = 0;
	cs42l83.suspended = 0;

	ret = cs8409_i2c_write(&cs42l83, 0x1207, 0x0020);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1207 ASP CLOCK = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x130b);
	codec_dbg(codec,
		   "CS42L83 READ 0x130b after 0x1207 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1501, 0x0001);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1501 PLL ON = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x130b);
	codec_dbg(codec,
		   "CS42L83 READ 0x130b after 0x1501 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x1107);
	codec_dbg(codec,
		"CS42L83 READ 0x1107 before OSC ON = 0x%x\n",
		ret);

	/*
	ret = cs8409_i2c_write(&cs42l83, 0x1107, 0x0001);
	codec_dbg(codec,
		"CS42L83 WRITE 0x1107 OSC ON value=0x0001 ret=0x%x\n",
		ret);

	ret = cs8409_i2c_read(&cs42l83, 0x1107);
	codec_dbg(codec,
		"CS42L83 READ BACK 0x1107 after OSC ON = 0x%x\n",
		ret);

	ret = cs8409_i2c_read(&cs42l83, 0x130b);
	codec_dbg(codec,
		   "CS42L83 READ 0x130b after 0x1107 OSC ON block = 0x%x\n",
		   ret);
	*/

	ret = cs8409_i2c_write(&cs42l83, 0x1f06, 0x0002);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1f06 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x130b);
	codec_dbg(codec,
		   "CS42L83 READ 0x130b after 0x1f06 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2001, 0x000d);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2001 value=0x000d ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2001);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2001 after update 0x000d = 0x%x\n",
		   ret);

	codec_dbg(codec, "CS42L83 keep 0x2001 output state\n");

	ret = cs8409_i2c_write(&cs42l83, 0x107e, 0x0099);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x107e value=0x0099 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1b08, 0x0030);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1b08 value=0x0030 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2102, 0x0000);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2102 value=0x0000 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x107e, 0x0000);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x107e restore value=0x0000 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1101, 0x00fe);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1101 output power = 0x00fe ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x1101);
	codec_dbg(codec,
		   "CS42L83 READBACK 0x1101 output power after 0x00fe = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1101, 0x009e);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1101 output power = 0x009e ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1101, 0x0096);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1101 output power = 0x0096 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x130b);
	codec_dbg(codec,
		   "CS42L83 READ 0x130b SRC lock after output power = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2a04, 0x0000);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2a04 value=0x0000 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2a04);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2a04 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2a07, 0x0020);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2a07 value=0x0020 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2a07);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2a07 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2301, 0x0000);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2301 MIXER A VOL value=0x0000 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2303, 0x0000);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2303 value=0x0000 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2a02, 0x0002);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2a02 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2a05, 0x0002);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2a05 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x100b, 0x0000);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x100b value=0x0000 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2301);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2301 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2303);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2303 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2a02);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2a02 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2a05);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2a05 = 0x%x\n",
		   ret);

	codec_info(codec, "iMac cs42l83 audio output end\n");
}

static int imac_cs42l83_buffers_on(struct hda_codec *codec)
{
	struct sub_codec cs42l83;
	int ret;

	codec_info(codec, "iMac cs42l83 buffers on start\n");

	memset(&cs42l83, 0, sizeof(cs42l83));

	cs42l83.codec = codec;
	cs42l83.addr = 0x90;
	cs42l83.paged = 1;
	cs42l83.last_page = 0;
	cs42l83.suspended = 0;

	ret = cs8409_i2c_write(&cs42l83, 0x1101, 0x00fe);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1101 before buffers = 0x00fe ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x1101);
	codec_dbg(codec,
		   "CS42L83 READBACK 0x1101 before buffers after 0x00fe = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1101, 0x009e);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1101 before buffers = 0x009e ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1101, 0x0096);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1101 before buffers = 0x0096 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x130b);
	codec_dbg(codec,
		   "CS42L83 READ 0x130b before buffers step1 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x130b);
	codec_dbg(codec,
		   "CS42L83 READ 0x130b before buffers step2 = 0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x107e, 0x0099);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x107e value=0x0099 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x1b08, 0x0010);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x1b08 value=0x0010 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2102, 0x0020);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2102 value=0x0020 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x107e, 0x0000);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x107e value=0x0000 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_write(&cs42l83, 0x2a01, 0x000c);

	codec_dbg(codec,
		   "CS42L83 WRITE 0x2a01 BUFFERS ON value=0x000c ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2a01);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2a01 BUFFERS = 0x%x\n",
		   ret);

	codec_info(codec, "iMac cs42l83 buffers on end\n");
	return imac_i2c_result(codec);
}

static int imac_cs42l83_buffers_off(struct hda_codec *codec)
{
	struct sub_codec cs42l83;
	int ret;

	codec_info(codec, "iMac cs42l83 buffers off start\n");

	memset(&cs42l83, 0, sizeof(cs42l83));

	cs42l83.codec = codec;
	cs42l83.addr = 0x90;
	cs42l83.paged = 1;
	cs42l83.last_page = 0;
	cs42l83.suspended = 0;

	ret = cs8409_i2c_write(&cs42l83, 0x2a01, 0x0000);
	codec_dbg(codec,
		   "CS42L83 WRITE 0x2a01 BUFFERS OFF value=0x0000 ret=0x%x\n",
		   ret);

	ret = cs8409_i2c_read(&cs42l83, 0x2a01);
	codec_dbg(codec,
		   "CS42L83 READ BACK 0x2a01 BUFFERS OFF = 0x%x\n",
		   ret);

	codec_info(codec, "iMac cs42l83 buffers off end\n");
	return imac_i2c_result(codec);
}

/**
 * cs8409_i2c_bulk_write - CS8409 I2C Write Sequence.
 * @scodec: the codec instance
 * @seq: Register Sequence to write
 * @count: Number of registeres to write
 *
 * Returns negative on error.
 */
static int cs8409_i2c_bulk_write(struct sub_codec *scodec, const struct cs8409_i2c_param *seq,
				 int count)
{
	struct hda_codec *codec = scodec->codec;
	struct cs8409_spec *spec = codec->spec;
	unsigned int i2c_reg_data;
	int i;

	if (scodec->suspended)
		return -EPERM;

	guard(mutex)(&spec->i2c_mux);
	cs8409_set_i2c_dev_addr(codec, scodec->addr);

	for (i = 0; i < count; i++) {
		cs8409_enable_i2c_clock(codec);
		if (cs8409_i2c_set_page(scodec, seq[i].addr))
			goto error;

		i2c_reg_data = ((seq[i].addr << 8) & 0x0ff00) | (seq[i].value & 0x0ff);
		cs8409_vendor_coef_set(codec, CS8409_I2C_QWRITE, i2c_reg_data);

		if (cs8409_i2c_wait_complete(codec) < 0)
			goto error;
		/* Certain use cases may require a delay
		 * after a write operation before proceeding.
		 */
		if (seq[i].delay)
			fsleep(seq[i].delay);
	}

	return 0;

error:
	codec_err(codec, "I2C Bulk Write Failed 0x%02x\n", scodec->addr);
	return -EIO;
}

static int cs8409_init(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;
	int ret = snd_hda_gen_init(codec);

	if (!ret) {
		if (codec->fixup_id != CS8409_FIXUP_IMAC19_2)
			spec->imac_init_error = 0;
		snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_INIT);
		if (cs8409_is_imac_amp(codec) && spec->imac_init_error)
			ret = spec->imac_init_error;
	}
	if (codec->fixup_id == CS8409_FIXUP_IMAC19_2)
		spec->imac_init_error = ret;

	return ret;
}

static const struct hda_pcm_stream cs42l83_apple_pcm_analog_playback = {
	.rates = SNDRV_PCM_RATE_44100,
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.maxbps = 32,
};

static const struct hda_pcm_stream cs42l83_apple_pcm_analog_capture = {
	.rates = SNDRV_PCM_RATE_44100,
};

static int imac_playback_pcm_open(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream);
static int imac_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     unsigned int stream_tag,
				     unsigned int format,
				     struct snd_pcm_substream *substream);
static int imac_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream);
static int imac_playback_pcm_close(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream);
static int imac_capture_pcm_open(struct hda_pcm_stream *hinfo,
				 struct hda_codec *codec,
				 struct snd_pcm_substream *substream);
static int imac_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    unsigned int stream_tag,
				    unsigned int format,
				    struct snd_pcm_substream *substream);
static void imac_unsol_event(struct hda_codec *codec, unsigned int res);

static int imac_hw_init(struct hda_codec *codec)
{
	int first_readiness_error;
	int ret;

	codec_info(codec, "iMac amp init start\n");

	imac_cs8409_tdm_setup_amps12(codec);
	imac_enable_i2c(codec);
	imac_gpio_reset(codec);

	/*
	 * Preserve V7's exact two-pass readiness sequence and timing.  The first
	 * pass is diagnostic and may fail transaction-by-transaction; the second
	 * pass and every later operation are required.
	 */
	imac_i2c_begin(codec, true);
	imac_cs42l83_reset(codec);
	first_readiness_error = imac_i2c_result(codec);
	if (first_readiness_error)
		codec_warn(codec,
			   "iMac CS42L83 initial readiness pass failed: %d; retrying unchanged sequence\n",
			   first_readiness_error);

	msleep(20);
	imac_i2c_begin(codec, false);
	imac_cs42l83_reset(codec);
	ret = imac_i2c_result(codec);
	if (ret) {
		codec_err(codec, "iMac CS42L83 final readiness pass failed: %d\n", ret);
		return ret;
	}
	if (first_readiness_error)
		codec_info(codec, "iMac CS42L83 readiness recovered on final pass\n");

	imac_cs42l83_inithw(codec);
	ret = imac_i2c_result(codec);
	if (ret)
		return ret;

	imac_cs42l83_patch(codec);
	ret = imac_i2c_result(codec);
	if (ret)
		return ret;

	imac_cs42l83_configure_serial_port(codec);
	ret = imac_i2c_result(codec);
	if (ret)
		return ret;

	imac_cs42l83_set_input_sample_rate(codec);
	ret = imac_i2c_result(codec);
	if (ret)
		return ret;

	imac_cs42l83_audio_output(codec);
	ret = imac_i2c_result(codec);
	if (ret)
		return ret;

	ret = imac_tas576_boot_reset_setup(codec);
	if (ret) {
		codec_err(codec, "iMac TAS boot configuration failed: %d\n", ret);
		return ret;
	}

	ret = imac_tas576_tdm_slot_setup(codec);
	if (ret) {
		codec_err(codec, "iMac TAS channel configuration failed: %d\n", ret);
		return ret;
	}

	ret = imac_tdm_extra_sync_setup(codec);
	if (ret) {
		codec_err(codec, "iMac required legacy I2C sequence failed: %d\n", ret);
		return ret;
	}

	codec_info(codec, "iMac amp init end\n");
	return 0;
}

static void cs8409_fixup_imac_common(struct hda_codec *codec, int action)
{
	struct cs8409_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PROBE) {
		codec_info(codec, "iMac probe hook setup\n");
		spec->gen.stream_analog_playback = &cs42l83_apple_pcm_analog_playback;
		spec->gen.stream_analog_capture = &cs42l83_apple_pcm_analog_capture;
		spec->unsol_event = imac_unsol_event;
		return;
	}

	if (action != HDA_FIXUP_ACT_INIT)
		return;

	spec->imac_init_error = imac_hw_init(codec);
}

void cs8409_fixup_imac_amp(struct hda_codec *codec,
			  const struct hda_fixup *fix, int action)
{
	cs8409_fixup_imac_common(codec, action);
}

void cs8409_fixup_imac19_2(struct hda_codec *codec,
			   const struct hda_fixup *fix, int action)
{
	cs8409_fixup_imac_common(codec, action);
}

static int cs8409_build_controls(struct hda_codec *codec)
{
	int err;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_BUILD);

	return 0;
}

/*
 * The generic parser sees the two fixed speaker pins as a 2.1-style layout
 * and otherwise attaches snd_pcm_2_1_chmaps, whose four-channel map contains
 * FL, FR, LFE, LFE.  The iMac19,2 diagnostic path needs four distinct PCM
 * lanes; their physical transducer roles are deliberately left unknown until
 * measured.  Standard 4.0 labels provide four unique lanes without claiming
 * that the rear labels describe physical rear speakers.
 */
static int cs8409_build_pcms(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;
	int err;

	err = snd_hda_gen_build_pcms(codec);
	if (err < 0)
		return err;

	if (cs8409_is_imac_amp(codec) && spec->gen.pcm_rec[0]) {
		struct hda_pcm_stream *playback =
			&spec->gen.pcm_rec[0]->stream[SNDRV_PCM_STREAM_PLAYBACK];
		struct hda_pcm_stream *capture =
			&spec->gen.pcm_rec[0]->stream[SNDRV_PCM_STREAM_CAPTURE];

		if (codec->fixup_id == CS8409_FIXUP_IMAC19_2) {
			playback->chmap = snd_pcm_std_chmaps;
			if (capture->substreams) {
				spec->imac_capture_open = capture->ops.open;
				spec->imac_capture_prepare = capture->ops.prepare;
				capture->ops.open = imac_capture_pcm_open;
				capture->ops.prepare = imac_capture_pcm_prepare;
			}
		}

		/*
		 * Generic PCM hooks cannot return errors.  The iMac path needs
		 * prepare/open failures to reach ALSA, so only this model's PCM ops
		 * are replaced after the generic stream has been constructed.
		 */
		playback->ops.open = imac_playback_pcm_open;
		playback->ops.prepare = imac_playback_pcm_prepare;
		playback->ops.cleanup = imac_playback_pcm_cleanup;
		playback->ops.close = imac_playback_pcm_close;
	}

	return 0;
}

static int imac_capture_pcm_open(struct hda_pcm_stream *hinfo,
				 struct hda_codec *codec,
				 struct snd_pcm_substream *substream)
{
	struct cs8409_spec *spec = codec->spec;
	int ret = spec->imac_init_error;

	if (ret < 0)
		return ret;
	if (!spec->imac_capture_open)
		return -ENODEV;

	return spec->imac_capture_open(hinfo, codec, substream);
}

static int imac_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    unsigned int stream_tag,
				    unsigned int format,
				    struct snd_pcm_substream *substream)
{
	struct cs8409_spec *spec = codec->spec;
	int ret = spec->imac_init_error;

	if (ret < 0)
		return ret;
	if (!spec->imac_capture_prepare)
		return -ENODEV;

	return spec->imac_capture_prepare(hinfo, codec, stream_tag, format,
					  substream);
}

/* Enable/Disable Unsolicited Response */
static void cs8409_enable_ur(struct hda_codec *codec, int flag)
{
	struct cs8409_spec *spec = codec->spec;
	unsigned int ur_gpios = 0;
	int i;

	for (i = 0; i < spec->num_scodecs; i++)
		ur_gpios |= spec->scodecs[i]->irq_mask;

	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK,
			    flag ? ur_gpios : 0);

	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_UNSOLICITED_ENABLE,
			    flag ? AC_UNSOL_ENABLED : 0);
}

static void cs8409_fix_caps(struct hda_codec *codec, unsigned int nid)
{
	int caps;

	/* CS8409 is simple HDA bridge and intended to be used with a remote
	 * companion codec. Most of input/output PIN(s) have only basic
	 * capabilities. Receive and Transmit NID(s) have only OUTC and INC
	 * capabilities and no presence detect capable (PDC) and call to
	 * snd_hda_gen_build_controls() will mark them as non detectable
	 * phantom jacks. However, a companion codec may be
	 * connected to these pins which supports jack detect
	 * capabilities. We have to override pin capabilities,
	 * otherwise they will not be created as input devices.
	 */
	caps = snd_hdac_read_parm(&codec->core, nid, AC_PAR_PIN_CAP);
	if (caps >= 0)
		snd_hdac_override_parm(&codec->core, nid, AC_PAR_PIN_CAP,
				       (caps | (AC_PINCAP_IMP_SENSE | AC_PINCAP_PRES_DETECT)));

	snd_hda_override_wcaps(codec, nid, (get_wcaps(codec, nid) | AC_WCAP_UNSOL_CAP));
}

static int cs8409_spk_sw_gpio_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs8409_spec *spec = codec->spec;

	ucontrol->value.integer.value[0] = !!(spec->gpio_data & spec->speaker_pdn_gpio);
	return 0;
}

static int cs8409_spk_sw_gpio_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs8409_spec *spec = codec->spec;
	unsigned int gpio_data;

	gpio_data = (spec->gpio_data & ~spec->speaker_pdn_gpio) |
		(ucontrol->value.integer.value[0] ? spec->speaker_pdn_gpio : 0);
	if (gpio_data == spec->gpio_data)
		return 0;
	spec->gpio_data = gpio_data;
	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DATA, spec->gpio_data);
	return 1;
}

static const struct snd_kcontrol_new cs8409_spk_sw_ctrl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = snd_ctl_boolean_mono_info,
	.get = cs8409_spk_sw_gpio_get,
	.put = cs8409_spk_sw_gpio_put,
};

/******************************************************************************
 *                        CS42L42 Specific Functions
 ******************************************************************************/

int cs42l42_volume_info(struct snd_kcontrol *kctrl, struct snd_ctl_elem_info *uinfo)
{
	unsigned int ofs = get_amp_offset(kctrl);
	u8 chs = get_amp_channels(kctrl);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->value.integer.step = 1;
	uinfo->count = chs == 3 ? 2 : 1;

	switch (ofs) {
	case CS42L42_VOL_DAC:
		uinfo->value.integer.min = CS42L42_HP_VOL_REAL_MIN;
		uinfo->value.integer.max = CS42L42_HP_VOL_REAL_MAX;
		break;
	case CS42L42_VOL_ADC:
		uinfo->value.integer.min = CS42L42_AMIC_VOL_REAL_MIN;
		uinfo->value.integer.max = CS42L42_AMIC_VOL_REAL_MAX;
		break;
	default:
		break;
	}

	return 0;
}

int cs42l42_volume_get(struct snd_kcontrol *kctrl, struct snd_ctl_elem_value *uctrl)
{
	struct hda_codec *codec = snd_kcontrol_chip(kctrl);
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42 = spec->scodecs[get_amp_index(kctrl)];
	int chs = get_amp_channels(kctrl);
	unsigned int ofs = get_amp_offset(kctrl);
	long *valp = uctrl->value.integer.value;

	switch (ofs) {
	case CS42L42_VOL_DAC:
		if (chs & BIT(0))
			*valp++ = cs42l42->vol[ofs];
		if (chs & BIT(1))
			*valp = cs42l42->vol[ofs+1];
		break;
	case CS42L42_VOL_ADC:
		if (chs & BIT(0))
			*valp = cs42l42->vol[ofs];
		break;
	default:
		break;
	}

	return 0;
}

static void cs42l42_mute(struct sub_codec *cs42l42, int vol_type,
	unsigned int chs, bool mute)
{
	if (mute) {
		if (vol_type == CS42L42_VOL_DAC) {
			if (chs & BIT(0))
				cs8409_i2c_write(cs42l42, CS42L42_MIXER_CHA_VOL, 0x3f);
			if (chs & BIT(1))
				cs8409_i2c_write(cs42l42, CS42L42_MIXER_CHB_VOL, 0x3f);
		} else if (vol_type == CS42L42_VOL_ADC) {
			if (chs & BIT(0))
				cs8409_i2c_write(cs42l42, CS42L42_ADC_VOLUME, 0x9f);
		}
	} else {
		if (vol_type == CS42L42_VOL_DAC) {
			if (chs & BIT(0))
				cs8409_i2c_write(cs42l42, CS42L42_MIXER_CHA_VOL,
					-(cs42l42->vol[CS42L42_DAC_CH0_VOL_OFFSET])
					& CS42L42_MIXER_CH_VOL_MASK);
			if (chs & BIT(1))
				cs8409_i2c_write(cs42l42, CS42L42_MIXER_CHB_VOL,
					-(cs42l42->vol[CS42L42_DAC_CH1_VOL_OFFSET])
					& CS42L42_MIXER_CH_VOL_MASK);
		} else if (vol_type == CS42L42_VOL_ADC) {
			if (chs & BIT(0))
				cs8409_i2c_write(cs42l42, CS42L42_ADC_VOLUME,
					cs42l42->vol[CS42L42_ADC_VOL_OFFSET]
					& CS42L42_REG_AMIC_VOL_MASK);
		}
	}
}

int cs42l42_volume_put(struct snd_kcontrol *kctrl, struct snd_ctl_elem_value *uctrl)
{
	struct hda_codec *codec = snd_kcontrol_chip(kctrl);
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42 = spec->scodecs[get_amp_index(kctrl)];
	int chs = get_amp_channels(kctrl);
	unsigned int ofs = get_amp_offset(kctrl);
	long *valp = uctrl->value.integer.value;

	switch (ofs) {
	case CS42L42_VOL_DAC:
		if (chs & BIT(0))
			cs42l42->vol[ofs] = *valp;
		if (chs & BIT(1)) {
			valp++;
			cs42l42->vol[ofs + 1] = *valp;
		}
		if (spec->playback_started)
			cs42l42_mute(cs42l42, CS42L42_VOL_DAC, chs, false);
		break;
	case CS42L42_VOL_ADC:
		if (chs & BIT(0))
			cs42l42->vol[ofs] = *valp;
		if (spec->capture_started)
			cs42l42_mute(cs42l42, CS42L42_VOL_ADC, chs, false);
		break;
	default:
		break;
	}

	return 0;
}

/*
 * Apple synchronizes converters 0x02 and 0x03 by briefly removing their
 * stream IDs around coefficient 0x17 updates.  The imported experimental path
 * restored a hard-coded Apple boot-trace format (0x4033: 44.1 kHz, 24-bit,
 * four channels), even when the HDA controller selected another valid runtime
 * format.  That makes the converter and DMA stream formats disagree.
 *
 * Linux's generic prepare has already selected the real stream tag, format,
 * and offsets before this hook runs.  Preserve those live values, validate the
 * expected 0/2 four-channel topology, perform only the observed synchronization
 * sequence, and restore the exact stream IDs.  The formats are never cleared.
 * Raw verb writes are restored to the same values cached by the HDA core, so
 * the core's converter cache remains coherent.
 */
static void imac_sync_4ch_converters(struct hda_codec *codec)
{
	unsigned int conv2, conv3, format2, format3;
	unsigned int check2, check3;

	conv2 = snd_hda_codec_read(codec, 0x02, 0, AC_VERB_GET_CONV, 0) & 0xff;
	conv3 = snd_hda_codec_read(codec, 0x03, 0, AC_VERB_GET_CONV, 0) & 0xff;
	format2 = snd_hda_codec_read(codec, 0x02, 0,
					   AC_VERB_GET_STREAM_FORMAT, 0) & 0xffff;
	format3 = snd_hda_codec_read(codec, 0x03, 0,
					   AC_VERB_GET_STREAM_FORMAT, 0) & 0xffff;

	codec_info(codec,
		   "iMac 4ch converter state: 0x02 stream=0x%02x format=0x%04x, 0x03 stream=0x%02x format=0x%04x\n",
		   conv2, format2, conv3, format3);

	if (!(conv2 & 0xf0) || !(conv3 & 0xf0) ||
	    (conv2 & 0xf0) != (conv3 & 0xf0) ||
	    (conv2 & 0x0f) != 0 || (conv3 & 0x0f) != 2 ||
	    !format2 || format2 != format3) {
		codec_warn(codec,
			   "iMac 4ch converter topology unexpected; synchronization skipped\n");
		return;
	}

	snd_hda_codec_write(codec, CS8409_VENDOR_NID, 0,
			    AC_VERB_SET_PROC_STATE, 0x00000001);
	snd_hda_codec_write(codec, 0x02, 0,
			    AC_VERB_SET_CHANNEL_STREAMID, 0x00);
	imac_cs8409_coef_write(codec, 0x0017, 0x0001);
	snd_hda_codec_write(codec, 0x03, 0,
			    AC_VERB_SET_CHANNEL_STREAMID, 0x00);
	imac_cs8409_coef_write(codec, 0x0017, 0x0003);

	snd_hda_codec_write(codec, 0x02, 0,
			    AC_VERB_SET_CHANNEL_STREAMID, conv2);
	snd_hda_codec_write(codec, 0x03, 0,
			    AC_VERB_SET_CHANNEL_STREAMID, conv3);

	check2 = snd_hda_codec_read(codec, 0x02, 0, AC_VERB_GET_CONV, 0) & 0xff;
	check3 = snd_hda_codec_read(codec, 0x03, 0, AC_VERB_GET_CONV, 0) & 0xff;
	if (check2 != conv2 || check3 != conv3 ||
	    (snd_hda_codec_read(codec, 0x02, 0,
				 AC_VERB_GET_STREAM_FORMAT, 0) & 0xffff) != format2 ||
	    (snd_hda_codec_read(codec, 0x03, 0,
				 AC_VERB_GET_STREAM_FORMAT, 0) & 0xffff) != format3)
		codec_warn(codec,
			   "iMac 4ch converter restore verification failed\n");
}

static int imac_playback_pcm_open(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream)
{
	struct cs8409_spec *spec = codec->spec;
	unsigned int coef, gpio;
	int ret = spec->imac_init_error;

	if (codec->fixup_id == CS8409_FIXUP_IMAC19_2 && ret < 0)
		return ret;

	guard(mutex)(&spec->gen.pcm_mutex);
	ret = snd_hda_multi_out_analog_open(codec, &spec->gen.multiout,
					    substream, hinfo);
	if (ret < 0)
		return ret;

	spec->gen.active_streams |= 1 << STREAM_MULTI_OUT;
	imac_i2c_begin(codec, false);

	coef = imac_cs8409_coef_read(codec, 0x0001);
	gpio = snd_hda_codec_read(codec, codec->core.afg, 0,
				  AC_VERB_GET_GPIO_DATA, 0);
	codec_dbg(codec, "iMac playback OPEN COEF 0x0001 = 0x%x\n", coef);
	codec_dbg(codec, "iMac playback OPEN GPIO DATA = 0x%x\n", gpio);

	return 0;
}

static int imac_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     unsigned int stream_tag,
				     unsigned int format,
				     struct snd_pcm_substream *substream)
{
	struct cs8409_spec *spec = codec->spec;
	unsigned int channels = substream->runtime->channels;
	unsigned int coef, gpio;
	int ret = spec->imac_init_error;

	if (codec->fixup_id == CS8409_FIXUP_IMAC19_2 && ret < 0)
		return ret;

	ret = snd_hda_multi_out_analog_prepare(codec, &spec->gen.multiout,
					       stream_tag, format, substream);
	if (ret < 0)
		return ret;

	imac_i2c_begin(codec, false);
	coef = imac_cs8409_coef_read(codec, 0x0001);
	gpio = snd_hda_codec_read(codec, codec->core.afg, 0,
				  AC_VERB_GET_GPIO_DATA, 0);
	codec_dbg(codec, "iMac playback PREPARE COEF 0x0001 = 0x%x\n", coef);
	codec_dbg(codec, "iMac playback PREPARE GPIO DATA = 0x%x\n", gpio);

	codec_info(codec, "iMac playback prepare TDM refresh start\n");
	imac_cs8409_tdm_setup_amps12(codec);

	ret = imac_tas576_tdm_slot_setup(codec);
	if (ret < 0) {
		codec_err(codec, "iMac playback TAS prepare failed: %d\n", ret);
		return ret;
	}

	ret = imac_tdm_extra_sync_setup(codec);
	if (ret < 0) {
		codec_err(codec, "iMac playback required legacy I2C sequence failed: %d\n",
			  ret);
		return ret;
	}

	if (channels >= 4)
		imac_sync_4ch_converters(codec);
	else
		codec_dbg(codec,
			  "iMac 2ch playback: skip forced DAC stream/channel restore\n");

	snd_hda_codec_write(codec, 0x24, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40);
	snd_hda_codec_write(codec, 0x25, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40);

	ret = imac_cs42l83_buffers_on(codec);
	if (ret < 0) {
		codec_err(codec, "iMac playback CS42L83 buffer enable failed: %d\n", ret);
		return ret;
	}

	snd_hda_codec_write(codec, 0x0a, 0,
			    AC_VERB_SET_CHANNEL_STREAMID, 0x00);
	coef = imac_cs8409_coef_read(codec, 0x0001);
	codec_dbg(codec, "iMac playback PREPARE END COEF 0x0001 = 0x%x\n", coef);

	spec->playback_started = 1;
	codec_info(codec, "iMac playback prepare TDM refresh end\n");
	return 0;
}

static int imac_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	struct cs8409_spec *spec = codec->spec;
	int ret;

	ret = snd_hda_multi_out_analog_cleanup(codec, &spec->gen.multiout);
	if (ret < 0)
		return ret;

	imac_i2c_begin(codec, false);
	codec_info(codec, "iMac playback cleanup\n");
	ret = imac_cs42l83_buffers_off(codec);

	/*
	 * MACHINE-PROVEN V7 behavior: do not clear speaker converter 0x02/0x03
	 * stream IDs.  Clearing them made the next short playback silent.
	 */
	codec_info(codec, "iMac speaker DAC 0x02/0x03 stream clear skipped\n");
	snd_hda_codec_write(codec, 0x0a, 0,
			    AC_VERB_SET_CHANNEL_STREAMID, 0x00);
	snd_hda_codec_write(codec, 0x24, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00);
	snd_hda_codec_write(codec, 0x25, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00);
	snd_hda_codec_write(codec, 0x2c, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00);
	spec->playback_started = 0;

	if (ret < 0)
		codec_err(codec, "iMac playback cleanup buffer disable failed: %d\n", ret);
	return ret;
}

static int imac_playback_pcm_close(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream)
{
	struct cs8409_spec *spec = codec->spec;

	guard(mutex)(&spec->gen.pcm_mutex);
	spec->gen.active_streams &= ~(1 << STREAM_MULTI_OUT);
	codec_info(codec, "iMac playback close\n");
	return 0;
}

/* Pristine upstream behavior for all CS42L42 companion-codec models. */
static void cs42l42_playback_pcm_hook(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				   struct snd_pcm_substream *substream,
				   int action)
{
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42;
	int i;
	bool mute;

	switch (action) {
	case HDA_GEN_PCM_ACT_PREPARE:
		mute = false;
		spec->playback_started = 1;
		break;
	case HDA_GEN_PCM_ACT_CLEANUP:
		mute = true;
		spec->playback_started = 0;
		break;
	default:
		return;
	}

	for (i = 0; i < spec->num_scodecs; i++) {
		cs42l42 = spec->scodecs[i];
		cs42l42_mute(cs42l42, CS42L42_VOL_DAC, 0x3, mute);
	}
}

static void cs42l42_capture_pcm_hook(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream,
				   int action)
{
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42;
	int i;
	bool mute;

	switch (action) {
	case HDA_GEN_PCM_ACT_PREPARE:
		mute = false;
		spec->capture_started = 1;
		break;
	case HDA_GEN_PCM_ACT_CLEANUP:
		mute = true;
		spec->capture_started = 0;
		break;
	default:
		return;
	}

	for (i = 0; i < spec->num_scodecs; i++) {
		cs42l42 = spec->scodecs[i];
		cs42l42_mute(cs42l42, CS42L42_VOL_ADC, 0x3, mute);
	}
}

/* Configure CS42L42 slave codec for jack autodetect */
static void cs42l42_enable_jack_detect(struct sub_codec *cs42l42)
{
	cs8409_i2c_write(cs42l42, CS42L42_HSBIAS_SC_AUTOCTL, cs42l42->hsbias_hiz);
	/* Clear WAKE# */
	cs8409_i2c_write(cs42l42, CS42L42_WAKE_CTL, 0x00C1);
	/* Wait ~2.5ms */
	usleep_range(2500, 3000);
	/* Set mode WAKE# output follows the combination logic directly */
	cs8409_i2c_write(cs42l42, CS42L42_WAKE_CTL, 0x00C0);
	/* Clear interrupts status */
	cs8409_i2c_read(cs42l42, CS42L42_TSRS_PLUG_STATUS);
	/* Enable interrupt */
	cs8409_i2c_write(cs42l42, CS42L42_TSRS_PLUG_INT_MASK, 0xF3);
}

/* Enable and run CS42L42 slave codec jack auto detect */
static void cs42l42_run_jack_detect(struct sub_codec *cs42l42)
{
	/* Clear interrupts */
	cs8409_i2c_read(cs42l42, CS42L42_CODEC_STATUS);
	cs8409_i2c_read(cs42l42, CS42L42_DET_STATUS1);
	cs8409_i2c_write(cs42l42, CS42L42_TSRS_PLUG_INT_MASK, 0xFF);
	cs8409_i2c_read(cs42l42, CS42L42_TSRS_PLUG_STATUS);

	cs8409_i2c_write(cs42l42, CS42L42_PWR_CTL2, 0x87);
	cs8409_i2c_write(cs42l42, CS42L42_DAC_CTL2, 0x86);
	cs8409_i2c_write(cs42l42, CS42L42_MISC_DET_CTL, 0x07);
	cs8409_i2c_write(cs42l42, CS42L42_CODEC_INT_MASK, 0xFD);
	cs8409_i2c_write(cs42l42, CS42L42_HSDET_CTL2, 0x80);
	/* Wait ~20ms*/
	usleep_range(20000, 25000);
	cs8409_i2c_write(cs42l42, CS42L42_HSDET_CTL1, 0x77);
	cs8409_i2c_write(cs42l42, CS42L42_HSDET_CTL2, 0xc0);
}

static int cs42l42_manual_hs_det(struct sub_codec *cs42l42)
{
	unsigned int hs_det_status;
	unsigned int hs_det_comp1;
	unsigned int hs_det_comp2;
	unsigned int hs_det_sw;
	unsigned int hs_type;

	/* Set hs detect to manual, active mode */
	cs8409_i2c_write(cs42l42, CS42L42_HSDET_CTL2,
			 (1 << CS42L42_HSDET_CTRL_SHIFT) |
			 (0 << CS42L42_HSDET_SET_SHIFT) |
			 (0 << CS42L42_HSBIAS_REF_SHIFT) |
			 (0 << CS42L42_HSDET_AUTO_TIME_SHIFT));

	/* Configure HS DET comparator reference levels. */
	cs8409_i2c_write(cs42l42, CS42L42_HSDET_CTL1,
			 (CS42L42_HSDET_COMP1_LVL_VAL << CS42L42_HSDET_COMP1_LVL_SHIFT) |
			 (CS42L42_HSDET_COMP2_LVL_VAL << CS42L42_HSDET_COMP2_LVL_SHIFT));

	/* Open the SW_HSB_HS3 switch and close SW_HSB_HS4 for a Type 1 headset. */
	cs8409_i2c_write(cs42l42, CS42L42_HS_SWITCH_CTL, CS42L42_HSDET_SW_COMP1);

	msleep(100);

	hs_det_status = cs8409_i2c_read(cs42l42, CS42L42_HS_DET_STATUS);

	hs_det_comp1 = (hs_det_status & CS42L42_HSDET_COMP1_OUT_MASK) >>
			CS42L42_HSDET_COMP1_OUT_SHIFT;
	hs_det_comp2 = (hs_det_status & CS42L42_HSDET_COMP2_OUT_MASK) >>
			CS42L42_HSDET_COMP2_OUT_SHIFT;

	/* Close the SW_HSB_HS3 switch for a Type 2 headset. */
	cs8409_i2c_write(cs42l42, CS42L42_HS_SWITCH_CTL, CS42L42_HSDET_SW_COMP2);

	msleep(100);

	hs_det_status = cs8409_i2c_read(cs42l42, CS42L42_HS_DET_STATUS);

	hs_det_comp1 |= ((hs_det_status & CS42L42_HSDET_COMP1_OUT_MASK) >>
			CS42L42_HSDET_COMP1_OUT_SHIFT) << 1;
	hs_det_comp2 |= ((hs_det_status & CS42L42_HSDET_COMP2_OUT_MASK) >>
			CS42L42_HSDET_COMP2_OUT_SHIFT) << 1;

	/* Use Comparator 1 with 1.25V Threshold. */
	switch (hs_det_comp1) {
	case CS42L42_HSDET_COMP_TYPE1:
		hs_type = CS42L42_PLUG_CTIA;
		hs_det_sw = CS42L42_HSDET_SW_TYPE1;
		break;
	case CS42L42_HSDET_COMP_TYPE2:
		hs_type = CS42L42_PLUG_OMTP;
		hs_det_sw = CS42L42_HSDET_SW_TYPE2;
		break;
	default:
		/* Fallback to Comparator 2 with 1.75V Threshold. */
		switch (hs_det_comp2) {
		case CS42L42_HSDET_COMP_TYPE1:
			hs_type = CS42L42_PLUG_CTIA;
			hs_det_sw = CS42L42_HSDET_SW_TYPE1;
			break;
		case CS42L42_HSDET_COMP_TYPE2:
			hs_type = CS42L42_PLUG_OMTP;
			hs_det_sw = CS42L42_HSDET_SW_TYPE2;
			break;
		case CS42L42_HSDET_COMP_TYPE3:
			hs_type = CS42L42_PLUG_HEADPHONE;
			hs_det_sw = CS42L42_HSDET_SW_TYPE3;
			break;
		default:
			hs_type = CS42L42_PLUG_INVALID;
			hs_det_sw = CS42L42_HSDET_SW_TYPE4;
			break;
		}
	}

	/* Set Switches */
	cs8409_i2c_write(cs42l42, CS42L42_HS_SWITCH_CTL, hs_det_sw);

	/* Set HSDET mode to Manual—Disabled */
	cs8409_i2c_write(cs42l42, CS42L42_HSDET_CTL2,
			 (0 << CS42L42_HSDET_CTRL_SHIFT) |
			 (0 << CS42L42_HSDET_SET_SHIFT) |
			 (0 << CS42L42_HSBIAS_REF_SHIFT) |
			 (0 << CS42L42_HSDET_AUTO_TIME_SHIFT));

	/* Configure HS DET comparator reference levels. */
	cs8409_i2c_write(cs42l42, CS42L42_HSDET_CTL1,
			 (CS42L42_HSDET_COMP1_LVL_DEFAULT << CS42L42_HSDET_COMP1_LVL_SHIFT) |
			 (CS42L42_HSDET_COMP2_LVL_DEFAULT << CS42L42_HSDET_COMP2_LVL_SHIFT));

	return hs_type;
}

static int cs42l42_handle_tip_sense(struct sub_codec *cs42l42, unsigned int reg_ts_status)
{
	int status_changed = 0;

	/* TIP_SENSE INSERT/REMOVE */
	switch (reg_ts_status) {
	case CS42L42_TS_PLUG:
		if (cs42l42->no_type_dect) {
			status_changed = 1;
			cs42l42->hp_jack_in = 1;
			cs42l42->mic_jack_in = 0;
		} else {
			cs42l42_run_jack_detect(cs42l42);
		}
		break;

	case CS42L42_TS_UNPLUG:
		status_changed = 1;
		cs42l42->hp_jack_in = 0;
		cs42l42->mic_jack_in = 0;
		break;
	default:
		/* jack in transition */
		break;
	}

	codec_dbg(cs42l42->codec, "Tip Sense Detection: (%d)\n", reg_ts_status);

	return status_changed;
}

static int cs42l42_jack_unsol_event(struct sub_codec *cs42l42)
{
	int current_plug_status;
	int status_changed = 0;
	int reg_cdc_status;
	int reg_hs_status;
	int reg_ts_status;
	int type;

	/* Read jack detect status registers */
	reg_cdc_status = cs8409_i2c_read(cs42l42, CS42L42_CODEC_STATUS);
	reg_hs_status = cs8409_i2c_read(cs42l42, CS42L42_HS_DET_STATUS);
	reg_ts_status = cs8409_i2c_read(cs42l42, CS42L42_TSRS_PLUG_STATUS);

	/* If status values are < 0, read error has occurred. */
	if (reg_cdc_status < 0 || reg_hs_status < 0 || reg_ts_status < 0)
		return -EIO;

	current_plug_status = (reg_ts_status & (CS42L42_TS_PLUG_MASK | CS42L42_TS_UNPLUG_MASK))
				>> CS42L42_TS_PLUG_SHIFT;

	/* HSDET_AUTO_DONE */
	if (reg_cdc_status & CS42L42_HSDET_AUTO_DONE_MASK) {

		/* Disable HSDET_AUTO_DONE */
		cs8409_i2c_write(cs42l42, CS42L42_CODEC_INT_MASK, 0xFF);

		type = (reg_hs_status & CS42L42_HSDET_TYPE_MASK) >> CS42L42_HSDET_TYPE_SHIFT;

		/* Configure the HSDET mode. */
		cs8409_i2c_write(cs42l42, CS42L42_HSDET_CTL2, 0x80);

		if (cs42l42->no_type_dect) {
			status_changed = cs42l42_handle_tip_sense(cs42l42, current_plug_status);
		} else {
			if (type == CS42L42_PLUG_INVALID || type == CS42L42_PLUG_HEADPHONE) {
				codec_dbg(cs42l42->codec,
					  "Auto detect value not valid (%d), running manual det\n",
					  type);
				type = cs42l42_manual_hs_det(cs42l42);
			}

			switch (type) {
			case CS42L42_PLUG_CTIA:
			case CS42L42_PLUG_OMTP:
				status_changed = 1;
				cs42l42->hp_jack_in = 1;
				cs42l42->mic_jack_in = 1;
				break;
			case CS42L42_PLUG_HEADPHONE:
				status_changed = 1;
				cs42l42->hp_jack_in = 1;
				cs42l42->mic_jack_in = 0;
				break;
			default:
				status_changed = 1;
				cs42l42->hp_jack_in = 0;
				cs42l42->mic_jack_in = 0;
				break;
			}
			codec_dbg(cs42l42->codec, "Detection done (%d)\n", type);
		}

		/* Enable the HPOUT ground clamp and configure the HP pull-down */
		cs8409_i2c_write(cs42l42, CS42L42_DAC_CTL2, 0x02);
		/* Re-Enable Tip Sense Interrupt */
		cs8409_i2c_write(cs42l42, CS42L42_TSRS_PLUG_INT_MASK, 0xF3);
	} else {
		status_changed = cs42l42_handle_tip_sense(cs42l42, current_plug_status);
	}

	return status_changed;
}

static void cs42l42_resume(struct sub_codec *cs42l42)
{
	struct hda_codec *codec = cs42l42->codec;
	struct cs8409_spec *spec = codec->spec;
	struct cs8409_i2c_param irq_regs[] = {
		{ CS42L42_CODEC_STATUS, 0x00 },
		{ CS42L42_DET_INT_STATUS1, 0x00 },
		{ CS42L42_DET_INT_STATUS2, 0x00 },
		{ CS42L42_TSRS_PLUG_STATUS, 0x00 },
	};
	unsigned int fsv;

	/* Bring CS42L42 out of Reset */
	spec->gpio_data = snd_hda_codec_read(codec, CS8409_PIN_AFG, 0, AC_VERB_GET_GPIO_DATA, 0);
	spec->gpio_data |= cs42l42->reset_gpio;
	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DATA, spec->gpio_data);
	usleep_range(10000, 15000);

	cs42l42->suspended = 0;

	/* Initialize CS42L42 companion codec */
	cs8409_i2c_bulk_write(cs42l42, cs42l42->init_seq, cs42l42->init_seq_num);

	/* Clear interrupts, by reading interrupt status registers */
	cs8409_i2c_bulk_read(cs42l42, irq_regs, ARRAY_SIZE(irq_regs));

	fsv = cs8409_i2c_read(cs42l42, CS42L42_HP_CTL);
	if (cs42l42->full_scale_vol) {
		// Set the full scale volume bit
		fsv |= CS42L42_FULL_SCALE_VOL_MASK;
		cs8409_i2c_write(cs42l42, CS42L42_HP_CTL, fsv);
	}
	// Unmute analog channels A and B
	fsv = (fsv & ~CS42L42_ANA_MUTE_AB);
	cs8409_i2c_write(cs42l42, CS42L42_HP_CTL, fsv);

	/* we have to explicitly allow unsol event handling even during the
	 * resume phase so that the jack event is processed properly
	 */
	snd_hda_codec_allow_unsol_events(cs42l42->codec);

	cs42l42_enable_jack_detect(cs42l42);
}

static void cs42l42_suspend(struct sub_codec *cs42l42)
{
	struct hda_codec *codec = cs42l42->codec;
	struct cs8409_spec *spec = codec->spec;
	int reg_cdc_status = 0;
	const struct cs8409_i2c_param cs42l42_pwr_down_seq[] = {
		{ CS42L42_DAC_CTL2, 0x02 },
		{ CS42L42_HS_CLAMP_DISABLE, 0x00 },
		{ CS42L42_MIXER_CHA_VOL, 0x3F },
		{ CS42L42_MIXER_ADC_VOL, 0x3F },
		{ CS42L42_MIXER_CHB_VOL, 0x3F },
		{ CS42L42_HP_CTL, 0x0D },
		{ CS42L42_ASP_RX_DAI0_EN, 0x00 },
		{ CS42L42_ASP_CLK_CFG, 0x00 },
		{ CS42L42_PWR_CTL1, 0xFE },
		{ CS42L42_PWR_CTL2, 0x8C },
		{ CS42L42_PWR_CTL1, 0xFF },
	};

	cs8409_i2c_bulk_write(cs42l42, cs42l42_pwr_down_seq, ARRAY_SIZE(cs42l42_pwr_down_seq));

	if (read_poll_timeout(cs8409_i2c_read, reg_cdc_status,
			(reg_cdc_status & 0x1), CS42L42_PDN_SLEEP_US, CS42L42_PDN_TIMEOUT_US,
			true, cs42l42, CS42L42_CODEC_STATUS) < 0)
		codec_warn(codec, "Timeout waiting for PDN_DONE for CS42L42\n");

	/* Power down CS42L42 ASP/EQ/MIX/HP */
	cs8409_i2c_write(cs42l42, CS42L42_PWR_CTL2, 0x9C);
	cs42l42->suspended = 1;
	cs42l42->last_page = 0;
	cs42l42->hp_jack_in = 0;
	cs42l42->mic_jack_in = 0;

	/* Put CS42L42 into Reset */
	spec->gpio_data = snd_hda_codec_read(codec, CS8409_PIN_AFG, 0, AC_VERB_GET_GPIO_DATA, 0);
	spec->gpio_data &= ~cs42l42->reset_gpio;
	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DATA, spec->gpio_data);
}

static void cs8409_remove(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	/* Cancel i2c clock disable timer, and disable clock if left enabled */
	cancel_delayed_work_sync(&spec->i2c_clk_work);
	cs8409_disable_i2c_clock(codec);

	snd_hda_gen_remove(codec);
}

/******************************************************************************
 *                   BULLSEYE / WARLOCK / CYBORG Specific Functions
 *                               CS8409/CS42L42
 ******************************************************************************/

/*
 * In the case of CS8409 we do not have unsolicited events from NID's 0x24
 * and 0x34 where hs mic and hp are connected. Companion codec CS42L42 will
 * generate interrupt via gpio 4 to notify jack events. We have to overwrite
 * generic snd_hda_jack_unsol_event(), read CS42L42 jack detect status registers
 * and then notify status via generic snd_hda_jack_unsol_event() call.
 */
static void cs8409_cs42l42_jack_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42 = spec->scodecs[CS8409_CODEC0];
	struct hda_jack_tbl *jk;

	/* jack_unsol_event() will be called every time gpio line changing state.
	 * In this case gpio4 line goes up as a result of reading interrupt status
	 * registers in previous cs8409_jack_unsol_event() call.
	 * We don't need to handle this event, ignoring...
	 */
	if (res & cs42l42->irq_mask)
		return;

	if (cs42l42_jack_unsol_event(cs42l42)) {
		snd_hda_set_pin_ctl(codec, CS8409_CS42L42_SPK_PIN_NID,
				    cs42l42->hp_jack_in ? 0 : PIN_OUT);
		/* Report jack*/
		jk = snd_hda_jack_tbl_get_mst(codec, CS8409_CS42L42_HP_PIN_NID, 0);
		if (jk)
			snd_hda_jack_unsol_event(codec, (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
							AC_UNSOL_RES_TAG);
		/* Report jack*/
		jk = snd_hda_jack_tbl_get_mst(codec, CS8409_CS42L42_AMIC_PIN_NID, 0);
		if (jk)
			snd_hda_jack_unsol_event(codec, (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
							 AC_UNSOL_RES_TAG);
	}
}

static void imac_unsol_event(struct hda_codec *codec, unsigned int res)
{
	/* Exact CS42L83 GPIO event semantics are not established. */
	codec_dbg(codec, "iMac unsolicited response 0x%x safely ignored\n", res);
}

static void cs8409_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct cs8409_spec *spec = codec->spec;

	if (spec->unsol_event)
		spec->unsol_event(codec, res);
	else if (spec->num_scodecs && spec->scodecs[CS8409_CODEC0])
		cs8409_cs42l42_jack_unsol_event(codec, res);
	else
		codec_warn(codec,
			   "unsolicited response 0x%x ignored without a companion codec handler\n",
			   res);
}

/* Manage PDREF, when transition to D3hot */
static int cs8409_cs42l42_suspend(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;
	int i;

	spec->init_done = 0;

	cs8409_enable_ur(codec, 0);

	for (i = 0; i < spec->num_scodecs; i++)
		cs42l42_suspend(spec->scodecs[i]);

	/* Cancel i2c clock disable timer, and disable clock if left enabled */
	cancel_delayed_work_sync(&spec->i2c_clk_work);
	cs8409_disable_i2c_clock(codec);

	snd_hda_shutup_pins(codec);

	return 0;
}

/* Vendor specific HW configuration
 * PLL, ASP, I2C, SPI, GPIOs, DMIC etc...
 */
static void cs8409_cs42l42_hw_init(struct hda_codec *codec)
{
	const struct cs8409_cir_param *seq = cs8409_cs42l42_hw_cfg;
	const struct cs8409_cir_param *seq_bullseye = cs8409_cs42l42_bullseye_atn;
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42 = spec->scodecs[CS8409_CODEC0];

	if (spec->gpio_mask) {
		snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_MASK,
			spec->gpio_mask);
		snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DIRECTION,
			spec->gpio_dir);
		snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DATA,
			spec->gpio_data);
	}

	for (; seq->nid; seq++)
		cs8409_vendor_coef_set(codec, seq->cir, seq->coeff);

	if (codec->fixup_id == CS8409_BULLSEYE) {
		for (; seq_bullseye->nid; seq_bullseye++)
			cs8409_vendor_coef_set(codec, seq_bullseye->cir, seq_bullseye->coeff);
	}

	switch (codec->fixup_id) {
	case CS8409_CYBORG:
	case CS8409_WARLOCK_MLK_DUAL_MIC:
		/* DMIC1_MO=00b, DMIC1/2_SR=1 */
		cs8409_vendor_coef_set(codec, CS8409_DMIC_CFG, 0x0003);
		break;
	case CS8409_ODIN:
		/* ASP1/2_xxx_EN=1, ASP1/2_MCLK_EN=0, DMIC1_SCL_EN=0 */
		cs8409_vendor_coef_set(codec, CS8409_PAD_CFG_SLW_RATE_CTRL, 0xfc00);
		break;
	default:
		break;
	}

	cs42l42_resume(cs42l42);

	/* Enable Unsolicited Response */
	cs8409_enable_ur(codec, 1);
}

static int cs8409_cs42l42_exec_verb(struct hdac_device *dev, unsigned int cmd, unsigned int flags,
				    unsigned int *res)
{
	struct hda_codec *codec = container_of(dev, struct hda_codec, core);
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42 = spec->scodecs[CS8409_CODEC0];

	unsigned int nid = ((cmd >> 20) & 0x07f);
	unsigned int verb = ((cmd >> 8) & 0x0fff);

	/* CS8409 pins have no AC_PINSENSE_PRESENCE
	 * capabilities. We have to intercept 2 calls for pins 0x24 and 0x34
	 * and return correct pin sense values for read_pin_sense() call from
	 * hda_jack based on CS42L42 jack detect status.
	 */
	switch (nid) {
	case CS8409_CS42L42_HP_PIN_NID:
		if (verb == AC_VERB_GET_PIN_SENSE) {
			*res = (cs42l42->hp_jack_in) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
		break;
	case CS8409_CS42L42_AMIC_PIN_NID:
		if (verb == AC_VERB_GET_PIN_SENSE) {
			*res = (cs42l42->mic_jack_in) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
		break;
	default:
		break;
	}

	return spec->exec_verb(dev, cmd, flags, res);
}

void cs8409_cs42l42_fixups(struct hda_codec *codec, const struct hda_fixup *fix, int action)
{
	struct cs8409_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_add_verbs(codec, cs8409_cs42l42_init_verbs);
		/* verb exec op override */
		spec->exec_verb = codec->core.exec_verb;
		codec->core.exec_verb = cs8409_cs42l42_exec_verb;

		spec->scodecs[CS8409_CODEC0] = &cs8409_cs42l42_codec;
		spec->num_scodecs = 1;
		spec->scodecs[CS8409_CODEC0]->codec = codec;

		spec->gen.suppress_auto_mute = 1;
		spec->gen.no_primary_hp = 1;
		spec->gen.suppress_vmaster = 1;

		spec->speaker_pdn_gpio = 0;

		/* GPIO 5 out, 3,4 in */
		spec->gpio_dir = spec->scodecs[CS8409_CODEC0]->reset_gpio;
		spec->gpio_data = 0;
		spec->gpio_mask = 0x03f;

		/* Basic initial sequence for specific hw configuration */
		snd_hda_sequence_write(codec, cs8409_cs42l42_init_verbs);

		cs8409_fix_caps(codec, CS8409_CS42L42_HP_PIN_NID);
		cs8409_fix_caps(codec, CS8409_CS42L42_AMIC_PIN_NID);

		spec->scodecs[CS8409_CODEC0]->hsbias_hiz = 0x0020;

		switch (codec->fixup_id) {
		case CS8409_CYBORG:
			spec->scodecs[CS8409_CODEC0]->full_scale_vol =
				CS42L42_FULL_SCALE_VOL_MINUS6DB;
			spec->speaker_pdn_gpio = CS8409_CYBORG_SPEAKER_PDN;
			break;
		case CS8409_ODIN:
			spec->scodecs[CS8409_CODEC0]->full_scale_vol = CS42L42_FULL_SCALE_VOL_0DB;
			spec->speaker_pdn_gpio = CS8409_CYBORG_SPEAKER_PDN;
			break;
		case CS8409_WARLOCK_MLK:
		case CS8409_WARLOCK_MLK_DUAL_MIC:
			spec->scodecs[CS8409_CODEC0]->full_scale_vol = CS42L42_FULL_SCALE_VOL_0DB;
			spec->speaker_pdn_gpio = CS8409_WARLOCK_SPEAKER_PDN;
			break;
		default:
			spec->scodecs[CS8409_CODEC0]->full_scale_vol =
				CS42L42_FULL_SCALE_VOL_MINUS6DB;
			spec->speaker_pdn_gpio = CS8409_WARLOCK_SPEAKER_PDN;
			break;
		}

		if (spec->speaker_pdn_gpio > 0) {
			spec->gpio_dir |= spec->speaker_pdn_gpio;
			spec->gpio_data |= spec->speaker_pdn_gpio;
		}

		break;
	case HDA_FIXUP_ACT_PROBE:
		/* Fix Sample Rate to 48kHz */
		spec->gen.stream_analog_playback = &cs42l42_48k_pcm_analog_playback;
		spec->gen.stream_analog_capture = &cs42l42_48k_pcm_analog_capture;
		/* add hooks */
		spec->gen.pcm_playback_hook = cs42l42_playback_pcm_hook;
		spec->gen.pcm_capture_hook = cs42l42_capture_pcm_hook;
		if (codec->fixup_id != CS8409_ODIN)
			/* Set initial DMIC volume to -26 dB */
			snd_hda_codec_amp_init_stereo(codec, CS8409_CS42L42_DMIC_ADC_PIN_NID,
						      HDA_INPUT, 0, 0xff, 0x19);
		snd_hda_gen_add_kctl(&spec->gen, "Headphone Playback Volume",
				&cs42l42_dac_volume_mixer);
		snd_hda_gen_add_kctl(&spec->gen, "Mic Capture Volume",
				&cs42l42_adc_volume_mixer);
		if (spec->speaker_pdn_gpio > 0)
			snd_hda_gen_add_kctl(&spec->gen, "Speaker Playback Switch",
					     &cs8409_spk_sw_ctrl);
		/* Disable Unsolicited Response during boot */
		cs8409_enable_ur(codec, 0);
		snd_hda_codec_set_name(codec, "CS8409/CS42L42");
		break;
	case HDA_FIXUP_ACT_INIT:
		cs8409_cs42l42_hw_init(codec);
		spec->init_done = 1;
		if (spec->init_done && spec->build_ctrl_done
			&& !spec->scodecs[CS8409_CODEC0]->hp_jack_in)
			cs42l42_run_jack_detect(spec->scodecs[CS8409_CODEC0]);
		break;
	case HDA_FIXUP_ACT_BUILD:
		spec->build_ctrl_done = 1;
		/* Run jack auto detect first time on boot
		 * after controls have been added, to check if jack has
		 * been already plugged in.
		 * Run immediately after init.
		 */
		if (spec->init_done && spec->build_ctrl_done
			&& !spec->scodecs[CS8409_CODEC0]->hp_jack_in)
			cs42l42_run_jack_detect(spec->scodecs[CS8409_CODEC0]);
		break;
	default:
		break;
	}
}

static int cs8409_comp_bind(struct device *dev)
{
	struct hda_codec *codec = dev_to_hda_codec(dev);
	struct cs8409_spec *spec = codec->spec;

	return hda_component_manager_bind(codec, &spec->comps);
}

static void cs8409_comp_unbind(struct device *dev)
{
	struct hda_codec *codec = dev_to_hda_codec(dev);
	struct cs8409_spec *spec = codec->spec;

	hda_component_manager_unbind(codec, &spec->comps);
}

static const struct component_master_ops cs8409_comp_master_ops = {
	.bind = cs8409_comp_bind,
	.unbind = cs8409_comp_unbind,
};

static void cs8409_comp_playback_hook(struct hda_pcm_stream *hinfo, struct hda_codec *codec,
				      struct snd_pcm_substream *sub, int action)
{
	struct cs8409_spec *spec = codec->spec;

	hda_component_manager_playback_hook(&spec->comps, action);
}

static void cs8409_cdb35l56_four_hw_init(struct hda_codec *codec)
{
	const struct cs8409_cir_param *seq = cs8409_cdb35l56_four_hw_cfg;

	for (; seq->nid; seq++)
		cs8409_vendor_coef_set(codec, seq->cir, seq->coeff);
}

static int cs8409_spk_sw_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs8409_spec *spec = codec->spec;

	ucontrol->value.integer.value[0] = !spec->speaker_muted;

	return 0;
}

static int cs8409_spk_sw_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs8409_spec *spec = codec->spec;
	bool muted = !ucontrol->value.integer.value[0];

	if (muted == spec->speaker_muted)
		return 0;

	spec->speaker_muted = muted;

	return 1;
}

static const struct snd_kcontrol_new cs8409_spk_sw_component_ctrl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = snd_ctl_boolean_mono_info,
	.get = cs8409_spk_sw_get,
	.put = cs8409_spk_sw_put,
};

void cs8409_cdb35l56_four_autodet_fixup(struct hda_codec *codec,
				  const struct hda_fixup *fix,
				  int action)
{
	struct device *dev = hda_codec_dev(codec);
	struct cs8409_spec *spec = codec->spec;
	struct acpi_device *adev;
	const char *bus = NULL;
	static const struct {
		const char *hid;
		const char *name;
	} acpi_ids[] = {{ "CSC3554", "cs35l54-hda" },
			{ "CSC3556", "cs35l56-hda" },
			{ "CSC3557", "cs35l57-hda" }};
	char *match;
	int i, count = 0, count_devindex = 0;
	int ret;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE: {
		for (i = 0; i < ARRAY_SIZE(acpi_ids); ++i) {
			adev = acpi_dev_get_first_match_dev(acpi_ids[i].hid, NULL, -1);
			if (adev)
				break;
		}
		if (!adev) {
			dev_err(dev, "Failed to find ACPI entry for a Cirrus Amp\n");
			return;
		}

		count = i2c_acpi_client_count(adev);
		if (count > 0) {
			bus = "i2c";
		} else {
			count = acpi_spi_count_resources(adev);
			if (count > 0)
				bus = "spi";
		}

		struct fwnode_handle *fwnode __free(fwnode_handle) =
			fwnode_handle_get(acpi_fwnode_handle(adev));
		acpi_dev_put(adev);

		if (!bus) {
			dev_err(dev, "Did not find any buses for %s\n", acpi_ids[i].hid);
			return;
		}

		if (!fwnode) {
			dev_err(dev, "Could not get fwnode for %s\n", acpi_ids[i].hid);
			return;
		}

		/*
		 * When available the cirrus,dev-index property is an accurate
		 * count of the amps in a system and is used in preference to
		 * the count of bus devices that can contain additional address
		 * alias entries.
		 */
		count_devindex = fwnode_property_count_u32(fwnode, "cirrus,dev-index");
		if (count_devindex > 0)
			count = count_devindex;

		match = devm_kasprintf(dev, GFP_KERNEL, "-%%s:00-%s.%%d", acpi_ids[i].name);
		if (!match)
			return;
		dev_info(dev, "Found %d %s on %s (%s)\n", count, acpi_ids[i].hid, bus, match);

		ret = hda_component_manager_init(codec, &spec->comps, count, bus,
						 acpi_ids[i].hid, match,
						 &cs8409_comp_master_ops);
		if (ret)
			return;

		spec->gen.pcm_playback_hook = cs8409_comp_playback_hook;

		snd_hda_add_verbs(codec, cs8409_cdb35l56_four_init_verbs);
		snd_hda_sequence_write(codec, cs8409_cdb35l56_four_init_verbs);
		break;
	}
	case HDA_FIXUP_ACT_PROBE:
		spec->speaker_muted = 0; /* speakers begin enabled */
		snd_hda_gen_add_kctl(&spec->gen, "Speaker Playback Switch",
				     &cs8409_spk_sw_component_ctrl);
		spec->gen.stream_analog_playback = &cs42l42_48k_pcm_analog_playback;
		snd_hda_codec_set_name(codec, "CS8409/CS35L56");
		break;
	case HDA_FIXUP_ACT_INIT:
		cs8409_cdb35l56_four_hw_init(codec);
		break;
	case HDA_FIXUP_ACT_FREE:
		hda_component_manager_free(&spec->comps, &cs8409_comp_master_ops);
		break;
	}
}

/******************************************************************************
 *                          Dolphin Specific Functions
 *                               CS8409/ 2 X CS42L42
 ******************************************************************************/

/*
 * In the case of CS8409 we do not have unsolicited events when
 * hs mic and hp are connected. Companion codec CS42L42 will
 * generate interrupt via irq_mask to notify jack events. We have to overwrite
 * generic snd_hda_jack_unsol_event(), read CS42L42 jack detect status registers
 * and then notify status via generic snd_hda_jack_unsol_event() call.
 */
static void dolphin_jack_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42;
	struct hda_jack_tbl *jk;

	cs42l42 = spec->scodecs[CS8409_CODEC0];
	if (!cs42l42->suspended && (~res & cs42l42->irq_mask) &&
	    cs42l42_jack_unsol_event(cs42l42)) {
		jk = snd_hda_jack_tbl_get_mst(codec, DOLPHIN_HP_PIN_NID, 0);
		if (jk)
			snd_hda_jack_unsol_event(codec,
						 (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
						  AC_UNSOL_RES_TAG);

		jk = snd_hda_jack_tbl_get_mst(codec, DOLPHIN_AMIC_PIN_NID, 0);
		if (jk)
			snd_hda_jack_unsol_event(codec,
						 (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
						  AC_UNSOL_RES_TAG);
	}

	cs42l42 = spec->scodecs[CS8409_CODEC1];
	if (!cs42l42->suspended && (~res & cs42l42->irq_mask) &&
	    cs42l42_jack_unsol_event(cs42l42)) {
		jk = snd_hda_jack_tbl_get_mst(codec, DOLPHIN_LO_PIN_NID, 0);
		if (jk)
			snd_hda_jack_unsol_event(codec,
						 (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
						  AC_UNSOL_RES_TAG);
	}
}

/* Vendor specific HW configuration
 * PLL, ASP, I2C, SPI, GPIOs, DMIC etc...
 */
static void dolphin_hw_init(struct hda_codec *codec)
{
	const struct cs8409_cir_param *seq = dolphin_hw_cfg;
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42;
	int i;

	if (spec->gpio_mask) {
		snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_MASK,
				    spec->gpio_mask);
		snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DIRECTION,
				    spec->gpio_dir);
		snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DATA,
				    spec->gpio_data);
	}

	for (; seq->nid; seq++)
		cs8409_vendor_coef_set(codec, seq->cir, seq->coeff);

	for (i = 0; i < spec->num_scodecs; i++) {
		cs42l42 = spec->scodecs[i];
		cs42l42_resume(cs42l42);
	}

	/* Enable Unsolicited Response */
	cs8409_enable_ur(codec, 1);
}

static int dolphin_exec_verb(struct hdac_device *dev, unsigned int cmd, unsigned int flags,
			     unsigned int *res)
{
	struct hda_codec *codec = container_of(dev, struct hda_codec, core);
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42 = spec->scodecs[CS8409_CODEC0];

	unsigned int nid = ((cmd >> 20) & 0x07f);
	unsigned int verb = ((cmd >> 8) & 0x0fff);

	/* CS8409 pins have no AC_PINSENSE_PRESENCE
	 * capabilities. We have to intercept calls for CS42L42 pins
	 * and return correct pin sense values for read_pin_sense() call from
	 * hda_jack based on CS42L42 jack detect status.
	 */
	switch (nid) {
	case DOLPHIN_HP_PIN_NID:
	case DOLPHIN_LO_PIN_NID:
		if (nid == DOLPHIN_LO_PIN_NID)
			cs42l42 = spec->scodecs[CS8409_CODEC1];
		if (verb == AC_VERB_GET_PIN_SENSE) {
			*res = (cs42l42->hp_jack_in) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
		break;
	case DOLPHIN_AMIC_PIN_NID:
		if (verb == AC_VERB_GET_PIN_SENSE) {
			*res = (cs42l42->mic_jack_in) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
		break;
	default:
		break;
	}

	return spec->exec_verb(dev, cmd, flags, res);
}

void dolphin_fixups(struct hda_codec *codec, const struct hda_fixup *fix, int action)
{
	struct cs8409_spec *spec = codec->spec;
	struct snd_kcontrol_new *kctrl;
	int i;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_add_verbs(codec, dolphin_init_verbs);
		/* verb exec op override */
		spec->exec_verb = codec->core.exec_verb;
		codec->core.exec_verb = dolphin_exec_verb;

		spec->scodecs[CS8409_CODEC0] = &dolphin_cs42l42_0;
		spec->scodecs[CS8409_CODEC0]->codec = codec;
		spec->scodecs[CS8409_CODEC1] = &dolphin_cs42l42_1;
		spec->scodecs[CS8409_CODEC1]->codec = codec;
		spec->num_scodecs = 2;
		spec->gen.suppress_vmaster = 1;

		spec->unsol_event = dolphin_jack_unsol_event;

		/* GPIO 1,5 out, 0,4 in */
		spec->gpio_dir = spec->scodecs[CS8409_CODEC0]->reset_gpio |
				 spec->scodecs[CS8409_CODEC1]->reset_gpio;
		spec->gpio_data = 0;
		spec->gpio_mask = 0x03f;

		/* Basic initial sequence for specific hw configuration */
		snd_hda_sequence_write(codec, dolphin_init_verbs);

		snd_hda_jack_add_kctl(codec, DOLPHIN_LO_PIN_NID, "Line Out", true,
				      SND_JACK_HEADPHONE, NULL);

		snd_hda_jack_add_kctl(codec, DOLPHIN_AMIC_PIN_NID, "Microphone", true,
				      SND_JACK_MICROPHONE, NULL);

		cs8409_fix_caps(codec, DOLPHIN_HP_PIN_NID);
		cs8409_fix_caps(codec, DOLPHIN_LO_PIN_NID);
		cs8409_fix_caps(codec, DOLPHIN_AMIC_PIN_NID);

		spec->scodecs[CS8409_CODEC0]->full_scale_vol = CS42L42_FULL_SCALE_VOL_MINUS6DB;
		spec->scodecs[CS8409_CODEC1]->full_scale_vol = CS42L42_FULL_SCALE_VOL_MINUS6DB;

		break;
	case HDA_FIXUP_ACT_PROBE:
		/* Fix Sample Rate to 48kHz */
		spec->gen.stream_analog_playback = &cs42l42_48k_pcm_analog_playback;
		spec->gen.stream_analog_capture = &cs42l42_48k_pcm_analog_capture;
		/* add hooks */
		spec->gen.pcm_playback_hook = cs42l42_playback_pcm_hook;
		spec->gen.pcm_capture_hook = cs42l42_capture_pcm_hook;
		snd_hda_gen_add_kctl(&spec->gen, "Headphone Playback Volume",
				     &cs42l42_dac_volume_mixer);
		snd_hda_gen_add_kctl(&spec->gen, "Mic Capture Volume", &cs42l42_adc_volume_mixer);
		kctrl = snd_hda_gen_add_kctl(&spec->gen, "Line Out Playback Volume",
					     &cs42l42_dac_volume_mixer);
		/* Update Line Out kcontrol template */
		if (kctrl)
			kctrl->private_value = HDA_COMPOSE_AMP_VAL_OFS(DOLPHIN_HP_PIN_NID, 3, CS8409_CODEC1,
					       HDA_OUTPUT, CS42L42_VOL_DAC) | HDA_AMP_VAL_MIN_MUTE;
		cs8409_enable_ur(codec, 0);
		snd_hda_codec_set_name(codec, "CS8409/CS42L42");
		break;
	case HDA_FIXUP_ACT_INIT:
		dolphin_hw_init(codec);
		spec->init_done = 1;
		if (spec->init_done && spec->build_ctrl_done) {
			for (i = 0; i < spec->num_scodecs; i++) {
				if (!spec->scodecs[i]->hp_jack_in)
					cs42l42_run_jack_detect(spec->scodecs[i]);
			}
		}
		break;
	case HDA_FIXUP_ACT_BUILD:
		spec->build_ctrl_done = 1;
		/* Run jack auto detect first time on boot
		 * after controls have been added, to check if jack has
		 * been already plugged in.
		 * Run immediately after init.
		 */
		if (spec->init_done && spec->build_ctrl_done) {
			for (i = 0; i < spec->num_scodecs; i++) {
				if (!spec->scodecs[i]->hp_jack_in)
					cs42l42_run_jack_detect(spec->scodecs[i]);
			}
		}
		break;
	default:
		break;
	}
}

static int cs8409_probe(struct hda_codec *codec, const struct hda_device_id *id)
{
	int err;

	if (!cs8409_alloc_spec(codec))
		return -ENOMEM;

	snd_hda_pick_fixup(codec, cs8409_models, cs8409_fixup_tbl, cs8409_fixups);

	codec_dbg(codec, "Picked ID=%d, VID=%08x, DEV=%08x\n", codec->fixup_id,
			 codec->bus->pci->subsystem_vendor,
			 codec->bus->pci->subsystem_device);

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = cs8409_parse_auto_config(codec);
	if (err < 0) {
		cs8409_remove(codec);
		return err;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);
	return 0;
}

static const struct hda_codec_ops cs8409_codec_ops = {
	.probe = cs8409_probe,
	.remove = cs8409_remove,
	.build_controls = cs8409_build_controls,
	.build_pcms = cs8409_build_pcms,
	.init = cs8409_init,
	.unsol_event = cs8409_unsol_event,
	.suspend = cs8409_cs42l42_suspend,
	.stream_pm = snd_hda_gen_stream_pm,
};

static const struct hda_device_id snd_hda_id_cs8409[] = {
	HDA_CODEC_ID(0x10138409, "CS8409"),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_cs8409);

static struct hda_codec_driver cs8409_driver = {
	.id = snd_hda_id_cs8409,
	.ops = &cs8409_codec_ops,
};
module_hda_codec_driver(cs8409_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cirrus Logic HDA bridge");
MODULE_IMPORT_NS("SND_HDA_SCODEC_COMPONENT");
