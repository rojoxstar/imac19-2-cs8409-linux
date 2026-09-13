# System S3 investigation

> Current correction: this document records the original reproduced silent
> cycle. Later experiments observed DEV_CFG1 transitions `0→0x1000` and
> `0x8000→0x9000`, plus continued playback after resume. The PLL1 restoration
> remains neither necessary nor sufficient by proof; S3 is still unqualified.
> See [current status](CURRENT-STATUS.md).

## Outcome

**System S3 is unsupported.** One controlled cycle was reproduced with
audible, clean playback before suspend and total silence afterward.

## Controller/DMA proof

During the silent playback attempt, existing Linux tracepoints recorded:

```text
azx_pcm_trigger cmd=START        observed
snd_hdac_stream_start tag=1     observed
azx_get_position                497 observations
sustained position progress     observed
normal buffer wrap              observed
azx_pcm_trigger cmd=STOP        observed
overrun / dropped events        0 / 0
```

The progression approximately matched the expected 44.1-kHz, stereo,
S32_LE byte rate. This proves controller start and DMA-position advancement.
It does not prove nonzero samples or downstream converter/ASP/TAS delivery.

## TAS differential

V11 reused values already read by the normal V10 TAS sequence; it added no
hardware transaction.

| Condition | d8 reg08 | da reg08 | dc reg08 | de reg08 | Audio |
|---|---:|---:|---:|---:|---|
| Before S3 | `0x10` | `0x10` | `0x10` | `0x10` | Audible |
| After S3 | `0x18` | `0x18` | `0x18` | `0x18` | Silent |

Linux requests `reg08=0x18` in both conditions. Exact TAS5764L bit semantics
are unavailable. Phase 20 found that TI's documented TAS5722L package,
register map, touched-register defaults and TDM programming are an unusually
close match. On that exact related device, register `0x08` bit 3 reports a
serial-audio clock error and also reads high in shutdown. This is **strong
comparative evidence** for a common clock-or-shutdown state, not authoritative
TAS5764L semantics or a fix basis.

## DEV_CFG1 restore gap

Exact iMac19,2 Windows `CONF_0910` programs CS8409 coefficient 0x00 as:

```text
0xb008
  ... Windows ASP/clock sequence ...
0x9008
```

The frozen Linux iMac INIT does not restore a complete DEV_CFG1 value. Its
normal I2C-clock helper does read/modify/write coefficient 0, but changes only
bit 3 and preserves all other state it finds. It therefore cannot reconstruct
PLL/vendor bits already lost. This is a source-proven coverage difference and
is consistent with a common downstream clock-state failure.

It is not a ready fix. Windows uses a different 48-kHz clock sequence, while
Linux has a machine-proven 44.1-kHz configuration. The safe single value and
placement were not proven, and actual post-S3 DEV_CFG1 loss was not observed.
No V12 was created.

## Current ranking

1. missing/shared CS8409 ASP clock or PLL/pad prerequisite after S3;
2. common amplifier shutdown/SDZ electrical state;
3. converter-to-ASP/vendor-route boundary;
4. common platform power state or digital-zero userspace content;
5. a different common TAS internal condition;
6. controller/DMA is strongly ruled down;
7. CS42L83 and unidentified side devices are very low priority.

The first two both explain the related-device `reg08=0x18` behavior, so that
readback does not select between them. Digital-zero content remains possible,
but a non-started stream is ruled down.

These are hypotheses except for the trace, acoustic outcome and exact logged
differential. Do not issue a TAS, DEV_CFG1 or GPIO write from this ranking.
