# CS8409 clock, coefficient and PM research

> Later evidence: the iMac19,2-only restoration candidate observed DEV_CFG1
> transitions `0→0x1000`, `0x8000→0x9000`, and `0x9000→0x9000`. Playback
> continued after the bounded resume observation. This supports restoring
> PLL1 bit 12 defensively, but does not prove necessity, sufficiency, or an
> audio-quality benefit. See [current status](CURRENT-STATUS.md).

This is the public-source state of knowledge for CS8409 coefficient 0
(`DEV_CFG1`), PLL/ASP clocking and the iMac19,2 S3 restoration gap. It does not
authorize a coefficient write.

## Executive conclusion

- The iMac19,2 INIT has a **PROVEN restoration asymmetry**: it replays known
  ASP/TDM and external-device sequences but does not reconstruct a complete
  `DEV_CFG1` value.
- A missing PLL1/serial-audio prerequisite is the strongest coherent mechanism
  for “HDA DMA advances while all four amps indicate one common changed state.”
- Actual `DEV_CFG1` loss across S3 is **UNKNOWN** because the exact value was
  not captured.
- A safe 44.1-kHz value and order are **UNKNOWN**. The Windows/upstream values
  surround materially different 48-kHz clock sequences. No V12 fix follows.

## Coefficient identity and public semantics

`CS8409_DEV_CFG1` is coefficient index `0x00` in Cirrus-authored Linux source.
The public kernel comments are the strongest available field description:

| Value/operation | Source description | Evidence |
|---|---|---|
| `0xb008` | `+PLL1/2_EN, +I2C_EN` | **STRONG EVIDENCE**, Cirrus-authored Linux comment |
| `0x9008` | `-PLL2_EN` after ASP setup | **STRONG EVIDENCE**, Cirrus-authored Linux comment |
| `value \| 0x0008` | enable I2C-clock/control path | **PROVEN** by upstream helper behavior |
| `value & ~0x0008` | delayed I2C-clock/control disable | **PROVEN** by upstream helper behavior |

The full public sequence is visible in Linux 7.0
[`cs8409-tables.c`](https://github.com/torvalds/linux/blob/v7.0/sound/hda/codecs/cirrus/cs8409-tables.c#L149-L192):

```text
DEV_CFG1  = 0xb008       enable PLL1, PLL2 and I2C per source comment
DEV_CFG2  = 0x0002
DEV_CFG3  = 0x0a80
ASP descriptors
ASP1/ASP2 clock controls select PLL1
DEV_CFG2  = 0x0062       enable ASPs
DEV_CFG1  = 0x9008       disable PLL2 per source comment
pad configuration
```

The same architectural pattern appears in upstream CS42L42, Dolphin and
CDB35L56 model tables. That is strong evidence about the CS8409 architecture,
not proof that their constants can be copied into the iMac19,2 path.

No public Cirrus CS8409 datasheet or erratum defining every `DEV_CFG1` field,
reserved bit, reset value, ordering requirement or D3 retention property was
found. The names above are therefore source-derived rather than independently
datasheet-proven.

## Exact iMac19,2 source behavior

The frozen [V10 source](../driver/source/cs8409.c) has three distinct pieces
which must not be conflated.

### `imac_enable_i2c()`

This function writes:

```text
coefficient 0x30 <- 0x9008
coefficient 0x02 <- 0x0080
coefficient 0x5b <- 0x0010
coefficient 0x30 <- 0x9000
software i2c_clck_enabled <- 1
```

The visually familiar `0x9008` is written to **coefficient `0x30`, not
coefficient `0x00`**. It is not a `DEV_CFG1` restore.

### Ordinary coefficient-0 I2C-clock RMW

The shared helper later performs an existing hardware read and writes only a
bit-3 modification:

```text
enable:  DEV_CFG1 <- DEV_CFG1 | 0x0008
disable: DEV_CFG1 <- DEV_CFG1 & ~0x0008
```

Because `imac_enable_i2c()` sets the software flag before the first helper
call, the helper can initially believe I2C is already enabled. In every case,
its read/modify/write preserves all non-bit-3 state it found. It cannot
recreate a lost PLL/power field.

It would be inaccurate to say the iMac path never accesses coefficient 0; it
does. The precise statement is: **it never publishes a complete known-good
`DEV_CFG1` value during full iMac INIT.**

### iMac ASP/TDM replay

The iMac INIT separately reissues its validated 44.1-kHz setup, including:

```text
ASP lane descriptors 0x19..0x1c = 0x0800, 0x0820, 0x0840, 0x0860
coefficient 0x03 = 0x8000
coefficient 0x04 = 0x08ff
coefficient 0x05 = 0x0001
coefficient 0x02 = 0x0280
coefficient 0x82 = 0x5400
coefficient 0x01 = 0x0220
coefficient 0x08 = 0x0042
coefficient 0x07 = 0x10ff
```

It then replays GPIO/reset, CS42L83, TAS and side-device sequences. Thus known
downstream configuration is refreshed while the upstream `DEV_CFG1`
prerequisite remains inherited.

## Clock-tree reconstruction

Public evidence supports this partial architecture:

```text
HDA controller DMA
  -> HDA converter widgets 0x02/0x03
  -> CS8409 vendor routing
  -> ASP1 serializer / TDM lanes
       clock source selected from PLL1 in upstream model comments
  -> external serial clock + frame clock + sample data
  -> four digital-input amplifiers
```

Cirrus’s public
[CDB42L42 board manual](https://statics.cirrus.com/pubs/rdDatasheet/CDB42L42_DS1083DB3.pdf)
exposes CS8409 ASP MCLK, SCLK, LRCK, SDIN and SDOUT signals. This proves the
bridge architecture, not the iMac board wiring.

Upstream table comments select PLL1 as both ASP master-clock and serial-clock
source. Enabling PLL1+PLL2 before configuration and disabling only PLL2 after
ASP enable is consistent with PLL1 being the steady source and PLL2 having a
transient/setup role. Exact PLL inputs, ratios, lock indication, pad supply and
S3 retention remain **UNKNOWN**.

It is technically possible for HDA converter/DMA machinery to advance while
the external ASP clock or pad is absent: those are different boundaries. The
S3 trace proves the former and says nothing direct about the latter.

## Complete Linux 7.0 PM path

### Codec suspend

From Linux [`sound/hda/common/codec.c`](https://github.com/torvalds/linux/blob/v7.0/sound/hda/common/codec.c):

```text
hda_codec_pm_suspend()
  -> pm_runtime_force_suspend()
     -> hda_codec_runtime_suspend()
        -> hda_call_codec_suspend()
           -> CS8409 driver .suspend
              -> disable unsolicited responses
              -> suspend registered companion codecs
              -> cancel delayed I2C work
              -> coefficient-0 bit-3 clear through existing RMW
              -> shut up pins
           -> hda_cleanup_all_streams()
           -> AFG power state D3
```

For exact iMac19,2, `num_scodecs` is zero in the upstream companion abstraction,
so the generic companion-codec loop does not explicitly reset/power down the
four TAS devices. GPIO/power-rail consequences outside the codec are unknown.

### Codec resume

```text
hda_codec_pm_resume()
  -> pm_runtime_force_resume()
     -> hda_codec_runtime_resume()
        -> hda_call_codec_resume()
           -> mark generic HDA regcache dirty
           -> AFG D0
           -> restore generic shut-up pins and init verbs
           -> no CS8409 .resume callback
              -> snd_hda_codec_init()
                 -> cs8409_init()
                    -> snd_hda_gen_init()
                    -> exact iMac HDA_FIXUP_ACT_INIT
                       -> imac_hw_init()
              -> snd_hda_regmap_sync()
```

CS8409 processing-coefficient helpers issue direct coefficient-index/process
verbs. CS8409 does not enable generic coefficient caching, and HDA processing
coefficients are treated as volatile in
[`sound/hda/core/regmap.c`](https://github.com/torvalds/linux/blob/v7.0/sound/hda/core/regmap.c).
Generic regmap synchronization therefore does not replay the CS8409 vendor
coefficient table.

### Controller path

Both runtime and system controller resume converge on
`__azx_runtime_resume()` in Linux
[`sound/hda/controllers/intel.c`](https://github.com/torvalds/linux/blob/v7.0/sound/hda/controllers/intel.c),
which reinitializes PCI/controller state and performs a full controller reset.
System sleep adds coordinated device/PCI/platform power transitions around
that path. Codec system sleep similarly forces the runtime codec callbacks
through `pm_runtime_force_suspend()` and `pm_runtime_force_resume()`.

That convergence explains why the same iMac INIT appears on runtime and system
resume. The important difference is the depth of external/platform state loss,
not a different iMac restore function.

## Generic HDA retention limits

The Intel
[High Definition Audio Specification, revision 1.0a](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf)
distinguishes ordinary HDA state from vendor processing coefficients. Its
power-state persistence table makes coefficient persistence vendor-defined;
the coefficient index resets, and a settings-reset indication can report that
some settings were lost without identifying which vendor coefficient.

Consequences:

- generic HDA cannot guarantee `DEV_CFG1` retention;
- generic pin/init/regmap restore cannot substitute for the vendor table;
- it is still **UNKNOWN** whether this exact CS8409 loses `DEV_CFG1` during the
  reproduced S3 transition.

## Runtime PM versus system S3

| Property | Runtime PM, observed working | System S3, observed silent | Interpretation |
|---|---|---|---|
| codec callback path | full INIT fallback | full INIT fallback | same restore code |
| controller reinit | full controller reset path | full controller reset plus platform transition | controller trace later proves DMA works |
| platform/PCI sleep depth | runtime power management | coordinated system/firmware S3 | deeper external loss is plausible |
| exact `DEV_CFG1` restore | absent | absent | only matters if state was lost |
| ASP/TDM replay | yes | yes | writes alone do not prove clocks |
| TAS programming | yes | yes | I2C completion does not prove serial-audio state |
| result | audible | silent | points to state retained in runtime PM but lost in S3 |

This is **STRONG EVIDENCE** for a deeper-state restoration gap, not direct
proof of coefficient-0 loss.

## 44.1 kHz versus 48 kHz

Upstream CS8409/CS42L42 exposes a fixed 48-kHz path; see Linux commit
[`fed0aaca0b0f204ca40b89b22b0e493ceb27d48e`](https://github.com/torvalds/linux/commit/fed0aaca0b0f204ca40b89b22b0e493ceb27d48e).
The exact derived Windows iMac19,2 configuration is also 48-kHz-oriented. The
frozen iMac Linux path is machine-validated at 44.1 kHz and differs materially:

| State | Linux iMac V10 44.1 kHz | Upstream/Windows 48-kHz family | Classification |
|---|---:|---:|---|
| ASP descriptor/lane plan | four 32-clock lanes | model-specific 48-kHz plan | rate/topology-specific |
| ASP1 clock control 2 | `0x08ff` | upstream `0x28ff` | rate/clock-dependent |
| ASP1 clock control 3 | `0x0001` | upstream `0x0062` | rate/clock-dependent |
| `DEV_CFG2` steady value | `0x0220` | upstream `0x0062` | topology/enable-dependent |
| `DEV_CFG1` enable stage | no complete value | `0xb008` | field roles appear broad; safe order unknown |
| `DEV_CFG1` steady stage | inherited/RMW bit 3 | `0x9008` | exact 44.1 compatibility unknown |

The apparent PLL/I2C enable fields may be sample-rate-independent, but their
ordering relative to rate-specific dividers and ASP enable is safety-relevant
and not authoritatively documented. “Same chip” is not enough to transplant
the constants.

## Relevant upstream history

- Initial CS8409 support:
  [`6cc7e93f46a5ce9f65ad3c6c6f645f1d831a8fa4`](https://github.com/torvalds/linux/commit/6cc7e93f46a5ce9f65ad3c6c6f645f1d831a8fa4)
- Driver split/refactor:
  [`8c70461bbb83cf4bec058a5da16253ec7ac3fecc`](https://github.com/torvalds/linux/commit/8c70461bbb83cf4bec058a5da16253ec7ac3fecc)
- Delayed I2C-clock RMW:
  [`647d50a0c30402d2156ca201a74d77d58c7ef5ff`](https://git.zx2c4.com/linux-dev/commit/?id=647d50a0c30402d2156ca201a74d77d58c7ef5ff)
- Prevent I2C access while suspended:
  [`a1a6c7df2b2e9e2291e4c1c621b6092b07777934`](https://github.com/torvalds/linux/commit/a1a6c7df2b2e9e2291e4c1c621b6092b07777934)
- Multiple companion codecs in suspend/resume and unsolicited handling:
  [`c076e201d5e16ffa7bcd01edc82cf5a1f9ce0721`](https://github.com/torvalds/linux/commit/c076e201d5e16ffa7bcd01edc82cf5a1f9ce0721)
- Correct CS42L42 suspend powerdown:
  [`4ff2ae3a135ffe3f849492fd59ebeda3c7d1100f`](https://github.com/torvalds/linux/commit/4ff2ae3a135ffe3f849492fd59ebeda3c7d1100f)
- Suspend pop/click handling:
  [`1a04830169d00cde48e13072a1ed2222784a958b`](https://github.com/torvalds/linux/commit/1a04830169d00cde48e13072a1ed2222784a958b)
- Resume/jack PM ordering:
  [`65cc4ad62a9ed47c0b4fcd7af667d97d7c29f19d`](https://github.com/torvalds/linux/commit/65cc4ad62a9ed47c0b4fcd7af667d97d7c29f19d)
  and [`57f234248ff925d88caedf4019ec84e6ecb83909`](https://github.com/torvalds/linux/commit/57f234248ff925d88caedf4019ec84e6ecb83909)
- CS42L42 companion PLL-settle handling:
  [`9fb9fa18fb50d1a33a1bd947681fce96fc2c8db6`](https://github.com/torvalds/linux/commit/9fb9fa18fb50d1a33a1bd947681fce96fc2c8db6)
- Reduced CS42L42 resume delay:
  [`6a7ed7ee16a963f0ca028861eca8f8b365861dd1`](https://github.com/torvalds/linux/commit/6a7ed7ee16a963f0ca028861eca8f8b365861dd1)

No public CS8409 erratum, upstream PLL/ASP-after-S3 patch, or code that saves
`DEV_CFG1` before suspend and restores the saved value was found. This is a
bounded search result, not proof that private documentation does not exist.

The public PM fixes address I2C serialization, companion powerdown, jack-event
ordering, pops/clicks and companion PLL settling. None restores CS8409
`DEV_CFG1` specifically after system S3. The companion PLL commit is about
CS42L42 and must not be cited as CS8409 PLL evidence.

## Windows/Boot Camp evidence boundary

Publication-safe derived exact-iMac facts are limited to configuration:

- `CONF_0910` contains `DEV_CFG1=0xb008`, then its ASP/clock table, then
  `DEV_CFG1=0x9008`;
- it maps GPIO4/GPIO5 to amplifier shutdown/fault roles;
- it has four-amplifier initialization data.

Static configuration does not prove that Windows runs the table on every
resume, saves a previous `DEV_CFG1`, has a distinct S3 callback, or applies the
same order after sleep. No publication-safe public control-flow proof of those
claims was found. Proprietary binaries are neither required nor included.

## Source-proven mechanism and missing proof

```text
S3 loses non-I2C DEV_CFG1/PLL prerequisite       UNKNOWN
  -> iMac INIT omits complete DEV_CFG1 restore   PROVEN
  -> helper restores/clears only bit 3           PROVEN
  -> I2C works but external ASP clock does not   CONSISTENT
  -> HDA DMA advances while amps receive no SAIF CONSISTENT
  -> all four related status bits change         PROVEN observation; meaning comparative
```

The chain is coherent and currently best-ranked. The first and fourth arrows
remain unproven, so the chain cannot authorize a fix.

## Fix evidence gate

A future `DEV_CFG1` restore needs all of:

1. exact pre-S3 and post-S3 coefficient-0 values captured without adding a
   new hardware transaction;
2. authoritative or machine-proven meaning for the changed fields;
3. exact iMac19,2 applicability;
4. a 44.1-kHz-safe value and order relative to the current clock sequence;
5. a one-variable, exact-model-only candidate;
6. offline hardware-operation and error-path equivalence outside that one
   restore;
7. a separately authorized one-cycle physical validation with exact rollback.

Until all seven exist, keep V10 frozen and S3 unsupported.
