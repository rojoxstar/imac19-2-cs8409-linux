# Known limitations

## Release blockers and boundaries

- System S3 suspend/resume and hibernate are unsupported. Playback can be
  totally silent after resume while DMA advances.
- Only ordinary internal-speaker playback on one exact iMac19,2 and kernel is
  release-qualified.
- 44.1 kHz is the only exposed/tested playback rate.
- Capture is source-hardened but not equivalently release-qualified.
- No DSP or EasyEffects configuration is part of this driver release.

## Evidence limitations

- Exact TAS5764L `reg08` bit semantics are unknown.
- Electrical state after an arbitrary partial TAS failure cannot be proven.
- Two side-device identities/register semantics remain incomplete; their
  known-good operations are retained and failures remain fatal.
- CS8409 vendor-coefficient retention across system S3 is not fully
  documented.
- A successful I2C acknowledgement validates transport completion only.

## Operational mitigation

Prevent all automatic and manual system suspend/hibernate paths while using
V10. Screen blanking can remain enabled. This avoids the known trigger; it is
not a driver fix.
