# DualBand findings

`DspMozartCompressorDualBand` splits audio into low/high analysis branches to
derive two control signals; it is not the physical speaker crossover. The
canonical branch CurveBlocks disable their dynamic curve targets, and the V8
branch gate is disabled for the recovered canonical parameters.

Each control branch decimates by `Q=31`, processes a scalar control stream,
then uses a 31× child interpolator back to the audio-frame grid. The child
inputs reduce canonically to A=`0` and B=`1`; there is no post-SRC A/B
division. Child-A convolution scratch is exactly zero.

The parent write lattice for `M>0` is

```text
I(g,n) = g + 31n
E_g = ceil((R-g)/31)
```

V24 proves complete, unique output-slot coverage when `R=0` or `M>0`. The
unresolved mathematical domain is

```text
U = {(p,R) | 1 ≤ p ≤ 30 and 1 ≤ R ≤ 31-p}
```

equivalently `R>0 && M=0`. Its production reachability is unknown. It is not
classified as an Apple bug. The exact userspace request producer is required
to close that question.
