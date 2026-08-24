# iMac19,2 CS8409 Linux audio

Experimental Linux HDA codec work for the 2019 21.5-inch Apple iMac19,2 with
Cirrus Logic CS8409 audio (`1013:8409`, subsystem `106b:0f00`). The canonical
source in this repository is **V10**, a local daily-driver release candidate
validated on one exact machine and kernel.

This is not a general CS8409 replacement and does not claim support for other
Macs, other subsystem IDs, or other kernels.

## Status

| Item | Status |
|---|---|
| Canonical release | V10 |
| Tested kernel | `7.0.0-30-generic` |
| Tested playback | 44.1 kHz, S32_LE container, stereo |
| Ordinary playback | Audible, both speakers, centered, clean |
| Runtime autosuspend | Tested successfully |
| System S3 suspend/resume | **Unsupported: post-resume playback is silent** |
| V11 | Diagnostic-only; not the production driver |

Canonical V10 identity:

```text
module SHA256: 8e751170a1006e682a12872464dde89bd1bea08936f48d5165b96f27a241d9b3
srcversion:     984693ABD54C8FF1DE34E39
vermagic:       7.0.0-30-generic SMP preempt mod_unload modversions
module_layout:  0xe9196a28
```

The binary is deliberately not distributed. Its hash identifies the exact
locally validated build; builds in another path or toolchain may differ at the
byte level even when the source and `srcversion` match.

## What works

- ordinary internal-speaker playback on the exact iMac19,2;
- left/right presence and centered stereo image;
- 44.1-kHz playback with the proven CS8409 ASP/TDM configuration;
- OPEN-only probing without activating CS42 buffers;
- PREPARE/CLEANUP buffer state balancing;
- fail-closed PCM gates after a required iMac initialization failure;
- repeated runtime-PM full initialization on the tested system.

## Known limitations

- **Do not use system suspend, S3, or hibernate.** The reproduced failure is
  total silence after resume even though HDA DMA advances normally.
- Only one iMac19,2 / `106b:0f00` machine is validated.
- Playback is intentionally constrained to 44.1 kHz. No 48-kHz claim exists.
- Capture has not received equivalent release qualification.
- TAS5764L register semantics are incomplete; related TAS parts are not an
  authoritative substitute.
- Side-device operations are retained from machine-proven behavior even
  where exact public register semantics are unavailable.

See [Known limitations](docs/KNOWN-LIMITATIONS.md) and the
[S3 investigation](docs/S3-INVESTIGATION.md).

## Build overview

The source is in [`driver/`](driver/). On the exact tested kernel:

```sh
./scripts/verify-source.sh
./scripts/run-offline-tests.sh
./scripts/build-v10.sh
```

These commands do not install or load a module. Build output goes under the
ignored `build/` directory. See [Build and installation](docs/INSTALL.md).

## Installation overview

No general-purpose public installer is supplied. The proven private
transaction tooling was bound to an exact V9-to-V10 machine state and would be
unsafe when presented as a generic installer. Any installation must first
establish an exact external rollback backup, confirm the target kernel and
module topology, stage on the target filesystem, and stop before reboot for a
separate review.

Read [INSTALL.md](docs/INSTALL.md) before considering any deployment.

## Rollback overview

Rollback means restoring the exact module that was installed immediately
before a test—not choosing stock, V9, or another historical build by guess.
The backup must be verified independently before replacement. See
[ROLLBACK.md](docs/ROLLBACK.md).

## Safety warning

Kernel audio work can produce silence, distortion, unexpectedly high output,
or an unbootable system. Never infer safe hardware behavior from a successful
I2C acknowledgement. Do not issue raw HDA, coefficient, GPIO, or I2C writes
without exact evidence and a separately reviewed experiment.

For the current release candidate, prevent every automatic and manual system
sleep path. Screen blanking may remain enabled because it is separate from
system S3.

## Research status

The Phase 20 research set is the current unknown-state map:

- [ranked root-cause hypotheses](docs/RESEARCH-HYPOTHESES.md);
- [observation/evidence matrix](docs/S3-EVIDENCE-MATRIX.md);
- [CS8409 clock and PM research](docs/CS8409-CLOCK-RESEARCH.md);
- [TAS5764L comparative research](docs/TAS5764L-RESEARCH.md);
- [exact/derived audio topology](docs/IMAC19-2-AUDIO-TOPOLOGY.md);
- [safe next experiments](docs/NEXT-EXPERIMENTS.md);
- [ranked open questions](docs/OPEN-QUESTIONS.md).

Significant research claims use six evidence labels:

- **PROVEN** — exact source, exact package mapping, deterministic trace, or a
  recorded physical outcome;
- **STRONG EVIDENCE** — independent evidence converges but one exact semantic
  or direct measurement is missing;
- **CONSISTENT** — compatible with the facts but weakly discriminated;
- **WEAK EVIDENCE** — indirect, cross-model or community support only;
- **SPECULATIVE** — technically possible without direct support;
- **UNKNOWN** — evidence is insufficient.

V11 diagnostic work and the bounded trace methodology live under
[`diagnostics/`](diagnostics/). They are not part of the default build.

## License

The combined work is distributed under **GPL-2.0-only** because it contains
Linux source files carrying both `GPL-2.0-only` and
`GPL-2.0-or-later` SPDX identifiers. Original SPDX headers and Cirrus Logic
copyright notices are preserved. See [license provenance](docs/LICENSE-PROVENANCE.md).

## Contributions welcome

Careful source review, exact-hardware reports, reproducible offline tests, and
documentation improvements are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md)
and [AI-HANDOFF.md](docs/AI-HANDOFF.md) first. Do not broaden hardware claims
or submit speculative hardware writes.
