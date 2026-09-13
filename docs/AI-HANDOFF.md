# AI and researcher handoff

Treat evidence labels strictly. Preserve frozen source identities and require a
separate review before any hardware-facing change.

## Repository and driver state

- Historical stable public baseline: V10 in `driver/`, SHA-pinned by
  `driver/V10-SOURCE-MANIFEST.sha256`.
- Development source checkpoint: `d6e8df4beda4ae4c7e1ed95e3db314c3a19bf5e9`.
- The development checkpoint adds an external PLL1 patch; `driver/source/`
  remains frozen V10.
- Tested installed module on `7.0.0-31-generic`: SHA256
  `f791e1c497d797432449e828dc8c41155d5dc266c06e18c1c4acb854ce524d3a`,
  srcversion `4696DA5BA67978BE4BD703F`.
- Do not claim the repository reproduces that exact binary without a controlled
  same-environment build comparison.

Exact hardware scope: iMac19,2 (2019 21.5-inch), CS8409 `1013:8409`, subsystem
`106b:0f00`, four TAS5764L devices at `0xd8/0xda/0xdc/0xde`, CS42L83 at
`0x90`. Playback endpoint is 44.1 kHz, `S32_LE`, with apparent 24-bit TDM
payload in 32-bit slots.

## S3 and PLL status

One historical V11 S3 cycle produced silence despite HDA DMA progress. Later
experiments observed DEV_CFG1 restoration cases `0→0x1000`, `0x8000→0x9000`,
and stable `0x9000→0x9000`; playback continued after the observed resume.
`imac19_2_restore_pll1()` preserves every other bit and sets `BIT(12)` before
ASP/TDM replay.

Do not call S3 fixed. The restore is not proved necessary or sufficient, and
no sound-quality improvement is claimed. System S3 remains unsupported for a
release qualification.

TAS register `0x04=0xAB` must remain unchanged. Its approximately −18 dB
interpretation comes from TAS5760M analogy, not authoritative TAS5764L
documentation. Never suggest testing `0xCF`, `0xD5`, or other gain codes.

## Userspace rate policy

The optional PipeWire `allowed-rates=[44100,48000]` policy passed runtime,
digital-monitor, repeated switching, independent acoustic transition, and
reboot-persistence checks. Isolated 44.1-kHz playback uses a 44.1-kHz graph
with no SRC; isolated 48-kHz playback uses a 48-kHz graph and one 48→44.1
conversion at the hardware boundary. Mixed workloads are first-active-stream
wins. No audible improvement is proved.

## Apple DSP reconstruction

Authenticated target: Sequoia 15.6.1 build 24G90 x86_64. High-level topology,
equalization, splitter/output order, spline limiter, Loudness, Mozart control
logic, and much of DualBand/SRC are reconstructed. DualBand is an analysis and
control stage, not the physical woofer/tweeter crossover.

Frozen status:

- Child-SRC contract: 88%;
- DualBand safety bounds: 74%;
- whole-chain digital headroom: partial;
- playback protection clearance: no.

V24 proves output-lattice coverage for `R=0` or `M>0`. The unresolved domain
is `R>0 && M=0`; production reachability is unknown, not a demonstrated bug.
V30 proves the kernel-side `performClientIO` frame units and wrap split. V31
rejects AppleHDAHALPlugIn as the normal render producer.

V32–V34 establish the blocker: the corpus is Recovery-only and lacks the exact
installed 24G90 System/SSV, x86_64 dyld cache, coreaudiod, CoreAudio and
AudioToolbox runtimes, and normal userspace scheduler. Continue only when the
[24G90 artifact](HELP-WANTED-24G90.md) is authenticated.

## Non-negotiable boundaries

- no Apple proprietary binary, raw executable bytes, or raw disassembly in Git;
- no speculative CS8409/TAS/CS42/GPIO/I2C writes;
- no claimed safety from limiter-only or static-EQ reconstruction;
- no claim that production reaches or avoids the unresolved SRC domain;
- no claim that the macOS scheduler is reconstructed.

Read [current status](CURRENT-STATUS.md), [Apple DSP overview](apple-dsp/README.md),
and [phase index](apple-dsp/PHASE-INDEX.md) before continuing.
