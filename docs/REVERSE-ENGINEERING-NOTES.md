# Reverse-engineering notes

This public repository contains derived factual descriptions only. It does
not redistribute Boot Camp packages, Apple/Cirrus driver binaries, INF files,
catalogs, disk images, or disassembly dumps.

## Evidence classes

- **PROVEN**: exact Linux source, exact package-to-subsystem mapping, or a
  deterministic trace.
- **OBSERVED**: repeatable behavior on the tested machine.
- **INFERRED**: an explanation consistent with proven facts.
- **UNKNOWN**: no exact evidence.

## Exact Windows facts retained

The exact subsystem `106b:0f00` maps to Cirrus `CONF_0910`. Derived facts used
by the Linux design include the four TAS addresses, selector order, 32-clock
lane positions, GPIO roles, and the two DEV_CFG1 values described in the S3
document.

That mapping does not make the entire Windows configuration suitable for
Linux. In particular, its default speaker endpoint and clock sequence are
48 kHz, while the current Linux transport is validated at 44.1 kHz.

## Forbidden shortcuts

- Do not substitute TAS5760L/TAS57xx semantics for TAS5764L.
- Do not infer operational amplifier state from I2C ACK/readback alone.
- Do not delete unexplained writes merely because their semantics are unknown.
- Do not add retries, reset toggles, coefficient writes, or delays without
  exact-device evidence and a one-variable review.
