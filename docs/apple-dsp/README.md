# Apple speaker-DSP clean-room research

This directory summarizes static research against authenticated macOS Sequoia
15.6.1 build 24G90 x86_64 material. It publishes independently authored facts,
mathematics, and provenance metadata. Apple binaries, executable bytes, raw
disassembly, dyld caches, KernelCollections, disk images, and recordings are
not included.

Recovered high-level topology:

```text
stereo
  → global equalization
  → DspMozartCompressor
  → DspLoudness
  → DspMozartCompressorDualBand
  → DspGainStage
  → Dsp2To4Splitter
  → per-transducer processing
  → Dsp4ChOutput
```

The four logical channels use `[TL, TR, WL, WR]` internally. The generic
four-channel output permutation is `[TL, WL, RT, RW]`. Physical evidence maps
TAS addresses `0xd8`, `0xda`, `0xdc`, `0xde` to left tweeter, left woofer,
right tweeter, and right woofer respectively.

The present Linux path duplicates stereo to four feeds and contains no
observed frequency-selective software stage before the TAS devices. Apple and
Apple-distributed Windows evidence instead distinguishes woofer and tweeter
processing. This difference is architectural evidence, not permission to load
a reconstructed DSP chain.

Read [topology](TOPOLOGY.md), the [phase index](PHASE-INDEX.md), and the
[current blocker](CURRENT-BLOCKER.md). Current evidence-weighted estimates are
88% for the Child-SRC contract and 74% for DualBand safety bounds. Whole-chain
digital headroom and protection remain incomplete.

