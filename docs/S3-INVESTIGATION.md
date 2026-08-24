# System S3 investigation

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
are unavailable. A related TAS5760L document associates bit 3 with a clock
condition, but that is a **non-authoritative analogy**, not a fix basis.

## DEV_CFG1 restore gap

Exact iMac19,2 Windows `CONF_0910` programs CS8409 coefficient 0x00 as:

```text
0xb008
  ... Windows ASP/clock sequence ...
0x9008
```

The frozen Linux iMac INIT does not restore a complete DEV_CFG1 value. This is
a source-proven coverage difference and is consistent with a common
downstream clock-state failure.

It is not a ready fix. Windows uses a different 48-kHz clock sequence, while
Linux has a machine-proven 44.1-kHz configuration. The safe single value and
placement were not proven, and actual post-S3 DEV_CFG1 loss was not observed.
No V12 was created.

## Current ranking

1. common TAS/speaker-path state indicated by `reg08=0x18`;
2. missing CS8409 ASP/PLL prerequisite after S3;
3. converter-to-ASP boundary;
4. digital-zero userspace content remains possible but a non-started stream
   is ruled down;
5. controller/DMA is strongly ruled down;
6. CS42L83;
7. unidentified side devices.

These are hypotheses except for the trace, acoustic outcome and exact logged
differential. Do not issue a TAS, DEV_CFG1 or GPIO write from this ranking.
