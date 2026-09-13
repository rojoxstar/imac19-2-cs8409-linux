# Spline limiter findings

The recovered clean-room limiter accepts linear amplitude, uses independent
per-channel sample-peak detection, and has no proved lookahead buffer. Its
static spline curve and state updates are substantially recovered. For normal
finite inputs under the recovered model, the downstream digital ceiling is
1.0 linear, or 0 dBFS.

That ceiling does not prove whole-chain or loudspeaker safety. Upstream
Mozart, Loudness, DualBand dynamics, profile selection, physical transducer
limits, and any additional protection behavior must all be accounted for.

