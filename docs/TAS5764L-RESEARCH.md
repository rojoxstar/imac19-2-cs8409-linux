# TAS5764L and amplifier-state research

> **Safety boundary:** register `0x04 = 0xab` must not be described as a
> proven TAS5764L gain value. The often-quoted `-18 dB` result follows the
> published TAS5760M/TAS5722L-style `0xcf = 0 dB`, `0.5 dB/code` mapping and
> is an **analogy only**. No exact public TAS5764L register authority has been
> recovered, and this repository does not recommend changing the value.

The four exact iMac19,2 amplifier addresses are part of the proven driver and
derived vendor configuration. Their public silicon identity is strongly
supported as TAS5764L, but no public TAS5764L datasheet or register map was
found. This document keeps exact-device facts separate from an unusually close
TAS5722L comparison.

## Identity result

**TAS5764L on exact iMac19,2: STRONG EVIDENCE, not publicly conclusive.**

Evidence in favor:

- exact iMac19,2 derived configuration and V10 use four Apple amplifier
  devices at eight-bit addresses `0xd8`, `0xda`, `0xdc`, `0xde`;
- the public Apple-oriented source lineage identifies the table as TAS5764L;
- iFixit identifies a TAS5764L in the closely related
  [2017 21.5-inch 4K iMac](https://www.ifixit.com/Teardown/iMac+Intel+21.5-Inch+Retina+4K+Display+2017+Teardown/92170);
- iFixit identifies
  [four TAS5764L devices in the iMac Pro](https://www.ifixit.com/Teardown/iMac+Pro+Teardown/101807);
- a public
  [Apple iBridge2,1 device-tree transcription](https://gist.github.com/insidegui/963248924cb6abd580dce53c07c5ec50)
  describes four `audio-control,tas5764` nodes at seven-bit addresses
  `0x6c`–`0x6f`.

Limits:

- no exact iMac19,2 teardown text or public schematic identifies the marking;
- no captured device-ID transaction exists;
- Apple could use a customer-specific derivative;
- no public document explains how TAS5764L relates to TI's published parts.

The canonical wording remains “four devices with strong TAS5764L evidence,”
not “TAS5764L register semantics are proven.”

## Exact transaction facts

The driver uses eight-bit address bytes; canonical seven-bit addresses are:

| Driver address | Seven-bit address | Selector | Strong physical-channel inference |
|---:|---:|---:|---|
| `0xd8` | `0x6c` | 2 | left tweeter |
| `0xda` | `0x6d` | 0 | left woofer |
| `0xdc` | `0x6e` | 3 | right tweeter |
| `0xde` | `0x6f` | 1 | right woofer |

Addresses and selectors are **PROVEN**. Physical channel names are a **STRONG
INFERENCE** from exact logical topology plus related Apple public evidence;
they are not backed by an exact public iMac19,2 schematic.

V10 boot programming for each device is:

```text
01=fc, 02=04, 03=80, 04=cf, 06=51, 08=00, 13=00, 14=02
```

V10 channel/PREPARE programming is:

```text
02=44
03=80|selector
06=55
08=18
04=ab
13=00
02=44
01=fd
read 01,02,03,04,06,08
```

The helper treats a completed I2C transaction as success; it does not compare
all returned values with the just-requested bytes. In particular, a successful
write/read cycle can return `reg08=0x18` and still report setup success. This
proves bus/controller/device communication only, not clock validity, active
power stage or audible sample reception.

## The TAS5722L fingerprint

The closest public, authoritative transaction-map match found is TI's
**TAS5722L**, documented in
[SLOS946A](https://www.ti.com/lit/ds/symlink/tas5722l.pdf) and supported by a
TI-authored Linux
[`tas5720/tas5722` driver](https://github.com/torvalds/linux/blob/master/sound/soc/codecs/tas5720.c).

This comparison is exceptionally specific:

| Property/register | TAS5722L authoritative fact | Apple/V10 observation | Assessment |
|---|---|---|---|
| Function | mono digital-input Class-D | four nominally mono amps | strong match |
| Package | 32-pin 4×4-mm QFN | related Apple footprint evidence | strong match |
| Digital supply | 1.8 V | related board evidence | strong match |
| Serial input | TDM, up to eight slots, 44.1/48 kHz supported | four 32-clock cells at 44.1 kHz | strong match |
| `reg01` reset | `0xfd` | active value/readback `0xfd` | exact byte match |
| `reg02` reset | `0x04` | boot `0x04`, active `0x44` | exact byte match |
| `reg03` reset | `0x80` | boot `0x80`, active `0x80|slot` | exact pattern match |
| `reg04` reset | `0xcf` | boot `0xcf` | exact byte match |
| `reg06` reset | `0x51` | boot `0x51`, active `0x55` | exact byte match |
| `reg08` reset | `0x00` | boot `0x00` | exact byte match |
| `reg13` reset | `0x00` | driver `0x00` | exact byte match |
| `reg14` reset | `0x02` | boot `0x02` | exact byte match |
| `reg00` ID | `0x12` | not captured | unknown |

The complete touched-register default match, selector pattern, package and TDM
fit are much stronger than generic family resemblance. They are still
**RELATED / NON-AUTHORITATIVE** for TAS5764L. A customer-specific part can
change status semantics while retaining an interface.

## Register `0x08`

### Exact iMac observation

V11 reused the values already read by normal V10 operation and added no I2C
transaction:

| Condition | d8 | da | dc | de | Requested value | Physical output |
|---|---:|---:|---:|---:|---:|---|
| pre-S3 | `0x10` | `0x10` | `0x10` | `0x10` | `0x18` | audible, clean |
| post-S3 | `0x18` | `0x18` | `0x18` | `0x18` | `0x18` | total silence |

The only changed bit is bit 3 (`0x08`). All four devices changed together.
Those statements are **PROVEN**. Its TAS5764L meaning is **UNKNOWN**.

### Exact TAS5722L semantics, for comparison only

TI defines TAS5722L register `0x08` as “Fault Configuration and Error Status”:

| Bits | TAS5722L field | Access | TAS5722L meaning |
|---:|---|---|---|
| 5:4 | `OC_THRESH` | read/write | over-current threshold |
| 3 | `CLKE` | read-only, self-clearing | serial-audio-interface clock error present |
| 2 | `OCE` | read-only, latched | over-current error |
| 1 | `DCE` | read-only, latched | DC error |
| 0 | `OTE` | read-only, latched | over-temperature error |

The datasheet also states:

- on a serial-audio clock error, the device rapidly enters sleep to limit
  artifacts, asserts `FAULTZ`, and asserts `CLKE`;
- when valid clocks return, volume ramps to the prior playback state;
- **while in shutdown, the clock detector is powered down and `CLKE` reads
  high; that reading does not identify a clock error**;
- I2C/register retention can coexist with a shut down or sleeping power stage.

If, and only if, TAS5764L shares this behavior, writing `0x18` requests writable
threshold bit `0x10`; healthy readback `0x10` means status bit 3 is clear, and
silent readback `0x18` means it is asserted. This is **STRONG COMPARATIVE
EVIDENCE** that the delta is live state rather than a persistent configuration
failure.

It does not distinguish two leading mechanisms:

1. BCLK/LRCLK/sample-interface clocks are absent or invalid; or
2. all devices remain in common hardware/software shutdown.

### Why TAS5760L is not authority

| Device | Public documentation | Similarity | Transferability |
|---|---|---|---|
| TAS5764L | no exact public datasheet found | likely Apple part | exact semantics unknown |
| TAS5722L | [TI SLOS946A](https://www.ti.com/lit/ds/symlink/tas5722l.pdf) | strongest map/default/package/TDM fingerprint | strong comparative evidence only |
| TAS5720L/M | [TI SLOS903B](https://www.ti.com/lit/ds/symlink/tas5720l.pdf) | related mono map and CLKE bit | weaker comparative evidence |
| TAS5760L | [TI SLOS782](https://www.ti.com/lit/ds/symlink/tas5760l.pdf) | some addresses resemble Apple writes | weak; stereo/PBTL and map differences |
| TAS5733L | public TI datasheet | broad family only | not transferable |

No line in this repository treats a related-device bit definition as safe
TAS5764L write semantics.

## Common-cause implications

The synchronized all-four transition materially constrains root cause:

| Common dependency | Fit | Evidence |
|---|---|---|
| shared BCLK/LRCLK from CS8409 ASP | excellent | exact logical topology plus TAS5722L mechanism; physical iMac19,2 wiring inferred |
| CS8409 PLL/pad prerequisite | excellent | exact iMac restore gap and upstream clock-source comments |
| common GPIO4/SDZ | excellent | exact derived GPIO role; logical state replayed but pad not measured |
| common power rail | plausible | can affect all four; no exact rail evidence |
| shared MCLK | weak/unknown | related Apple board evidence suggests MCLK may be unused; exact board unknown |
| individual selector/config error | poor | selectors/readbacks remain per-device-correct; same status bit changes |
| four independent faults | very poor | simultaneous identical transition is implausible |

The clock and shutdown branches are not mutually exclusive: an invalid
CS8409 pad/clock prerequisite could keep the related amplifier state machine
asleep, and a shared shutdown signal could make a clock-status detector read
abnormal.

## GPIO4 and GPIO5

Already-derived exact `CONF_0910` evidence establishes vendor configuration
intent:

- GPIO4 is an AFG-power-controlled output associated with amplifier `SDZ`;
- GPIO5 is an input associated with aggregate amplifier `FAULTZ`.

Linux final INIT writes direction/data/mask `0x12/0x12/0x1f`; V11 observed the
same GPIO-data value `0x12` before and after S3. Therefore the logical GPIO4
output/high state is replayed. This does not prove:

- physical pad voltage;
- active polarity on exact TAS5764L;
- that the pad leaves a low-power override after S3;
- whether GPIO5 asserted during the failure.

V10 has no amplifier-FAULTZ handling path, making GPIO5 a real observability
gap. A GPIO toggle is not justified. A future read must use an already normal,
reviewed read path or receive separate authorization.

## Reset, shutdown and retention

For exact TAS5764L these remain **UNKNOWN**:

- software-reset semantics;
- SDZ polarity/timing;
- clock-fault clearing;
- register retention across SDZ, rail loss or S3;
- safe retry or reset sequence;
- whether `0x18` is status, configuration, or a mixed register.

TAS5722L documents retention across shutdown and default reload after DVDD POR,
but that cannot be promoted to exact-device proof. V10's current policy—stop
on the first required error, fail closed, perform no speculative rollback, and
allow a later natural complete INIT—is still the safest evidence-based policy.

## Windows/public-source comparison

The exact derived iMac19,2 configuration uses `reg08=0x10`; Linux requests
`0x18`. Neither value should be changed here. Existing legal/public evidence
does not show exact Windows post-S3 readback, masking, status clearing or a
resume-specific TAS recovery branch.

Public Linux lineage includes the Apple-oriented commit
[`d8c9001418e6172099a0907f022534f152e29d71`](https://github.com/egorenar/snd-hda-codec-cs8409/commit/d8c9001418e6172099a0907f022534f152e29d71),
which labels the support TAS5764L. It is useful provenance, not vendor register
authority.

## What `TAS setup PASS` proves

It proves:

- the CS8409 I2C engine completed required transactions;
- each addressed device ACKed/responded sufficiently for the helper;
- the captured register values were returned.

It does **not** prove:

- SDZ is deasserted electrically;
- BCLK/LRCLK are present or valid;
- the device reached active mode;
- sample data is nonzero or accepted;
- the power stage drives a transducer;
- the mixed configuration/status readback equals the requested byte.

## Exact-part fix gate

A TAS change would require:

1. conclusive exact iMac19,2 silicon identity;
2. an authoritative TAS5764L register definition;
3. proof of what `reg08` bit 3 means on that silicon;
4. proof distinguishing clock error from shutdown;
5. a demonstrated causal correction;
6. known-safe reset/write order, polarity and timing;
7. no conflict with the validated V10 success path.

None of these can be replaced by the TAS5722L fingerprint alone. No TAS write,
reset, status clear or fail-closed value comparison is authorized.
