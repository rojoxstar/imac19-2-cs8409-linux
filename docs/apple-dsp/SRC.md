# Control sample-rate conversion

The DualBand child SRC is an integer 31× control interpolator, not the
PipeWire 44.1/48-kHz audio-rate conversion. At 44.1 kHz its input control rate
is exactly `44100/31`, approximately 1422.580645 Hz, and its output lies on the
44.1-kHz audio frame grid.

Frozen high-confidence results:

- `Q=31`, reset phase `0`, canonical `P=4` history;
- `_vDSP_conv` ABI and negative filter traversal recovered;
- copy helper bound to overlap-safe `memmove` semantics;
- history recurrence `H_next = concat(H,X)[M:M+P]`;
- Child-A input/history and convolution scratch remain exactly zero;
- lattice coverage is complete for `R=0` or `M>0`;
- production reachability of `R>0 && M=0` remains unknown.

The 88% Child-SRC completeness label is evidence-weighted and frozen pending
the installed-System request producer.

