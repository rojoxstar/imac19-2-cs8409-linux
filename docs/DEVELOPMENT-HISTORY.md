# Development history

This is a concise semantic history, not the private research workspace or its
Git history.

- **V7** established the 44.1-kHz iMac transport and four-amplifier selector
  order.
- **V8** hardened required I2C/TAS/CS42 error propagation, unsolicited-event
  safety, and capture state.
- **V9** removed the unbalanced CS42 buffer activation from PCM OPEN. OPEN-only
  probes became hardware-neutral with respect to that buffer operation.
- **V10** latched each completed exact-iMac19,2 INIT result and gated playback
  and capture OPEN/PREPARE after required initialization failure. V10 is the
  canonical release candidate.
- **V11** added diagnostic logging only, reusing existing reads to study the
  post-S3 silence. It is not a production release.
- **V12** was not created because a safe 44.1-kHz DEV_CFG1 restore value and
  ordering were not proven.

No binary or proprietary reverse-engineering artifact from the private
workspace is part of this repository.
