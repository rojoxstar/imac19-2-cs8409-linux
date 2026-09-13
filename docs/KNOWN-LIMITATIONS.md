# Known limitations

## Release blockers and boundaries

- System S3 suspend/resume and hibernate remain unsupported for release use.
  One earlier cycle produced total silence while DMA advanced. Later bounded
  experiments observed DEV_CFG1 PLL1-bit restoration and continued playback,
  but did not prove the restore necessary or sufficient.
- Only ordinary internal-speaker playback on one exact iMac19,2 and kernel is
  release-qualified.
- The ALSA/HDA playback endpoint is 44.1 kHz. An optional PipeWire policy can
  select 44.1- or 48-kHz graph domains; 48-kHz material still crosses one
  48→44.1 conversion before hardware.
- Capture is source-hardened but not equivalently release-qualified.
- No DSP or EasyEffects configuration is part of this driver release.
- Reconstructed Apple speaker DSP remains offline research. Child-SRC contract
  completeness is 88%, DualBand safety-bound completeness is 74%, and
  whole-chain digital headroom remains partial.

## Evidence limitations

- Exact TAS5764L `reg08` bit semantics are unknown. TAS5722L is an unusually
  close public register/default/package comparator and defines bit 3 as
  serial-audio clock error or shutdown-time high state, but it remains
  non-authoritative for TAS5764L.
- Electrical state after an arbitrary partial TAS failure cannot be proven.
- Two side-device identities/register semantics remain incomplete; their
  known-good operations are retained and failures remain fatal.
- CS8409 vendor-coefficient retention across system S3 is not fully
  documented. The exact iMac INIT does not reconstruct a complete `DEV_CFG1`;
  its existing I2C clock helper only performs a bit-3 read/modify/write.
- A successful I2C acknowledgement validates transport completion only.

## Operational mitigation

Treat system suspend/hibernate as unqualified. Screen blanking is independent.
The PLL1 state-restoration patch is research evidence, not a qualified S3 fix.
