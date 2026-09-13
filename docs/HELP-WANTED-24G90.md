# Help wanted: exact macOS 15.6.1 (24G90) Intel audio userspace

The clean-room iMac19,2 audio research has reached a precise artifact blocker.
The available authenticated Apple Internet Recovery product `093-10615`
proves macOS Sequoia 15.6.1 build `24G90`, but Recovery does not contain the
normal installed-System CoreAudio request producer.

We need an authenticated, read-only system artifact from an Intel/x86_64
installation of exactly:

```text
ProductVersion:      15.6.1
ProductBuildVersion: 24G90
architecture:        x86_64 / Intel capable
```

Preferred evidence is a complete System/SSV copy. The minimum potentially
useful set is the complete matching `dyld_shared_cache_x86_64`, every required
subcache, relevant standalone audio daemon/host binaries, and
`SystemVersion.plist` proving 24G90. Files need SHA256 hashes and their original
paths/container provenance. A Recovery-only image, another build, arm64-only
cache, partial cache, symbol dump, or unverified repack cannot close the gap.

We do not need any user Data volume or personal material. Do not provide
`/Users`, home directories, keychains, credentials, browser data, mail,
messages, photos, or documents.

The next static target is the userspace component that supplies normal
`(offset, frame_count, frames_per_buffer)` requests to
`IOAudioEngineUserClient::performClientIO`. No Apple binary will be
redistributed by this repository. Please do not test speculative TAS5764L,
CS8409, CS42L83, GPIO, HDA-verb, or I2C writes.
