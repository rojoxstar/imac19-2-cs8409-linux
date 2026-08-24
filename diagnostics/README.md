# Diagnostics

This directory preserves the diagnostic methodology separately from the V10
production build.

- `v11-s3-logging.patch` applies diagnostic-only V11 logging to the V10 source.
- `S3-TRACE-SUMMARY.md` records the sanitized controller/DMA result and
  interpretation boundary.

V11 logs software PCM parameters plus values already obtained by the normal
V10 PREPARE sequence. Its reviewed invariant was no added HDA verb,
coefficient access, I2C/GPIO transaction, delay, PM callback, state transition,
or return-value change.

The patch is not applied by `scripts/build-v10.sh`. Do not deploy it as the
daily-driver release and do not use it as a basis for a speculative write.
