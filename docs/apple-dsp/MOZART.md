# Mozart compressor findings

The clean-room model recovers CurveBlock structure, high-level piecewise
`vectCurve` behavior, an asymmetric attack/release state machine, and the
control-rate SRC topology. The precise equations and finite-state tests were
derived independently from static evidence; executable bytes are not
published.

The 31× interpolator uses a recovered 267-tap symmetric positive FIR. Its
nominal linear-phase group delay is 133 audio frames, approximately
3.015873 ms at 44.1 kHz. That is a property of the FIR. Exact externally
visible startup latency and a universal Child-B output bound remain conditional
on the unresolved producer/request domain.
