# Topology and channel order

## Current Linux path

The source proves a 44.1-kHz stereo PCM interface and duplication to four
speaker feeds. No FIR, IIR, EQ, crossover, limiter, or delay is observed in
Linux before the four TAS5764L devices.

## Recovered Apple graph

Static resources and exact-symbol analysis support:

```text
L/R → EQ → Mozart → Loudness → DualBand control stage → GainStage
    → 2-to-4 split → transducer EQ/limiting → four-channel output
```

DualBand is **not** the physical woofer/tweeter crossover. Its low/high
analysis branches derive control modulation. Physical transducer separation
occurs later in per-transducer processing.

| Internal channel | Output position | Amplifier | Physical class |
|---|---|---:|---|
| TL | FL | `0xd8` | left tweeter |
| WL | FR | `0xda` | left woofer |
| TR | RL | `0xdc` | right tweeter |
| WR | RR | `0xde` | right woofer |

This reflects the recovered generic `Dsp4ChOutput` permutation
`[TL,TR,WL,WR] → [TL,WL,RT,RW]`. The optimized runtime permutation retains a
documented caveat and should be rechecked before implementation.
