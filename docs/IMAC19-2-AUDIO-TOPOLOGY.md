# iMac19,2 audio topology and public hardware evidence

> **Current-status note:** this document preserves the hardware/S3 research
> context. For the subsequently recovered four-transducer DSP topology and
> channel order, see [`apple-dsp/TOPOLOGY.md`](apple-dsp/TOPOLOGY.md) and
> [`CURRENT-STATUS.md`](CURRENT-STATUS.md).

This is a derived factual topology. It does not redistribute Apple/Boot Camp
binaries, board files or schematics. Where exact iMac19,2 public evidence ends,
the text says so.

## Exact supported identity

| Item | Value | Evidence |
|---|---|---|
| Machine | Apple iMac19,2, 2019 21.5-inch Retina 4K | **PROVEN** on tested system |
| HDA codec | Cirrus Logic CS8409, `1013:8409` | **PROVEN** |
| Subsystem | `106b:0f00` | **PROVEN** |
| Vendor configuration | exact derived `CONF_0910` mapping | **PROVEN** derived fact |
| Linux production path | V10, 44.1 kHz, S32_LE container | **PROVEN** source and runtime |
| Amplifier addresses | `d8/da/dc/de` | **PROVEN** source/configuration |
| Amplifier identity | TAS5764L | **STRONG EVIDENCE**, no exact public datasheet/marking |
| Companion | CS42L83-class Apple headphone/headset codec | **STRONG EVIDENCE** |

## Speaker data path

```text
application / PipeWire
  -> ALSA PCM
  -> Intel HDA stream descriptor and DMA
  -> CS8409 HDA converter widgets 0x02 / 0x03
  -> CS8409 vendor routing
  -> CS8409 ASP1/TDM
       32-clock cells at offsets 0, 32, 64, 96
       shared serial clock / frame clock / data boundary
  -> four digital-input mono amplifier addresses
       d8 selector 2   strongly inferred left tweeter
       da selector 0   strongly inferred left woofer
       dc selector 3   strongly inferred right tweeter
       de selector 1   strongly inferred right woofer
  -> four internal transducer channels
```

The trace proves the path through controller DMA during the silent S3 attempt.
It does not prove converter output, ASP clocks/data, amplifier active state or
transducer motion.

## Address representation

The CS8409 helper stores eight-bit I2C address bytes with read/write handling in
the controller helper. Canonical seven-bit equivalents are:

| Driver address | Seven-bit address | Role status |
|---:|---:|---|
| `0xd8` | `0x6c` | speaker amp, selector 2 |
| `0xda` | `0x6d` | speaker amp, selector 0 |
| `0xdc` | `0x6e` | speaker amp, selector 3 |
| `0xde` | `0x6f` | speaker amp, selector 1 |
| `0x90` | `0x48` | CS42L83-class companion path |
| `0x64` | `0x32` | unidentified on exact board |
| `0x28` | `0x14` | unidentified on exact board |

Do not compare the driver bytes directly with a datasheet's seven-bit address
without converting the representation.

## Four-amplifier topology

Four independent I2C addresses receive the same boot and channel sequence with
different slot selectors. Related public Apple evidence strongly supports a
left/right tweeter/woofer arrangement:

- iFixit identifies TAS5764L on the
  [2017 21.5-inch Retina 4K iMac](https://www.ifixit.com/Teardown/iMac+Intel+21.5-Inch+Retina+4K+Display+2017+Teardown/92170);
- iFixit identifies four TAS5764L devices on the
  [iMac Pro](https://www.ifixit.com/Teardown/iMac+Pro+Teardown/101807);
- a public
  [Apple iBridge2,1 device-tree transcription](https://gist.github.com/insidegui/963248924cb6abd580dce53c07c5ec50)
  labels `0x6c/0x6d/0x6e/0x6f` as left tweeter, left woofer, right tweeter and
  right woofer.

Those are cross-model corroboration. No exact public iMac19,2 schematic was
found, so the physical channel labels remain **STRONG INFERENCE** rather than
proof.

The simultaneous all-four `reg08` transition strongly favors a shared
dependency—serial clock/frame clock, shutdown, reset, power or CS8409 pad—over
four independent amplifier faults.

## CS8409 ASP/TDM boundary

V10 programs four receive descriptors (`0x0800`, `0x0820`, `0x0840`,
`0x0860`) and a validated 44.1-kHz clock/framing sequence. Public CS8409
evaluation-board documentation shows ASP interfaces expose MCLK, SCLK, LRCK
and data pins, but it does not disclose this board's wiring.

What is **PROVEN**:

- all four logical cells are programmed;
- the iMac INIT and PREPARE replay the known coefficients;
- DMA progresses post-S3;
- all four amplifiers remain I2C-responsive.

What is **UNKNOWN**:

- whether BCLK/LRCLK/data are electrically present post-S3;
- which exact CS8409 PLL/pad state drives them;
- whether the exact board uses MCLK at the amplifiers;
- whether a power-domain override defeats a correct-looking coefficient or
  GPIO readback.

## GPIO4 / GPIO5

Exact derived configuration intent is:

```text
CS8409 GPIO4 -> common amplifier SDZ/shutdown control
aggregate amplifier FAULTZ -> CS8409 GPIO5
```

V10 reissues direction/data/mask `0x12/0x12/0x1f`; V11 recorded the same GPIO
data value before and after S3. Therefore logical restoration is proven.
Electrical voltage, exact polarity and power-domain retention are not.

GPIO5 is not handled as an amplifier fault input by V10, so the aggregate fault
state during the silent attempt is unknown. The closest public comparator,
TAS5722L, uses active-low `SDZ` and open-drain active-low `FAULTZ`; those
polarity semantics are **not authoritative for TAS5764L**.

## CS42L83 role

The companion path is configured and its buffers are enabled during speaker
PREPARE, but the strongest topology evidence associates it with
headphone/headset/capture functions rather than the four speaker power stages.
Public upstream work likewise describes Apple CS42L83 support as a headphone-
jack codec; see the
[CS42L83 Apple patch-series summary](https://lwn.net/Articles/907647/).

Consequences:

- successful CS42 reset, readiness and buffer writes do not prove speaker
  clocks or sample flow;
- a direct CS42 cause for synchronized four-amp silence is very low priority;
- an indirect shared-CS8409-resource interaction remains possible but has no
  positive evidence.

## Side device at driver address `0x64`

Exact V10 operations are:

```text
write register 0x14 <- 0xe4
read  register 0x14
```

The source lineage labels the register `PCMModeConfig` and traces the byte to a
foreign Apple model/helper associated with a MAX98706-family amplifier. A
related public [MAX98372 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98372.pdf)
also calls register `0x14` PCM mode configuration, but MAX98372 is not
authoritative for MAX98706 or iMac19,2.

Exact board identity and physical population are **UNKNOWN**. The exact
iMac19,2 `CONF_0910` evidence does not contain this address. Its post-S3
readback did not differ, and no shared speaker-path role is known. S3 relevance
is therefore **VERY LOW**, not zero.

## Side device at driver address `0x28`

Exact V10 operations are:

```text
write/read register 0x05 <- 0x00
write/read register 0x04 <- 0x51
```

The historical byte source is an Analog Devices SSM3515 helper. On the exact
[SSM3515 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ssm3515.pdf),
seven-bit address `0x14` is valid and those registers configure serial/TDM
format and slot. That proves provenance of the copied bytes, **not** that an
SSM3515 is populated on iMac19,2.

Exact identity and role remain **UNKNOWN**; exact `CONF_0910` does not contain
the sequence. Its unchanged zero readbacks and missing topology link make it a
very-low-priority S3 cause.

## Shared bus implications

The four amplifiers, CS42L83 path and side-device transactions are issued by
the CS8409 vendor I2C engine. A bus-engine or I2C-clock failure could affect
multiple devices. In the reproduced failure, all required I2C transactions
completed, so a general I2C outage is strongly ruled down. This does not prove
the independent ASP serial-audio clock domain.

## ACPI, SMC and platform power

No exact public iMac19,2 audio `_PS0`/`_PS3`, ACPI power resource, SMC command,
or resume-only rail sequence was found. Linux PCI/HDA system resume includes a
deeper platform transition than runtime PM, so a common external rail or pad
supply is technically consistent. Without an exact method or measurement, it
remains **UNKNOWN/LOW priority** and cannot justify a platform write.

OpenCore's public
[iMac19,2 model record](https://github.com/acidanthera/OpenCorePkg/blob/master/AppleModels/DataBase/iMac/IM192.yaml)
identifies board model `Mac-63001698E7A34814`. It is useful identity metadata
only and contains no audio PM sequence.

Community Apple CS8409 projects that alter s2idle/xHCI behavior address wider
platform suspend problems. They do not prove this exact S3 audio root cause.

Targeted searches of public FreeBSD, AppleALC/OpenCore, Hackintosh codec-dump
and Linux bug-report material found useful model/codec identity clues but no
exact iMac19,2 speaker-clock PM sequence. Codec layouts and HDEF renames do not
establish physical rail or S3 callback behavior.

## Cross-model compatibility matrix

| Model/family | Subsystem | CS8409 | Companion/speaker devices | Public S3 status | Relevant architecture | Transferability |
|---|---:|---|---|---|---|---|
| exact iMac19,2 | `106b:0f00` | yes | CS42L83-class + four likely TAS5764L | **FAIL: post-S3 silence** | V10/V11 in this repository | exact target only |
| iMac18,3 public-driver branch | `106b:1000` in source | yes | four TAS-oriented iMac path | not established here | shares portions of out-of-tree iMac support | constants/behavior not transferable without proof |
| 2017 21.5-inch 4K iMac | not established here | public teardown supports Cirrus/TAS context | TAS5764L identified by iFixit | unknown | board-family corroboration | topology clues only |
| iMac Pro 2017 | not established here | Cirrus audio complex | four TAS5764L + CS42L83 identified by iFixit | unknown | four-amp topology | identity/topology clues only |
| upstream Dell Bullseye/Warlock/Cyborg/Odin | multiple `1028:*` | yes | CS42L42 | upstream resume/jack fixes exist | full `DEV_CFG1`/ASP table replay | architecture precedent, not iMac constants |
| upstream Dell Dolphin | multiple `1028:*` | yes | two CS42L42 companions | upstream PM support | multiple companion suspend/resume | architecture precedent only |
| upstream CDB35L56 reference platform | reference IDs | yes | four CS35L56 | public 2026 support | complete vendor clock table | architectural comparison only |
| community Intel MacBook projects | varies | often yes | MAX/SSM/TAS variants | mixed/unknown | confirms broad Apple diversity | do not port by model name |

The matrix deliberately uses “unknown” instead of generalizing a successful
or failing sleep result across Apple systems.

## Public board material policy

Public teardowns, community device-tree transcriptions and legally accessible
repair material were used only to derive component/topology facts. No
copyrighted schematic or proprietary driver binary is included in this
repository. A related-board drawing cannot prove exact iMac19,2 wiring.

## Remaining topology unknowns

1. Exact amplifier marking and TAS5764L register authority.
2. Exact physical BCLK/LRCLK/data/MCLK sharing on iMac19,2.
3. Physical GPIO4 polarity and voltage after S3.
4. GPIO5 aggregation and state during the silent failure.
5. Common amp power-rail control and S3 retention.
6. Identities and physical presence of side addresses `0x32` and `0x14`.
7. Any exact ACPI/SMC audio power method.

These unknowns define evidence requests, not permission to probe or write.
