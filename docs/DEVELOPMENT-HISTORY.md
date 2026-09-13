# Development history

## Current research publication

The source lineage after the public S3 research head adds the D12 existing-read
diagnostic, its final review, and an iMac19,2-only PLL1 state-restoration patch.
The current installed kernel-31 module is separately identified in
[`CURRENT-STATUS.md`](CURRENT-STATUS.md); exact byte correspondence to the
repository is not claimed. Userspace rate-policy and Apple DSP forensic work
are documented separately from the kernel driver.

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
