# Current blocker

V31–V34 established that the authenticated local material is Recovery-only.
The normal installed-System CoreAudio producer that calls
`IOAudioEngineUserClient::performClientIO` is missing.

Required next artifact: an authenticated Intel/x86_64 macOS Sequoia 15.6.1
build 24G90 installed System/SSV, or the complete matching
`dyld_shared_cache_x86_64` family plus standalone audio daemon/host binaries
and build metadata. See [help wanted](../HELP-WANTED-24G90.md).

Until that artifact is available, the Child-SRC contract remains 88%, DualBand
safety bounds 74%, whole-chain digital headroom partial, and reconstructed
protection insufficient for a new playback experiment.
