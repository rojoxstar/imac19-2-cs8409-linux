# D12 coefficient-0 existing-read diagnostic

Status: **offline candidate**. Not authorized for installation. Any physical
run requires the full NEXT-EXPERIMENTS.md Priority-1 procedure: offline
equivalence proof (below), fail-closed install/rollback against exact V10,
separate authorization, and exactly one S3 cycle.

## What D12 observes

D12 logs the value already produced by the normal CS8409 I2C-clock gating
read-modify-write in `cs8409_disable_i2c_clock()`. It adds no verb, read,
write, transaction, delay or callback.

The logged value is precisely: **"the earliest coefficient-0 value observed by
the existing normal I2C clock gating path after resume/INIT, before that path
modifies coefficient 0."**

It is deliberately *not* described as the raw register value at the instant of
S3 wake. No project-source coefficient-0 write occurs between resume and this
observation (`imac_enable_i2c()` writes coefficients 0x30/0x02/0x5b only;
`imac_cs8409_tdm_setup_amps12()` writes 0x01-0x05, 0x07, 0x08, 0x19-0x1c,
0x6b, 0x71, 0x82; generic HDA resume restores cached widget verbs only), but
autonomous hardware state evolution across sleep, wake and init remains an
explicit unknown. The sample is therefore a lower bound on post-resume state,
not a proof of wake-instant state.

## Why only the disable path

`imac_enable_i2c()` sets `spec->i2c_clck_enabled = 1` manually before any I2C
helper runs during INIT/resume replay. Consequently every
`cs8409_enable_i2c_clock()` call inside that sequence sees the clock already
enabled and performs no coefficient-0 access. The enable-path RMW cannot
observe anything new during INIT/resume, so instrumenting it adds no necessary
information and would only perturb delayed-work timing. `cs8409_cs42l42_suspend()`
ends with `cancel_delayed_work_sync()` plus an explicit disable, so the flag is
always 0 at resume entry and the disable transition is guaranteed to occur.

First observation timing, both epochs: the deferred
`cs8409_disable_i2c_clock_worker`, approximately 25 ms after the final INIT
I2C transaction. On cold boot this is the first D12-observed coefficient-0
read from the normal I2C clock-gating path after iMac INIT. Steady-state
cycles then alternate:
enable reads `base`, writes `base|0x8`; disable reads `base|0x8`, writes
`base`.

## Semantic transformation

Inside `cs8409_disable_i2c_clock()` only:

```text
BEFORE                                AFTER
existing coef0 GET                    old  = existing coef0 GET
existing bit-3 clear                  new  = old & 0xfffffff7   (CPU-local)
existing coef0 SET                    existing coef0 SET(new)
spec->i2c_clck_enabled = 0            spec->i2c_clck_enabled = 0
                                      model-gated codec_info() after unlock
```

Hardware sequence invariant:

```text
SET_COEF_INDEX(0) -> GET_PROC_COEF -> SET_COEF_INDEX(0) -> SET_PROC_COEF(new)
```

Nothing executes between GET_PROC_COEF and SET_COEF_INDEX(0) except the CPU-local
mask arithmetic. The masks are byte-identical to V10 (`& 0xfffffff7`,
single bit-3 clear). `cs8409_vendor_coef_get()` returning a negative error is
converted to `unsigned int` exactly as V10's implicit argument conversion did;
bit patterns and all subsequent behavior are unchanged, including error cases.

## Logging placement

The mutex guard moved from function scope into a nested lexical block. Lock
semantics relative to all hardware operations are unchanged: the identical
critical section contains the identical four verbs and flag update, and the
unlock point now occurs strictly before the only newly added statement. The
diagnostic print runs after `i2c_mux` release, gated on:

- `rmw_done` - skipped transitions log nothing (no misleading zeros when the
  clock was already disabled);
- `codec->fixup_id == CS8409_FIXUP_IMAC19_2` - exact model only; iMac18,3
  (`CS8409_FIXUP_IMAC_AMP`), Dell, Dolphin and CDB35L56 machines execute the
  identical binary hardware path as before.

Format: `codec_info()`, `"iMac D12 diag: disable-path coef0 old=0x%08x new=0x%08x\n"`.
`codec_info()` is used rather than `codec_dbg()` because dynamic debug is
typically disabled for this driver on distribution kernels and the experiment
must not lose its one sample per epoch. Volume is bounded to one line per
clock-gating transition.

## Hardware-operation equivalence

Per executed transition, before vs after: HDA verbs 4 vs 4
(SET_COEF_INDEX x2, GET_PROC_COEF x1, SET_PROC_COEF x1); coefficient data
reads 1 vs 1; coefficient data writes 1 vs 1; I2C transactions 0 vs 0 (clock
gating is HDA-side); GPIO operations 0 vs 0; explicit delays added none (the
25 ms cancel/queue lifecycle is untouched); PM callbacks unchanged. Skipped
transitions execute identically: zero verbs, zero lines.

## Interpretation guide

Compare one known-good baseline epoch with one post-S3 epoch using the
decision matrix in docs/NEXT-EXPERIMENTS.md. Because suspend's explicit
disable clears bit 3 before sleep, if coefficient 0 is retained unchanged
across S3 and no autonomous hardware transition modifies it, bit 3 is
expected to remain 0 in the first post-resume sample; retained
PLL/power-like bits with lost bit-3 state, or wholesale decayed values,
discriminate differently per that matrix. An invalid/error-derived
coefficient value would indicate failure or an invalid response in the HDA
vendor-coefficient read path at the observation point; it does not by itself
indicate an external I2C transaction failure.

This experiment cannot establish field semantics, safe restore values, or
44.1-kHz ordering. It must not motivate any DEV_CFG1 write without the full
evidence list in NEXT-EXPERIMENTS.md ("Evidence required for fixes").

## Application

Apply only to a pristine, hash-verified V10 checkout; never commit into
`driver/`. Rollback is exact reverse-patch back to frozen V10.

```sh
./scripts/verify-source.sh                       # confirm pristine V10 first
patch -p1 --dry-run < diagnostics/d12-coef0-rmw-logging.patch
patch -p1 < diagnostics/d12-coef0-rmw-logging.patch
# ... collect dmesg across the authorized single cycle ...
patch -p1 -R < diagnostics/d12-coef0-rmw-logging.patch
./scripts/verify-source.sh                       # confirm exact V10 restored
```

Note: while applied, `scripts/verify-source.sh` fails by design because the
working tree differs from the V10 manifest; that failure is the rollback tripwire.
