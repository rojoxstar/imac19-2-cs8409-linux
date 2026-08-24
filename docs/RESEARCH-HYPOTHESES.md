# S3 root-cause hypotheses

This document prioritizes explanations for the one reproduced iMac19,2 S3
failure. It is a research map, not a patch proposal. System S3 remains
unsupported and no hardware write is authorized by this ranking.

## Evidence vocabulary

- **PROVEN** — exact project/Linux source, an exact derived configuration
  record, or a deterministic trace establishes the statement.
- **STRONG EVIDENCE** — several independent observations support the statement,
  but an exact-device semantic or direct measurement is missing.
- **CONSISTENT** — the statement fits all known facts but has little positive
  discrimination.
- **WEAK EVIDENCE** — indirect, cross-model or community material only.
- **SPECULATIVE** — technically possible with no direct evidence.
- **UNKNOWN** — the required public evidence was not found.

Probability bands are qualitative priorities, not calibrated posterior
probabilities. Hypotheses overlap, so their bands must not be added:

- **HIGH**: more likely than not among the current explanations;
- **MEDIUM**: material alternative, roughly 20–50%;
- **LOW**: roughly 5–20%;
- **VERY LOW**: below roughly 5%.

## Hard constraints

The following facts dominate the ranking:

1. **PROVEN:** the same V11 candidate was audible before S3 and completely
   silent after one S3 cycle.
2. **PROVEN:** ALSA requested START, the HDA controller started stream tag 1,
   497 position observations advanced at approximately the expected byte rate,
   the ring wrapped, and STOP occurred with no trace loss.
3. **PROVEN:** all four amplifier-address readbacks changed together from
   register `0x08 = 0x10` while audible to `0x18` while silent.
4. **PROVEN:** Linux requested `0x18` in both conditions. **STRONG COMPARATIVE
   EVIDENCE:** the differing bit is read-only status on the closest publicly
   documented transaction-map match; exact TAS5764L access remains unknown.
5. **PROVEN:** normal resume INIT replayed GPIO, ASP/TDM, companion, amplifier
   and side-device operations and returned success.
6. **PROVEN:** the exact iMac path does not reconstruct a complete CS8409
   coefficient-0/`DEV_CFG1` value. Its ordinary coefficient-0 I2C-clock
   read/modify/write preserves the other bits it finds.
7. **PROVEN:** V11 added logging only and left the V10 hardware sequence
   unchanged.

## Ranked hypothesis tree

### H1 — common serial-audio clock/state is absent after S3

**Priority: HIGH. Evidence: STRONG EVIDENCE.**

The most economical explanation is that the CS8409-to-amplifier serial-audio
transport is not operational after the deeper system transition even though
the HDA controller and software-visible setup succeed.

#### H1a — incomplete `DEV_CFG1`/PLL prerequisite restoration

Upstream CS8409 model tables begin hardware initialization with
`DEV_CFG1=0xb008` (commented `+PLL1/2_EN, +I2C_EN`) and finish the clock/ASP
sequence with `DEV_CFG1=0x9008` (commented `-PLL2_EN`). Exact derived
iMac19,2 Windows `CONF_0910` evidence has the same two coefficient-0 values
around a different, 48-kHz-oriented clock sequence. The Linux iMac INIT
replays downstream ASP descriptors and clock-control coefficients but does not
perform this complete coefficient-0 sequence.

The local I2C-clock helper later reads coefficient 0 and changes only bit 3.
Consequently it cannot recreate PLL/power bits that S3 lost. Runtime PM success
does not disprove this: a shallower transition may retain state that platform
S3 loses.

This is **source-proven as a restoration asymmetry** and **strongly consistent
with the failure**, but loss of the exact coefficient value has not been
observed. Public CS8409 material does not establish a safe 44.1-kHz constant
or placement. No write is justified.

#### H1b — another ASP/pad/vendor prerequisite is missing

Coefficient-0 is not the only possible common prerequisite. ASP pad enable,
vendor routing, PLL lock ordering, or a state outside the visible HDA verb
cache could produce the same boundary failure. This remains **CONSISTENT**;
the iMac INIT does reissue its known ASP/TDM coefficients, which argues against
a simple omitted downstream descriptor.

### H2 — common amplifier shutdown/pad signal is not electrically restored

**Priority: MEDIUM. Evidence: CONSISTENT to STRONG EVIDENCE.**

Exact derived package evidence maps CS8409 GPIO4 to amplifier `SDZ` and GPIO5
to amplifier `FAULTZ`. Linux reissues GPIO direction/data/mask and reads back
`GPIO_DATA=0x12` after resume, the same value seen on the working path. That
rules down a simple software-latch omission.

It does not prove the electrical pad level, direction retention below the HDA
register interface, power-domain state, or polarity at all four devices.
Critically, the authoritative **TAS5722L comparative source** says its CLKE bit
reads high while the device is in shutdown because the clock detector is
powered down. Therefore the observed all-four `0x18` is compatible with either
missing clocks or a shared shutdown condition on that related device.

### H3 — converter-to-ASP routing/state is lost downstream of DMA

**Priority: LOW–MEDIUM. Evidence: CONSISTENT.**

DMA advancement proves controller progress, not that nonzero samples passed
through converter nodes `0x02`/`0x03`, vendor routing, or ASP pins. V11 showed
the same two-channel stream tag, HDA format and software state before and after
S3. Generic HDA resume and PREPARE restore ordinary verbs, while the iMac path
refreshes its TDM coefficients. A hidden converter/vendor route can still be
wrong, but no differentiating evidence currently points to it over the shared
clock prerequisite.

### H4 — Apple platform/ACPI power resource is not restored

**Priority: LOW. Evidence: WEAK EVIDENCE.**

A common amp rail, reference clock, CS8409 pad supply, or SMC-controlled state
could be lost only during platform S3. Public searches found no exact
iMac19,2 audio `_PS0`/`_PS3`, power-resource or SMC sequence. Community
Apple-CS8409 suspend workarounds concern broader platform/xHCI behavior and do
not prove this audio mechanism. Keep this branch open, but do not invoke
“firmware magic” as an explanation without an exact method or measurement.

### H5 — the HDA stream advances over digital zero

**Priority: LOW. Evidence: CONSISTENT but weak.**

Position progress cannot establish sample contents. A PipeWire/application
path could feed silence while START and DMA advance. Against this are the same
reference playback procedure, successful pre-S3 control, normal lifecycle
logs, and a synchronous hardware-status differential on all four amplifiers.
A software-only sample-content checksum or an existing userspace graph trace
would discriminate this without touching codec hardware.

### H6 — all TAS devices enter a common internal fault unrelated to clocks

**Priority: LOW. Evidence: WEAK EVIDENCE.**

An all-four thermal, overcurrent or DC condition is physically less economical
than a shared clock/shutdown cause. In the exact TAS5722L comparative register,
those faults occupy bits 0–2; only bit 3 differs here. This is useful related-
device evidence, not authoritative TAS5764L semantics.

### H7 — side device `0x64` or `0x28` blocks the speaker path

**Priority: VERY LOW. Evidence: UNKNOWN.**

The exact parts and register meanings remain unidentified. Their transactions
ACKed and their V11 readbacks did not differ. They could participate in a
shared clock/power path, but there is no positive evidence. Address resemblance
alone is not identification.

### H8 — CS42L83 causes all-four speaker silence

**Priority: VERY LOW. Evidence: STRONG EVIDENCE against a direct cause.**

Exact derived topology and the public CS8409/CS42 companion architecture place
the companion primarily on headset/headphone functions. The recovered initial
readiness warning was followed by a successful final pass, configuration and
buffer enable. Those operations do not prove speaker transport, but they also
do not explain the all-four amplifier differential.

### H9 — HDA controller never starts or DMA stalls

**Priority: VERY LOW. Evidence: PROVEN trace evidence against.**

The valid trace directly observed trigger START, core stream start, sustained
position movement, normal wrap and STOP. A narrower controller-to-converter
boundary issue remains possible under H3; controller non-start/stall does not.

### H10 — four independent amplifier failures

**Priority: VERY LOW. Evidence: STRONG EVIDENCE against.**

Four addresses changed on the same bit at the same transition. Independent
simultaneous failures are a much less parsimonious explanation than a shared
clock, shutdown, reset or supply dependency.

## Formal hypothesis matrix

| ID | Hypothesis | Pre-S3 success | Post-S3 silence | All-four `0x18` | DMA progress | Evidence for | Evidence against | Confidence | Safe discriminating test | Fix risk |
|---|---|---|---|---|---|---|---|---|---|---|
| H1a | CS8409 `DEV_CFG1`/PLL prerequisite lost | Initial/firmware state can be valid | Omitted full restore can leave ASP clocks absent | Related TAS5722L CLKE analogy fits | Fully consistent | Exact source gap; upstream/Windows ordering; common change | Exact post-S3 coef0 not captured; bit semantics incomplete | HIGH | Log value already read by normal coef0 RMW, before modification | Very high until 44.1-kHz order is proven |
| H1b | Other CS8409 ASP/pad/vendor state lost | Cold initialization supplies it | Hidden state absent after deeper reset | Common serial interface explains all four | Fully consistent | Common boundary; generic HDA does not save arbitrary vendor state | Known iMac ASP/TDM writes replay | MEDIUM | Phase-tag existing coefficient/readbacks; passive clock observation later | High |
| H2 | Shared SDZ/pad electrically wrong | Correct before S3 | All amps remain shutdown | Exact related device reads CLKE high in shutdown | Fully consistent | Derived GPIO4 mapping; synchronous response | GPIO latch/direction/data rewritten and data matches | MEDIUM | Log existing GPIO data/mask/direction reads; later qualified pad measurement | High without polarity/electrical proof |
| H3 | Converter/vendor route to ASP lost | Correct route before S3 | DMA terminates before ASP | Could remove common data/clocks | Fully consistent | DMA proves only controller side | Normal tag/format and known TDM restore match | LOW–MEDIUM | Software-only converter/cache/normal-read trace | Medium |
| H4 | Common platform rail/reference absent | Firmware supplies on boot | S3 platform transition fails restore | All four share dependency | Fully consistent | Runtime-vs-S3 distinction | No exact ACPI/SMC evidence | LOW | Public ACPI dump review; qualified passive rail/clock measurement | Very high |
| H5 | Digital-zero userspace payload | Application feeds audio pre-S3 | Stream contains zeros post-S3 | Does not naturally explain hardware delta | Fully consistent | DMA cannot prove contents | Same reference path; synchronous TAS change | LOW | Software-side payload/graph trace only | Low for diagnostics, not a driver fix |
| H6 | Common internal amplifier fault | No fault before S3 | All power stages sleep | Directly explains status change if compatible | Fully consistent | `0x08` is fault/status on exact TAS5722L | Exact TAS5764L semantics missing; bits 0–2 unchanged | LOW | Existing readbacks plus authoritative exact-part documentation | Extreme without exact datasheet |
| H7 | Side-device common gate | Correct before S3 | Side gate lost | Possible common dependency | Fully consistent | Devices lie in iMac sequence | No identity, delta or known topology | VERY LOW | Identify exact parts from public board evidence; no probing | Extreme |
| H8 | CS42L83 shared dependency | Correct before S3 | Companion clock/path wrong | Poor explanation | Fully consistent | Shares CS8409 resources | Headset role; final success; no correlated delta | VERY LOW | Public topology proof only | High |
| H9 | Controller start/DMA failure | Works pre-S3 | Would cause silence | No explanation required | Contradicted | None after trace | START and sustained progress proven | VERY LOW | None; already tested | N/A |
| H10 | Four independent amp faults | All healthy pre-S3 | All fail simultaneously | Direct but implausible | Fully consistent | Same bit on each | Common transition overwhelmingly favors shared cause | VERY LOW | Exact-part fault semantics | Extreme |

## What the ranking authorizes

It authorizes only documentation and a diagnostic design that reuses values
already obtained by normal execution. It does **not** authorize:

- `DEV_CFG1` writes copied from the Windows 48-kHz sequence;
- TAS register writes or interpreting TAS5722L as TAS5764L;
- GPIO toggles;
- arbitrary HDA verbs or coefficient reads;
- another S3 cycle without a separately reviewed one-cycle procedure.

See [NEXT-EXPERIMENTS.md](NEXT-EXPERIMENTS.md) for the smallest safe
discriminator and [OPEN-QUESTIONS.md](OPEN-QUESTIONS.md) for the evidence gates
required before any fix.

## Primary sources

- Linux CS8409 [source](https://codebrowser.dev/linux/linux/sound/hda/codecs/cirrus/cs8409.c.html),
  [tables](https://codebrowser.dev/linux/linux/sound/hda/codecs/cirrus/cs8409-tables.c.html),
  and the frozen [V10 source](../driver/source/cs8409.c).
- Linux HDA [codec PM implementation](https://codebrowser.dev/linux/linux/sound/hda/common/codec.c.html)
  and Intel HDA [controller PM implementation](https://codebrowser.dev/linux/linux/sound/hda/controllers/intel.c.html).
- Texas Instruments [TAS5722L datasheet SLOS946A](https://www.ti.com/lit/ds/symlink/tas5722l.pdf),
  used only as an explicitly comparative source.
- Cirrus [CDB42L42 evaluation-board documentation](https://statics.cirrus.com/pubs/rdDatasheet/CDB42L42_DS1083DB3.pdf),
  used for public CS8409 architecture, not iMac topology proof.

## Search coverage and negative results

The survey covered Linux 7.0 and current CS8409 sources/history, ALSA mailing
lists, Ubuntu's landed CS8409 PM series, public Linux/Android/Apple-driver
forks, TI and Cirrus public documents, public teardown/device-tree evidence,
community Intel-Mac projects, exact already-derived configuration facts, and
targeted ACPI/patent/erratum searches.

No public source was found that provides:

- a complete CS8409 register map or S3/PLL erratum;
- code that snapshots `DEV_CFG1` before suspend and restores the snapshot;
- an authoritative TAS5764L datasheet/register map;
- an exact public iMac19,2 board schematic or amp marking;
- an exact iMac19,2 audio ACPI/SMC resume method;
- publication-safe control-flow proof of a Windows resume-only clock/TAS
  sequence;
- exact identities for the devices at seven-bit `0x32` and `0x14`.

These are bounded negative search results, not proof that private vendor
documentation or unavailable board material does not exist.
