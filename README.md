# iMac19,2 CS8409 Linux audio

Experimental Linux HDA codec work for the 2019 21.5-inch iMac19,2: Cirrus
Logic CS8409 (`1013:8409`, Apple subsystem `106b:0f00`), four TAS5764L
speaker amplifiers, and a CS42L83 companion codec. Only this exact hardware
has been validated. This is not a general CS8409 replacement.

## Current status

| Layer | Evidence-backed state |
|---|---|
| Historical public baseline | V10 source in [`driver/`](driver/); preserved and SHA-pinned |
| Local research head at publication | `d6e8df4beda4ae4c7e1ed95e3db314c3a19bf5e9`; its PLL1 change is an external [patch](patches/local-imac19-2-pll1-restore-diagnostic.patch), not applied to `driver/source/` |
| Tested local kernel | Ubuntu 26.04.1, `7.0.0-31-generic`; internal speakers audible on both sides at 44.1 kHz |
| Installed local module identity | SHA256 `f791e1c497d797432449e828dc8c41155d5dc266c06e18c1c4acb854ce524d3a`; srcversion `4696DA5BA67978BE4BD703F`; vermagic `7.0.0-31-generic SMP preempt mod_unload modversions` |
| Source/binary correspondence | The installed binary identity is verified independently; a byte-for-byte build from this published tree has **not** been demonstrated |
| System S3 | Still unqualified: a prior failure produced silence; later observations include audible resume, but the PLL1 restore is not proved necessary or sufficient |
| PipeWire | Optional, separately validated dynamic 44.1/48 kHz graph policy; hardware playback endpoint stays 44.1 kHz |
| Apple speaker DSP research | Topology substantially reconstructed; whole-chain headroom and protection remain partial; no Apple DSP is enabled here |

The driver constrains its playback PCM to 44.1 kHz in an `S32_LE` container.
The apparent ASP/TDM payload is 24 bits in 32-bit slots; exact amplifier
internal precision is not established. A native 48-kHz application may run a
48-kHz PipeWire graph, but the userspace-to-hardware path then includes a
48→44.1 conversion. See [rate-policy findings](docs/PIPEWIRE-RATE-POLICY.md).

Historical V10 local identity on kernel `7.0.0-30-generic` was SHA256
`8e751170a1006e682a12872464dde89bd1bea08936f48d5165b96f27a241d9b3`,
srcversion `984693ABD54C8FF1DE34E39`. The module is not distributed. The
source manifest and [build notes](docs/INSTALL.md) preserve this baseline.

## Hardware safety and limits

The current iMac19,2 source writes TAS register `0x04 = 0xAB` during channel
setup. A TAS5760M register analogy suggests approximately −18 dB relative to
`0xCF`, but **TAS5764L semantics are not proven by that analogy**. Never try
speculative TAS gain writes, including `0xCF`, on the speakers. The PLL1 patch
reads `DEV_CFG1`, ORs only bit 12, and writes only on change; it is a defensive
state restoration based on observed values, not an audio-quality enhancement
or a qualified S3 repair. See [current hardware status](docs/CURRENT-STATUS.md).

Capture is not equivalently release-qualified. No reconstructed Apple DSP or
speaker-protection chain is approved for live playback. The acoustic results
for the rate policy do not validate a future EQ, crossover, or limiter.

## Build and research

The frozen V10 tree is in [`driver/`](driver/). Offline checks:

```sh
./scripts/verify-source.sh
./scripts/run-offline-tests.sh
```

The existing `build-v10.sh` targets the historical kernel 7.0.0-30 and does
not reproduce the installed 7.0.0-31 binary. No module is installed or loaded
by the commands above. Read [installation cautions](docs/INSTALL.md) before
using any locally built module.

Research entry points: [Apple DSP overview](docs/apple-dsp/README.md),
[phase index](docs/apple-dsp/PHASE-INDEX.md), [current 24G90 blocker](docs/apple-dsp/CURRENT-BLOCKER.md),
and [help wanted](docs/HELP-WANTED-24G90.md). The public documents are curated
technical summaries. They include no Apple executable, raw disassembly, or
recording. The clean-room work does **not** establish a complete Apple playback
implementation or speaker safety.

Contributions and exact-hardware observations are welcome. Read
[CONTRIBUTING.md](CONTRIBUTING.md) and the [AI handoff](docs/AI-HANDOFF.md).

## License

The combined Linux driver work is distributed under **GPL-2.0-only**. Original
SPDX headers remain on copied kernel files. See
[license provenance](docs/LICENSE-PROVENANCE.md).

## Want to test this on another iMac19,2?

The repository currently does **not** provide a generic installer or prebuilt kernel module.

This is intentional: the driver is experimental, kernel-specific, and has only been validated on an exact iMac19,2 (`106b:0f00`). Installing an incompatible out-of-tree audio module can leave the system without working audio or require manual rollback.

If you have an iMac19,2 and want to help test the driver, please open a GitHub issue and include:

- Linux distribution
- `uname -r`
- iMac model identifier
- CS8409 PCI/subsystem identification
- current internal-speaker behavior
- whether you can boot a fallback kernel if necessary

I will first verify that your machine matches the validated hardware path before suggesting any installation procedure.

A fail-closed public testing/install path with automatic compatibility checks, backup, and rollback is planned.

Until then, please **do not manually replace `snd-hda-codec-cs8409.ko` using commands copied from unrelated CS8409 projects**.

I will first verify that your machine matches the validated hardware path before suggesting any installation procedure.

A fail-closed public testing/install path with automatic compatibility checks, backup and rollback is planned. Until then, please do not manually replace snd-hda-codec-cs8409.ko using commands copied from unrelated CS8409 projects.
