# Architecture

## Playback path

```text
PipeWire/application
  -> ALSA PCM
  -> HDA controller stream/DMA
  -> CS8409 DAC converters 0x02/0x03
  -> CS8409 ASP1/TDM lanes at 0/32/64/96
  -> four TAS5764L devices
  -> internal transducers
```

The CS42L83 companion is configured by the iMac path but exact Windows
topology associates it primarily with headphone/headset functions. Its
successful buffer operation is not proof of speaker sample flow.

## Lifecycle

- Full codec INIT and runtime resume run the complete current iMac hardware
  initialization.
- Playback OPEN performs generic constraints, bookkeeping, and existing
  diagnostics but does not activate CS42 buffers.
- PREPARE configures converters and refreshes ASP/TDM, TAS and side devices,
  enables speaker pins, then enables CS42 buffers.
- CLEANUP disables buffers and pins while preserving the converter stream-ID
  workaround.
- CLOSE is hardware-neutral bookkeeping.

V10 publishes the final exact-iMac19,2 INIT result and gates playback and
capture OPEN/PREPARE before further work when that result is negative.

## Error boundary

Checked HDA/I2C failures propagate as exact negative errno values where the
Linux API permits. A successful I2C transaction proves bus completion, not
functional amplifier clocks, power-stage state, or audible output.

## Power management

Runtime autosuspend is operationally validated. System S3 is not: the
controller stream resumes and DMA advances, but the downstream speaker path
is silent. See `S3-INVESTIGATION.md`.
