# Ranked open questions

This is the remaining unknown-state map after the public-source survey. A high
rank means information value, not permission to touch hardware.

## Priority 0 — blocks any S3 fix

### Q1. What complete CS8409 coefficient-0 value exists in each power epoch?

- **Known:** upstream and exact derived Windows sequences use `0xb008` then
  `0x9008`; iMac V10 does not reconstruct a complete value.
- **Unknown:** cold-working, runtime-resumed and post-S3 values before the
  existing I2C bit-3 RMW.
- **Why it matters:** this directly tests the lead PLL/power prerequisite
  hypothesis.
- **Safe evidence:** expose the value already read by normal I2C-clock gating;
  no duplicate coefficient access.

### Q2. Are shared ASP BCLK/LRCLK physically present after S3?

- **Known:** controller DMA advances; ASP coefficients are replayed; all four
  amps synchronously change state.
- **Unknown:** electrical clock/frame signals at the amplifier boundary.
- **Why it matters:** it separates an upstream clock/pad failure from an amp
  shutdown/power condition.
- **Safe evidence sequence:** exhaust existing software readbacks first; only
  then consider qualified passive measurement.

### Q3. Is amplifier SDZ physically deasserted, and is FAULTZ asserted?

- **Known:** exact derived roles map GPIO4 to SDZ and GPIO5 to aggregate
  FAULTZ; CS8409 logical GPIO data returns `0x12` pre/post.
- **Unknown:** exact polarity/electrical level/pad override and fault input.
- **Why it matters:** the strongest public related-device source says the same
  status bit reads high both on bad clocks and in shutdown.
- **Safety gate:** no GPIO toggle or arbitrary read.

## Priority 1 — resolves semantic ambiguity

### Q4. Is the exact amplifier publicly provable as TAS5764L, and what is its
register `0x08` definition?

- **Known:** component-family evidence is strong; TAS5722L is an exceptionally
  close register/default/package/TDM fingerprint.
- **Unknown:** exact marking, public device ID, TAS5764L register map and its
  relationship to TAS5722L.
- **Why it matters:** exact authority could establish whether bit 3 is clock
  error, shutdown indication or something else.
- **Current rule:** TAS5722L remains comparative only.

### Q5. Which `DEV_CFG1` fields are rate-independent and what is their safe
44.1-kHz ordering?

- **Known:** Cirrus-authored comments identify PLL1/PLL2/I2C enable roles.
- **Unknown:** full bit map, reset values, lock prerequisites, reserved bits
  and sequencing around the validated 44.1-kHz dividers.
- **Why it matters:** even proving post-S3 loss does not make the Windows
  48-kHz sequence safe.

### Q6. Does a converter/vendor command fail after S3 despite PREPARE?

- **Known:** generic suspend clears streams; generic multi-out PREPARE
  reconstructs both speaker DACs; controller DMA progresses.
- **Unknown:** functional acceptance and the converter-to-ASP vendor route.
- **Safe evidence:** bounded existing HDA command/response trace, no new verb.

### Q7. Are nonzero reference samples reaching the kernel stream?

- **Known:** the stream starts and advances at the expected rate.
- **Unknown:** payload contents.
- **Why it matters:** closes the last upstream alternative.
- **Safe evidence:** software-side reference checksum or existing graph
  instrumentation without opening a second PCM.

## Priority 2 — board/platform topology

### Q8. Does exact iMac19,2 firmware own an audio power resource?

- No exact audio `_PS0/_PS3`, power resource or SMC sequence was found.
- A sanitized ACPI table review is safe static research; execution semantics
  still need separate proof.

### Q9. Which clocks and rails are physically shared by all four amps?

- Related Apple evidence favors shared serial clock/frame/data and common
  shutdown/fault.
- Exact iMac19,2 MCLK and power-rail topology remains unknown.

### Q10. What state survives runtime D3 but not system S3?

- The driver callback path largely converges.
- Candidate domains are CS8409 vendor coefficients/PLL, pad supply, common
  GPIO electrical state and an external rail.
- A successful runtime cycle is retention evidence, not a register-level map.

### Q11. Is a cold reboot a physical amplifier/CS8409 rail reset?

- Software/controller state is reconstructed on boot.
- A physical TAS or CS8409 power cycle has not been proven.

## Priority 3 — unresolved side paths

### Q12. What is actually populated at driver address `0x64` (`0x32` 7-bit)?

- Byte provenance points to a foreign MAX98706-family helper.
- Exact iMac19,2 configuration does not identify it.
- Physical presence, class and role remain unknown.

### Q13. What is actually populated at driver address `0x28` (`0x14` 7-bit)?

- Byte provenance is an SSM3515 helper and exact SSM3515 documentation decodes
  the copied registers.
- That does not prove an SSM3515 exists on this board.

### Q14. Does CS42L83 participate indirectly in a shared clock/power domain?

- Direct speaker transport is very unlikely.
- Shared CS8409 resources make indirect interaction technically possible, but
  no positive evidence exists.

### Q15. Is GPIO5 aggregation observable through a safe existing path?

- V10 lacks a FAULTZ handler.
- Adding a new GPIO read is hardware access and needs separate review; first
  search for a value already acquired by normal code.

## Priority 4 — broader support/release questions

### Q16. Do other Apple CS8409 machines reproduce the same all-four state?

Public projects report varied post-suspend failures across MacBook/iMac models,
but companions and topology differ. No result can be transferred without exact
subsystem and device evidence.

### Q17. Does Windows use a distinct resume sequence?

Static exact configuration proves values and ordering, not invocation
frequency, PM callbacks or error recovery. No publication-safe control-flow
proof was found.

### Q18. Are there private CS8409 or TAS5764L errata?

No public exact erratum was found. Absence from public search is not evidence
that no vendor erratum exists.

## Resolved or strongly ruled-down questions

| Question | Result |
|---|---|
| Did ALSA request START? | **PROVEN YES** |
| Did HDA core start the stream path? | **PROVEN YES** |
| Did controller position advance? | **PROVEN YES**, sustained and wrapped |
| Did trace loss invalidate that result? | **PROVEN NO**, zero overrun/drop |
| Did full iMac INIT run after S3? | **PROVEN YES** |
| Did a final required I2C error occur? | **PROVEN NO** |
| Did all four amps show one common changed bit? | **PROVEN YES** |
| Is a single failed amp/selector a good explanation? | strongly ruled down |
| Does PREPARE omit generic converter restoration? | **source evidence says NO** |
| Is V11 a production fix? | **NO**, logging-only diagnostic |
| Is S3 supported? | **NO** |

## Unknowns that must not become assumptions

- TAS5764L is not automatically register-compatible with TAS5722L or TAS5760L.
- A successful I2C readback is not an amplifier-health check.
- A correct HDA GPIO latch is not proof of pad voltage.
- A complete-looking ASP coefficient sequence is not proof of PLL lock.
- Windows 48-kHz `DEV_CFG1` ordering is not a Linux 44.1-kHz recipe.
- DMA progress is not proof of nonzero sample content or speaker-path delivery.
- No public ACPI finding means “unknown,” not “firmware is irrelevant.”

## Current fix status

No fix class passes its evidence gate. The next decision should concern a
diagnostic-only existing-read capture, not V12. Frozen V10 remains the daily
baseline with system suspend and hibernate disabled/unsupported.
