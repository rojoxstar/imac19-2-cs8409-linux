# Research provenance and publication boundary

The canonical Apple source is Internet Recovery product `093-10615`, macOS
15.6.1 build `24G90`, requested for the iMac19,2 board identifier and verified
with Apple's signed Recovery chunklist. Exact recovered AppleHDA HAL metadata:

```text
version: 600.2
SHA256: 50bbc5e03670787af5e237d79d91bb2dd62c361d3f394d1813481cea3df2993e
UUID:   B31F8D33-758B-3BD3-8CC3-F305F6175F0F
arch:   x86_64
```

Static analysis shows that bundle is a property/open/close/notification
adapter, not the normal request producer. The local corpus is Recovery-only
and lacks the full System/SSV, exact x86_64 dyld cache, `coreaudiod`, complete
CoreAudio and AudioToolbox runtimes, and authenticated normal scheduler.

Only independently authored summaries, equations, test concepts, hashes,
UUIDs, symbols, and addresses are public here. Apple binaries, raw executable
sections, disassembly dumps, disk images, caches, and recovered machine code
are excluded.
