# Contributing

Contributions are welcome when they keep evidence and risk explicit.

Before opening a change:

1. Read `docs/AI-HANDOFF.md` and `docs/KNOWN-LIMITATIONS.md`.
2. State the exact hardware, subsystem ID, kernel, and evidence level.
3. Keep V10 reproducible and unchanged unless the change is an explicitly
   reviewed new candidate.
4. Run `scripts/verify-source.sh` and `scripts/run-offline-tests.sh`.
5. Separate diagnostic logging from behavior changes.

Hardware-affecting patches must identify every new HDA verb, coefficient,
I2C/GPIO operation, delay, error-path change, and model affected. A related
chip datasheet is not proof for TAS5764L. Successful I2C completion is not
proof that an amplifier is operational.

Do not attach proprietary Boot Camp packages, Apple/Cirrus binaries, private
logs, module binaries, machine identifiers, or credentials to issues or pull
requests.

Apple DSP research contributions must contain only clean-room authored
reports, mathematics, scripts, tests, or provenance metadata. Do not commit
Apple binaries, executable sections, raw disassembly, KernelCollections, dyld
caches, disk images, recordings, personal data, or secrets. See
[`docs/apple-dsp/CONTRIBUTING.md`](docs/apple-dsp/CONTRIBUTING.md) and the
[24G90 help request](docs/HELP-WANTED-24G90.md).

Never test or recommend speculative TAS5764L gain values. In particular,
`0xCF` is not established safe for TAS5764L merely because another TI part has
a documented mapping.
