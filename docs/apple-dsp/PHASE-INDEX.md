# Forensic phase index

This index compresses the V9–V34 research history. It links conclusions rather
than copying internal reports or proprietary evidence.

| Phase | Single target | Main result / correction | Remaining dependency |
|---:|---|---|---|
| V9 | Mozart control SRC | `Q=31`, 267-tap polyphase structure, phase/state contract | external helper/startup semantics |
| V10 | Loudness | two interpolated shelves; MasterGain dependence | global valid control bounds |
| V10.5 | MasterGain | active −12…+12 dB grid recovered | complete producer/range semantics |
| V11 | DualBand | analysis/control topology and two child SRCs | dynamic output bound |
| V12 | Mozart bounds | finite bound prerequisites mapped | final modulation extrema |
| V13 | V8 reachability | canonical branch gate disabled | noncanonical reachability |
| V14 | child startup | reset/phase/startup structure | exact helper/provider semantics |
| V15 | child contract | control ratio and canonical A=0, B=1 corrected; no post-SRC division | external helpers |
| V16 | external helper | `0x13d6190` proved `_vDSP_vsadd`, not FIR | convolution provider |
| V17 | convolution call | `_vDSP_conv` ABI and reversed physical coefficient traversal | exact provider semantics |
| V18 | vDSP provider | provider semantics and Child-A zero implication | runtime phase table |
| V19 | phase table | exact polyphase construction/table evidence | exposed-output indexing |
| V20 | child exposure | startup output path narrowed | full pointer coverage |
| V21 | memory exposure | output base/lattice and history copies | copy overlap semantics |
| V22 | copy helper | `memmove` binding and exact history recurrence | parent exposure |
| V23 | parent exposure | parent write mapping recovered | unconditional lattice proof |
| V24 | lattice coverage | complete/unique iff `R=0` or `M>0`; unsafe domain isolated | production reachability of `M=0,R>0` |
| V25 | upstream count | frame-count contract narrowed | callback scheduler |
| V26 | scheduler contract | cross-block/count cases enumerated | concrete producer |
| V27 | stream vtable | relevant stream dispatch identified | callback owner |
| V28 | callback scheduler | scheduling call family narrowed | workloop producer |
| V29 | workloop producer | kernel producer boundary narrowed | `performClientIO` caller |
| V30 | performClientIO | frame units, `F`, no-wrap/tail/head split proven | normal userspace caller |
| V31 | userspace candidate | AppleHDAHALPlugIn rejected as normal producer | installed-System host |
| V32 | image search | local audio candidates rejected or incomplete | full installed userspace |
| V33 | artifact inventory | exact Recovery corpus classified insufficient | exact 24G90 System/cache |
| V34 | acquisition validation | no new artifact; RE remains blocked | exact installed 24G90 x86_64 artifact |

The percentages 88% (Child SRC) and 74% (DualBand safety bound) are frozen
evidence-weighted project estimates, not statistical confidence or safety
clearance.
