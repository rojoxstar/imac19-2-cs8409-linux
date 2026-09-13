# IOAudio scheduling boundary

Static analysis recovers this kernel-side chain:

```text
IOAudioEngineUserClient::performClientIO
  → performClientOutput
  → IOAudioStream::processOutputSamples
  → AppleHDAEngine::clipOutputSamples
  → DspFuncManager::process
  → DualBand / control SRC
```

`F` is the engine's sample frames per buffer. Both offset and count use audio
frames. Kernel-side wrap splitting is:

```text
no wrap:  (offset, count)
tail:     (offset, F-offset)
head:     (0, count-(F-offset))
```

The exact normal userspace producer of `(offset,count,F)` is absent from the
Recovery corpus. Consequently process-piece to child-SRC `R` mapping and the
production reachability of the unsafe mathematical domain remain unresolved.
