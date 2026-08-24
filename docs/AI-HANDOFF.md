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
meaning is unknown. Related TAS5760L clock-error documentation is
non-authoritative analogy only.

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

1. Can software-only evidence establish the exact CS8409 clock/PLL state lost
   across S3 without adding a coefficient selector/read?
2. Can an exact TAS5764L register definition establish `reg08` bit 3?
3. Can existing codec/controller tracepoints narrow converter-to-ASP delivery
   without MMIO or behavior changes?

If those questions cannot establish one safe restore, retain V10 and keep S3
unsupported.
