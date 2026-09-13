# Final local driver review — Phase 21

Date: 2026-08-25 (America/Santiago)

Branch: `codex/local-imac-audio-refinement` (local only; never pushed)

## Executive decision

The strongest locally observed candidate is the D12 existing-read diagnostic,
built from frozen V10. D12 adds an exact-iMac19,2 information log after the
normal coefficient-0 I2C-clock disable RMW. It does not add, remove, or reorder
any HDA, coefficient, I2C, GPIO, TAS, CS42, delay, PCM, or PM operation.

One controlled D12 S3 cycle resumed with audible speaker output. The final
post-S3 test was reported as fully audible, centered, clear, and without
distortion. This is better than the previously reproduced V10/V11
post-S3 total silence, but it is not yet a causal S3 fix: coefficient 0 was
identical before and after S3, and D12's post-RMW logging can perturb software
timing. A single deliberately bounded cycle cannot establish repeatability.

Accordingly:

- D12 is left installed and running as the best local diagnostic candidate.
- Frozen V10 remains the production/recovery baseline.
- System S3 remains unsupported and all sleep/hibernate targets remain masked.
- No DEV_CFG1, TAS, GPIO, CS42, gain, rate, or other hardware change is
  authorized by this observation.

## A. Starting baseline

- Machine: Apple iMac19,2, 2019 21.5-inch Retina 4K.
- Subsystem: `106b:0f00`.
- Kernel: `7.0.0-30-generic`.
- Frozen V10 disk/live SHA256:
  `8e751170a1006e682a12872464dde89bd1bea08936f48d5165b96f27a241d9b3`.
- Frozen V10 srcversion: `984693ABD54C8FF1DE34E39`.
- Frozen V10 vermagic:
  `7.0.0-30-generic SMP preempt mod_unload modversions`.
- Frozen V10 module_layout: `0xe9196a28`.
- Frozen V10 source hashes were reverified before all work:
  - `driver/source/cs8409.c`:
    `b111af117b6cbe87a7a6328971a3b733e8e13dbd2a481bf5ea034503b0d5517a`;
  - `driver/source/cs8409.h`:
    `6ce59ec45fb6c35735914a545ec703cbc65464c8163f023bcdd8965ec83a1b65`;
  - `driver/source/cs8409-tables.c`:
    `9b935dc45703e23164c5b64750d750dad1197e4987ae3bb2be0e29d921354965`.
- The independent immutable V10 backup was verified before installation and
  was never overwritten.

## B. Important hypotheses tested

### DEV_CFG1 / coefficient 0 retention

The approved D12 design exposes the value already read by
`cs8409_disable_i2c_clock()`; it performs no extra read or write.

Observed before S3 during known-audible operation:

```text
iMac D12 diag: disable-path coef0 old=0x00009008 new=0x00009000
```

Observed after S3 during successful resume INIT and subsequent PCM activity:

```text
iMac D12 diag: disable-path coef0 old=0x00009008 new=0x00009000
```

Some nested/repeated disable boundaries observed `0x9000 -> 0x9000`; this is
consistent with bit 3 already being clear. The meaningful non-bit-3 state was
`0x9000` on both sides of S3, while the normal enable path sets bit 3 and the
normal disable path clears it.

Conclusion: the proposed coefficient-0 retention loss was not observed at the
disable-RMW boundaries captured by D12. This does not exclude an earlier
transient loss during resume, but it rejects a blind `0xb008`/`0x9008` restore
experiment from the evidence currently available.

### Controller/DMA

Prior V11 tracing already proved trigger START, HDA stream start, sustained
position progress at the expected byte rate, normal wrap, and STOP during the
silent failure. D12 added no controller instrumentation and did not reopen this
disproven controller-level hypothesis.

### Downstream common-state/timing hypothesis

The earlier all-four TAS reg08 `0x10 -> 0x18` differential remains evidence of
a common downstream state change. Exact TAS5764L semantics remain unavailable.
D12 did not add TAS reads, writes, or validation, so the successful D12 cycle
does not identify that state or prove a TAS fix.

The only credible D12-specific influence on the surprising successful S3 cycle
is software/logging latency after a completed coefficient RMW and after mutex
release. Pre-existing run-to-run variability independent of D12 is equally
possible. Either is consistent with a timing-sensitive restore race, but
neither is proof.

## C. Candidates attempted

### D12 existing-read coefficient-0 observer

Hypothesis: observe the coefficient-0 value already obtained by the normal
disable-path RMW without changing hardware traffic.

Source delta:

- capture `coef_old` from the existing `cs8409_vendor_coef_get()`;
- calculate and pass the same masked value to the existing SET;
- retain whether that RMW actually ran;
- after releasing `i2c_mux`, log old/new values only for
  `CS8409_FIXUP_IMAC19_2`.

No other driver candidate was created. No speculative restore or gain candidate
was attempted.

## D. Regressions and anomalies encountered

- The first post-S3 `pw-play` invocation at 20% remained attached to the tool
  session for over 40 seconds. It was terminated once, without retry. Kernel
  logs show the PCM hardware lifecycle itself cleaned up after roughly five
  seconds. Subsequent notification audio and the final bounded test were fully
  audible. This is recorded as a client/orchestration anomaly, not hidden as a
  pass.
- The normal CS42 first readiness pass continued to return `-EIO` and recovered
  on the required final pass. No final required initialization failure occurred.
- The known `out of range cmd 0:1:7f0:3000b7` message remained unchanged.
- The unsigned out-of-tree module continued to taint the kernel as expected.
- Unrelated Bluetooth, Wi-Fi, ACPI, and KHO messages were not attributed to the
  audio driver.

## E. Measurements obtained

- D12 pre-S3 coefficient-0 RMW: `0x9008 -> 0x9000`.
- D12 post-S3 coefficient-0 RMW: `0x9008 -> 0x9000`.
- D12 resume full INIT: completed.
- TAS boot/channel setup: completed.
- CS42 first pass: transient `-EIO`; final pass recovered.
- V10 init-error latch: no fatal result.
- PREPARE: completed; CS42 buffers ON.
- CLEANUP: completed; CS42 buffers OFF.
- CLOSE: completed; speaker converter clear remained intentionally skipped.
- All five sleep/hibernate targets were re-masked immediately after the single
  controlled S3 cycle.

## F. D12 baseline and post-S3 result

Pre-S3 D12, 44.1-kHz pink-noise test:

- audible;
- deliberately quiet at 20%;
- both sides present and centered;
- normal, clean sound without reported crackles or distortion;
- no required kernel error.

Post-S3 observations:

- a desktop volume-change notification was audible after the human raised the
  volume manually; this notification had been silent after the prior V10/V11
  failure;
- the final two-second 44.1-kHz pink-noise test at 35% was fully audible;
- image was centered;
- sound was clear;
- no distortion was reported.

Exactly one D12 S3 cycle was performed. It was not repeated merely because the
result was surprising.

## G. S3 root-cause conclusion

Coefficient-0 state loss at the observed disable-RMW boundaries is not
supported by the new direct observation. A transient earlier in resume remains
unmeasured. Because D12 has no hardware-operation delta, its one successful S3
cycle cannot be presented as a source-proven repair. The result raises the
priority of run-to-run variability or a timing/ordering race after an existing
I2C-clock disable or in the downstream ASP/TAS restore path.

Future evidence must distinguish that race without converting logging latency
into an accidental production dependency. No write-based change is justified.

## H. Volume-chain analysis

The local GNOME/PipeWire percentage mapping is cubic:

```text
effective software gain = percentage^3
```

Representative attenuation before the fixed external-amplifier headroom:

| Desktop percentage | Approximate attenuation |
|---:|---:|
| 20% | -41.94 dB |
| 15% | -49.44 dB |
| 12% | -55.25 dB |
| 10% | -60.00 dB |
| 8% | -65.81 dB |
| 5% | -78.06 dB |

The ALSA `PCM Playback Volume` range is approximately -51 to 0 dB. Below the
point where that range bottoms out, PipeWire supplies the remaining attenuation
in software. No second exposed software attenuation/control or discontinuity
was found in the inspected chain; this is not a claim about undocumented
hardware gain.

The current TAS value `0xab` adds conservative headroom historically selected
to avoid distortion without Apple's proprietary DSP. Exact TAS5764L gain
semantics remain non-authoritative. The practical silence below about 10% is
therefore best explained by the userspace cubic curve compounded with fixed
amplifier headroom, not by a kernel mute or missing speaker channel.

No amplifier gain was changed. A future usability improvement should be a
separate, unity-capped userspace curve design; it must not add gain above the
known-safe full-scale ceiling or disguise a kernel fault.

## I. Audio-quality changes

None. D12 does not alter the sample path, amplifier gain, channel selectors,
TDM framing, converter programming, CS42 state, rate, or userspace policy. It
therefore preserves V10's working acoustic behavior rather than claiming an
unearned quality enhancement.

## J. Source and binary review

The production files under `driver/source/` remain byte-for-byte frozen V10.
The branch adds:

- [the D12 external patch](../diagnostics/d12-coef0-rmw-logging.patch), SHA256
  `a6bd43649482e51e212467e8ff1e322cef9f7ebe2fd703291ba528e3a9b26b2c`;
- [the reproducible D12 builder](../scripts/build-d12-diagnostic.sh);
- [the D12 source-contract tests](../tests/test_d12_diagnostic.py);
- this final review document.

Machine-local fail-closed transaction tooling was added outside Git under a
private work directory; its user-specific path is intentionally omitted and it
is not a public or production repository change.

Source proof:

- only `cs8409_disable_i2c_clock()` changes when the patch is applied;
- exactly one existing coefficient GET and one existing coefficient SET remain;
- the exact mask `0xfffffff7` remains;
- no log occurs between GET and SET;
- the log is exact-iMac19,2 only and after mutex release;
- no PM callback, delayed-work operation, or PCM return changes.

Same-path binary comparison:

- HDA read relocations: V10 22, D12 22;
- HDA write relocations: V10 90, D12 90;
- `msleep`: V10 4, D12 4;
- `usleep_range_state`: V10 9, D12 9;
- delayed-work/cancel counts: identical;
- `.text`: +96 bytes;
- `.rodata`: +64 bytes;
- one additional `_dev_info` call;
- one extra compiled unlock site caused by control-flow splitting, while each
  runtime path still takes one lock and one unlock.

This proves hardware-operation equivalence, not literal zero wall-clock
perturbation from logging.

## K. Build and static validation

- Frozen V10 manifest verification: PASS.
- Existing offline source-contract suite: 18/18 PASS including D12 tests.
- D12-specific tests: 6/6 PASS.
- `bash -n`: PASS.
- `git diff --check`: PASS.
- `W=1` build: PASS.
- Two clean D12 builds: byte-identical.
- Same-path V10/D12 binary audit: expected logging/control-flow delta only.
- `shellcheck`: unavailable; no package was installed.
- Raw-diff `checkpatch --strict` reported only absent mail-patch description and
  Signed-off-by metadata, not a C-style defect.

## L. Transaction and rollback proof

The local V10-to-D12 transaction reused the previously hardened architecture:

- exact token;
- exact kernel, path, SHA, srcversion, vermagic, module_layout, license, size;
- exact live V10 prerequisite;
- double preflight under a kernel-specific lock;
- same-filesystem stage and atomic rename;
- exact target-kernel `depmod` and initramfs update;
- handled post-replacement recovery to exact V10;
- no automatic reboot;
- independent exact-V10 rollback.

Disposable result: 82/82 PASS, including TOCTOU, symlink/path escape, lock,
signal, wrong identity, stale state, handled recovery, recovery failure, and
historical V6/V7/V8/V9 preservation cases.

## M. Installed local candidate identity

- Path:
  `/lib/modules/7.0.0-30-generic/updates/snd-hda-codec-cs8409.ko`
- Build candidate:
  `build/d12/7.0.0-30-generic/candidate/snd-hda-codec-cs8409-d12.ko`
- SHA256:
  `c7bdf9b22b257689025113c07f6f41d42d0ae0a06af3b41436bc24f91a53bbbd`
- srcversion: `9CA970DA548E489033CA2FD`
- vermagic: `7.0.0-30-generic SMP preempt mod_unload modversions`
- module_layout: `0xe9196a28`
- license: `GPL`
- size: `1375680` bytes
- compiler: `gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0`
- local source checkpoint: `86444f4`.

## N. Acceptance matrix

- [x] boots cleanly enough for the audio candidate
- [x] exact D12 disk/live identity verified
- [x] no final required INIT failure
- [x] both speaker sides audible
- [x] centered output
- [x] no obvious distortion
- [x] no crackles
- [x] no pop heard on bounded test
- [x] repeated OPEN/PREPARE/CLEANUP/CLOSE observed
- [x] 44.1 kHz stable in bounded tests
- [x] low-volume behavior numerically characterized
- [x] ordinary usable volume left at the user's selected 56%
- [x] repeated PCM reopen and naturally occurring runtime-resume full INIT
      observed without forcing runtime PM
- [x] reboot loaded the intended candidate
- [x] S3 resume full INIT completed without a final required error
- [x] one D12 S3 cycle resumed with audible output
- [x] V10 fail-closed source behavior unchanged
- [x] no speculative TAS write
- [x] no speculative GPIO write
- [x] exact model gating preserved
- [x] rollback verified and independently staged
- [x] final source delta reviewed line by line
- [ ] repeated S3 reliability (not authorized/tested)
- [ ] causal explanation for the D12 S3 success
- [ ] shutdown/cold-boot matrix beyond the one required reboot

## O. Known limitations

- S3 remains unsupported for unattended daily use despite one successful D12
  observation.
- Exact TAS5764L reg08 semantics remain unknown.
- The source of the prior all-four-amp `0x18` state remains unresolved.
- The CS42 initial readiness pass still commonly needs its final retry.
- Only the 44.1-kHz production path is supported.
- Volume below about 10% remains impractical under the current userspace curve
  and conservative amplifier headroom.
- D12 logging is diagnostic observability, not a production-quality causal fix.

## P. Userspace changes

No persistent PipeWire, WirePlumber, ALSA, desktop-volume policy, profile, EQ,
DSP, or configuration change was made. The only final userspace state change
is the session sink volume manually selected by the user, currently 56%.

## Q. Exact rollback

The immutable rollback module is:

`/var/lib/imac-audio-work/module-backups/7.0.0-30-generic/v10-8e751170a1006e682a12872464dde89bd1bea08936f48d5165b96f27a241d9b3/snd-hda-codec-cs8409-v10.ko`

Verified SHA256:
`8e751170a1006e682a12872464dde89bd1bea08936f48d5165b96f27a241d9b3`.

Exact rollback command:

```bash
pkexec /path/to/private/local-d12-transaction/rollback.sh \
  --restore-exact-v10
```

The script restores V10 atomically, verifies it, runs only target-kernel
`depmod`/initramfs update, and does not reboot. A separate reboot is then
required. The repository is not needed to recover V10.

## R. Local Git state and publication boundary

Driver implementation checkpoint beyond `main` before committing this review:

```text
86444f4 diagnostics: add existing-read coefficient-0 observer
```

Before this review commit, `git diff --stat main...HEAD` reported 3 files and
414 insertions: the patch, builder, and test listed above. The final local diff
adds this document. `git diff main...HEAD`, `git diff --stat main...HEAD`, and
`git log --oneline main..HEAD` are part of the handoff state and were inspected
at closure.

No branch, commit, module, or release was pushed. `main` and `origin/main` were
not changed locally or remotely. The installed D12 module is a local diagnostic
candidate only.

## S. Recommended next decision

Keep D12 installed only under the existing masked-S3 policy while this review
is evaluated. The next meaningful engineering step is not a register write: it
is a separately authorized, passive timing/ordering observation that can test
whether the post-RMW logging delay changed an ASP/TAS restore race. If that
cannot be done without adding speculative hardware access, retain V10 as the
production release and keep S3 unsupported.
