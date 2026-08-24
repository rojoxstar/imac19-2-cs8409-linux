# AI handoff

This document is the compact state transfer for Jules, Gemini, or another
coding agent. Treat claims by evidence level and keep hardware changes behind
separate review.

## Canonical release

**V10** is the production source in `driver/`.

```text
tested kernel:     7.0.0-30-generic
module SHA256:     8e751170a1006e682a12872464dde89bd1bea08936f48d5165b96f27a241d9b3
srcversion:        984693ABD54C8FF1DE34E39
module_layout:     0xe9196a28
```

The binary is an identity record and is not distributed. Do not make V11 the
default build and do not overwrite frozen V10 without a separately built,
reviewed, reproducible candidate.

## Supported hardware

- Apple iMac19,2, 21.5-inch 2019
- Cirrus Logic CS8409 `1013:8409`
- subsystem `106b:0f00`
- kernel `7.0.0-30-generic`
- internal speakers at 44.1 kHz using an S32_LE container

Do not broaden this scope from shared source hooks or address resemblance.

## Working features

Observed on the exact machine:

- ordinary playback is audible on both sides, centered and clean;
- no observed distortion or pause click/pop;
- runtime autosuspend/resume works with complete conservative reinitialization;
- OPEN-only probes do not activate CS42 buffers;
- PREPARE enables buffers, CLEANUP disables them, CLOSE is hardware-neutral;
- V10’s successful path matches V9 hardware behavior.

Source/offline proven:

- required iMac INIT errors are latched exactly;
- exact-iMac19,2 playback and capture OPEN/PREPARE fail closed;
- a later complete successful INIT clears the blocking state.

## Known limitations

- System S3 and hibernate are **unsupported**.
- Capture is not release-qualified.
- 48 kHz, DSP and speaker correction are outside the frozen driver.
- TAS partial-failure electrical state is an accepted evidence limitation.
- Exact identities/semantics of two side devices remain incomplete.

## S3 experiment

Pre-S3 V11 playback was clean and audible. After exactly one S3 cycle, the
same reference playback was totally silent. Resume INIT and PREPARE completed
without a required transaction error.

## Controller/DMA proof

The silent attempt produced:

```text
azx_pcm_trigger START             yes
snd_hdac_stream_start tag=1       yes
azx_get_position                  497 observations
sustained progress and wrap       yes
STOP                              yes
overrun / dropped                 0 / 0
```

Position advancement approximately matched 44.1-kHz stereo S32 throughput.
Therefore “the controller stream never ran” is ruled down. DMA progress does
not prove nonzero samples reached the converters or TAS devices.

## TAS differential

V11 reused existing normal-path readbacks and changed no hardware operation:

```text
audible pre-S3: d8/da/dc/de reg08 = 0x10
silent post-S3: d8/da/dc/de reg08 = 0x18
Linux request in both cases:       0x18
```

The all-four change is the strongest observed differential. Exact TAS5764L
meaning is unknown.

Phase 20 found a much closer public comparator than TAS5760L: TI's documented
TAS5722L package, TDM behavior, touched-register map and every boot default
match the Apple sequence. On TAS5722L, register `0x08` bit 3 is read-only
serial-audio clock-error status; it also reads high while the device is in
shutdown. This is **STRONG COMPARATIVE EVIDENCE** for a common clock-or-shutdown
state, not TAS5764L authority and not a write basis. See
[TAS5764L-RESEARCH.md](TAS5764L-RESEARCH.md).

## DEV_CFG1 evidence

Exact iMac19,2 Windows `CONF_0910` has:

```text
CS8409 coefficient 0x00 <- 0xb008
... its 48-kHz ASP/clock sequence ...
CS8409 coefficient 0x00 <- 0x9008
```

Cirrus annotations describe PLL1/PLL2/I2C enable followed by PLL2 disable.
Linux’s iMac INIT omits a complete coefficient-0x00 restore while replaying
downstream ASP/TAS state.

Precise source qualification: V10 does access coefficient 0 during ordinary
I2C clock gating. It reads/modifies only bit 3 and preserves the other bits it
finds. It therefore cannot reconstruct PLL/vendor bits that were already lost.
Do not describe this as “V10 never writes coefficient 0.”

## Why no V12 was created

The hypothesis is strong but the hardware fix gate failed:

1. the exact post-S3 DEV_CFG1 value was not observed;
2. Windows uses a different 48-kHz clock family;
3. no single full value and placement were proven safe for the current
   machine-proven 44.1-kHz sequence;
4. TAS `reg08=0x18` is correlated, not causally decoded.

Do not blindly copy the Windows values into Linux.

## Safety boundaries

- No raw HDA, coefficient, I2C, GPIO, mixer, or MMIO writes without a
  separately reviewed, exact-evidence experiment.
- Never deliberately inject TAS/CS42/side-device failures on hardware.
- Never claim S3 support from a successful resume INIT.
- Keep diagnostic logging and hardware behavior in separate candidates.
- Preserve exact error propagation and V10 fail-closed gates.
- Keep a verified exact rollback before any installation.

## Forbidden assumptions

- TAS5760L/TAS57xx documentation is not TAS5764L authority.
- Successful I2C communication does not prove operational amp state.
- DMA advancement does not prove samples reached the speaker path.
- Unknown register semantics do not imply a write is unnecessary.
- Windows 48-kHz values are not automatically compatible with Linux 44.1 kHz.
- Runtime PM success does not qualify system S3.

## Next research questions

Only after explicit authorization:

1. Log the coefficient-0 value **already read** by normal I2C-clock gating,
   before its existing bit-3 RMW; add no hardware transaction.
2. Phase-tag already-existing GPIO data/mask/direction and TAS readbacks to
   distinguish logical restore from physical clock/shutdown state.
3. Use bounded existing HDA command/response tracepoints to confirm ordinary
   converter verbs without issuing a new verb.
4. Continue searching for exact TAS5764L authority; TAS5722L remains related
   evidence only.

If those questions cannot establish one safe restore, retain V10 and keep S3
unsupported.

## Phase 20 ranking

1. **HIGH:** shared CS8409-to-amplifier serial-audio clock/state absent after
   S3; incomplete `DEV_CFG1`/PLL prerequisite restoration is the leading
   source-based mechanism.
2. **MEDIUM:** common GPIO4/SDZ is still asserted or electrically ineffective,
   despite the correct-looking CS8409 GPIO latch.
3. **LOW–MEDIUM:** converter/vendor routing or another ASP/pad prerequisite
   downstream of controller DMA.
4. **LOW:** common platform power resource or zero-valued sample payload.
5. **VERY LOW:** side devices, CS42L83 direct speaker path, controller stall,
   or four independent amplifier failures.

These bands overlap and are prioritization, not calibrated probabilities. The
complete matrices are in [RESEARCH-HYPOTHESES.md](RESEARCH-HYPOTHESES.md) and
[S3-EVIDENCE-MATRIX.md](S3-EVIDENCE-MATRIX.md). No fix is authorized.
