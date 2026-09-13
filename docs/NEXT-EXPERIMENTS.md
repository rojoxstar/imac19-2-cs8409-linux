# Next high-information experiments

> **Historical scope:** these are unexecuted S3 experiment designs from an
> earlier phase. They are not current instructions or authorization. Current
> work is silent/offline and is indexed in
> [`apple-dsp/PHASE-INDEX.md`](apple-dsp/PHASE-INDEX.md).

No experiment in this file is authorized. Each physical run needs a separate
review, exact rollback and explicit authorization. The design goal is maximum
information from values or events already produced by normal execution, with
at most one S3 cycle per authorized experiment.

## Priority 1 — existing-read clock-state diagnostic

### Question

Does CS8409 coefficient 0 (`DEV_CFG1`) differ between a known-good boot/runtime
epoch and the post-S3 silent epoch?

### Why this is the best next discriminator

V10 already reads coefficient 0 inside normal I2C-clock gating and then writes
the same value with only bit 3 changed. Exposing that already-obtained value
requires diagnostic software flow only:

```text
old = existing cs8409_vendor_coef_get(codec, 0x00)
new = old with existing bit-3 change
existing cs8409_vendor_coef_set(codec, 0x00, new)
diagnostic log: phase, old, new
```

There must be no second coefficient selection, read or write. The hardware
operation count, order, timing, return values and I2C-clock behavior must remain
identical to V10.

### Companion observations already available

The same diagnostic candidate can phase-tag values already read by normal
execution:

- GPIO data, mask and direction from `imac_gpio_reset()`;
- TAS registers already read at boot/channel/PREPARE, especially `0x08`;
- PCM parameters and stream tag/format already passed to PREPARE;
- coefficient `0x01` only on a path that already reads it;
- side-device readbacks already produced by their required sequence.

Do not add a GPIO, TAS, side-device, coefficient or converter read.

### One-cycle procedure, for later review

1. Build from frozen production V10, not V11, adding diagnostic observability
   only.
2. Prove transaction-count and success-path hardware equivalence offline.
3. Create fail-closed install/rollback tooling with exact V10 as rollback.
4. Separately authorize installation and reboot.
5. Obtain one known-good 44.1-kHz, 20%, 10–15-second baseline; collect the
   already-existing readbacks.
6. Stop audio and verify the silent pre-suspend gate.
7. STOP for separate authorization.
8. Perform exactly one S3 cycle.
9. On resume, run only the silent verifier and collect INIT/I2C-gating logs.
10. STOP for human review.
11. Only if authorized, run one 10–15-second playback and collect the same
    bounded diagnostics.
12. Stop; do not loop or retry.

### Decision matrix

| Coef0 pre/post | GPIO logical state | TAS `reg08` | Interpretation effect |
|---|---|---|---|
| differs in non-bit-3 PLL/power-like bits | same | `0x10 -> 0x18` | strongly elevates incomplete `DEV_CFG1` restoration; still needs 44.1-kHz semantics before a write |
| same complete value | same | `0x10 -> 0x18` | rules down coef0 retention; elevates physical ASP pad/clock, SDZ or power rail |
| same coef0 | GPIO register differs | `0x18` | elevates logical GPIO restoration defect; exact polarity still required |
| same visible state everywhere | same | `0x18` | software readbacks exhausted; passive electrical clock/SDZ observation becomes justified next |
| no reproducible silence | any | any | do not infer recovery; preserve logs and stop |

## Priority 2 — bounded existing HDA command/response trace

### Question

Does PREPARE actually submit and receive ordinary converter stream/format verbs
for nodes `0x02` and `0x03` after S3?

Linux 7.0 provides existing tracepoints:

| Tracepoint | Source | What it proves | Limitation |
|---|---|---|---|
| `hda:hda_send_cmd` | [`sound/hda/core/trace.h`](https://github.com/torvalds/linux/blob/v7.0/sound/hda/core/trace.h) | packed HDA command was submitted by normal code | not successful application |
| `hda:hda_get_response` | same | a response was returned | pairing/semantics require offline decoding |
| `hda_controller:azx_pcm_prepare` | [`controller_trace.h`](https://github.com/torvalds/linux/blob/v7.0/sound/hda/common/controller_trace.h) | controller PREPARE entry and stream tag | not successful converter programming |
| `hda_controller:azx_pcm_trigger` | same | exact START/STOP request | not downstream delivery |
| `hda:snd_hdac_stream_start` | core trace | core start path entered | emitted before RUN write |
| `hda_controller:azx_get_position` | controller trace | controller position returned/advanced | not sample content or ASP output |

Only the first two add information not already obtained in the valid DMA
trace. They are passive events emitted around commands normal code already
sends. They do not create a verb.

Safety constraints for a future capture:

- verify exact tracepoint directories before any tracefs write;
- save prior tracer/event state and fail closed if an unexpected broad tracer
  is active;
- use `nop`, a bounded buffer and only the selected events;
- never enable global `events/enable`, function tracing, dynamic debug,
  kprobes, eBPF or MMIO inspection;
- enable only around a single short PREPARE/playback window, not across S3;
- disable immediately, dump once, restore prior state, stop on an empty or
  ambiguous trace;
- filter/decode only converter nodes `0x02`/`0x03` offline;
- do not publish process names or raw personal logs.

This can rule down a missing ordinary converter command. It cannot prove that
the converter accepted the route functionally or that data crossed ASP.

## Priority 3 — software-side nonzero-content evidence

DMA advancement does not prove sample contents. A narrowly scoped userspace
or ALSA software diagnostic could establish that the reference stream contains
nonzero frames before entering the kernel, without codec access. Candidate
evidence should be taken from the known input file or an already-existing
PipeWire graph monitor; it must not open a second PCM or change the graph.

This is lower priority because a synchronous all-four amplifier-state change
does not naturally follow from digital zero, but it cleanly closes the one
remaining upstream logical gap.

## Priority 4 — bounded PM callback ordering trace

Linux 7.0 exposes `hda_intel:azx_suspend`, `hda_intel:azx_resume`,
`hda_intel:azx_runtime_suspend` and `hda_intel:azx_runtime_resume`, plus generic
`power:device_pm_callback_start/end`. A separately authorized, bounded PM-only
capture could verify callback ordering and distinguish forced runtime callbacks
from platform phases. It cannot reveal a retained coefficient, physical clock
or amplifier state.

Do not combine this lower-information trace with the first diagnostic cycle
unless a separate timing/volume review approves it. Never use function tracing
or trace arbitrary code across suspend.

## Priority 5 — sanitized ACPI table analysis

Capture exact machine ACPI tables through an ordinary metadata-only method in
a separately reviewed phase, sanitize machine identifiers, then inspect
offline for:

- HDEF/audio `_PS0`, `_PS3`, `_PR0`, `_PR3`;
- `PowerResource` dependencies;
- GPIO resources;
- PCI/audio device dependencies;
- Apple-specific methods invoked on sleep/resume.

This has medium information gain. It proves static firmware structure, not
that a method ran or restored the amplifier path.

## Priority 6 — passive physical BCLK/LRCLK and SDZ observation

If all normal software-visible states match, the highest direct information is
a qualified, high-impedance oscilloscope/logic-analyzer observation of:

- shared BCLK;
- shared LRCLK/frame sync;
- common amplifier SDZ;
- optionally aggregate FAULTZ.

Compare one working baseline with one post-S3 silent attempt. This adds no
software write and directly distinguishes clock absence from shutdown, but
board access and probing can electrically damage hardware. It is invasive,
must be designed by a qualified human, and is **not** the next default action.

## Experiments rejected now

| Experiment | Why rejected |
|---|---|
| write `DEV_CFG1=0xb008/0x9008` | exact post-S3 value and 44.1-kHz order are unproven |
| read coefficient 0 with a new helper call | selecting it is an extra hardware write; an existing normal read is available |
| rewrite/clear TAS `reg08` | exact TAS5764L access and bit semantics are unavailable |
| read TAS device ID | new I2C transaction; a match would not prove full register compatibility |
| toggle GPIO4/SDZ | polarity, electrical topology and safe timing are not exact-device proven |
| sample GPIO5 using an arbitrary verb | new hardware access and uncertain fault aggregation |
| broad HDA verb tracing across S3 | unnecessary volume/timing impact; phase-specific capture is enough |
| controller MMIO/LPIB read | DMA progress is already proven |
| repeated suspend loop | adds risk and little information |
| disable runtime PM or alter autosuspend | runtime PM works and is not the discriminating variable |

## Evidence required for fixes

### `DEV_CFG1` restore

- exact before/after value from an already-normal read;
- authoritative field/reset semantics or equivalent machine proof;
- exact iMac19,2 applicability;
- safe order around the validated 44.1-kHz divider/ASP sequence;
- one-variable candidate and exact rollback;
- one separately authorized physical validation.

### TAS change

- conclusive TAS5764L identity;
- authoritative TAS5764L register/access semantics;
- causal proof, not just `0x18` correlation;
- documented safe operation and timing.

### GPIO restore

- exact signal connection, direction and polarity;
- proof that logical or physical state is lost;
- safe sequencing relative to power/clock/amps;
- no conflict with CS42 reset or existing GPIO behavior.

### Converter/vendor-route change

- exact failed command/state isolated after S3;
- proof generic PREPARE does not already restore it;
- exact-model-only restore with no stream-ID workaround regression.

### ACPI/platform change

- exact method/resource and invocation proof;
- kernel ownership and ordering proof;
- demonstrated connection to the speaker resource;
- no workaround based solely on community sleep symptoms.

## Recommended next human decision

Review a diagnostic-only design that exposes the existing coefficient-0 RMW
value and phase-tags existing GPIO/TAS readbacks. Do not authorize a clock,
TAS or GPIO write from the current evidence.
