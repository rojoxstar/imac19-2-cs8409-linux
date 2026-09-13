# Loudness findings

`DspLoudness` is controlled by user-volume/MasterGain state and has its own
program-level-independent coefficient path. It uses a two-biquad shelf
cascade with coefficient interpolation. The active MasterGain grid recovered
from `DspFuncVolume` spans −12 dB through +12 dB; low MasterGain produces more
loudness boost.

Filter equations, sample-rate dependence, channel independence, and static
response for a supplied valid MasterGain are proven. Native global volume
bounds, every transient interpolated state, and the whole-chain maximum boost
remain partial or unknown. `DefaultVolume = -17.0` is not inserted as a fixed
attenuation because its link into this path is unresolved.

