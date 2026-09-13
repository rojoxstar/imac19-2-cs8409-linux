# Optional PipeWire 44.1/48 kHz graph policy

This is a userspace policy, independent of the kernel driver implementation.
The tested user drop-in contains:

```ini
context.properties = {
    default.clock.rate = 48000
    default.clock.allowed-rates = [ 44100 48000 ]
}
```

Controlled runtime tests established:

| Isolated source | PipeWire graph | ALSA/HDA endpoint | Rate-changing boundary |
|---|---:|---:|---|
| 44.1 kHz | 44.1 kHz | 44.1 kHz | none |
| 48 kHz | 48 kHz | 44.1 kHz | one, 48→44.1 |

The 44.1-kHz sink monitor matched the S16 source under the exact lossless
`S16 << 16` representation in S32. Repeated graph-rate switching succeeded,
and an independent phone recording found no repeatable click, pop, unexpected
gap, or channel loss. This does not prove an audible fidelity improvement.

Mixed-rate behavior was first-active-stream-wins: later streams resample into
the active graph domain. Observed graph quantum was 1024 frames at 44.1 kHz
and 2048 frames at 48 kHz. CPU differences were within experimental
variability. Persistence was validated after reboot on the tested system.
