# S3 evidence matrix

Later evidence supplement: DEV_CFG1 was observed as `0→0x1000`,
`0x8000→0x9000`, and `0x9000→0x9000` under the iMac19,2 PLL1-bit restoration
candidate. Playback continued after the bounded resume observation. This
updates the historical statement that resume always stays silent, but does not
establish a causal or complete S3 fix.

This ledger separates what the one controlled S3 experiment established from
what remains inferred. It is deliberately conservative: a successful write or
trace at one boundary does not prove the next boundary.

## Experiment identity

| Item | Result | Classification |
|---|---|---|
| Exact machine | iMac19,2, subsystem `106b:0f00` | **PROVEN** |
| Production baseline | V10 source; V11 logging-only candidate used for diagnosis | **PROVEN** |
| Pre-S3 playback | both sides, centered, clean | **PROVEN recorded outcome** |
| S3 cycles | exactly one diagnostic cycle | **PROVEN** procedure record |
| Resume INIT | completed, no final required failure | **PROVEN** journal |
| Post-S3 playback | total silence | **PROVEN recorded outcome** |
| Controller trace integrity | 500 relevant events, no overrun/drop | **PROVEN** |
| Current release status | S3 unsupported | release decision |

## End-to-end boundary matrix

| Boundary | Pre-S3 evidence | Post-S3 evidence | What is proven | What remains unknown |
|---|---|---|---|---|
| application/PipeWire request | known reference playback audible | same intended reference session | playback lifecycle requested | whether sample payload was nonzero post-S3 |
| ALSA trigger | normal playback | `azx_pcm_trigger START/STOP` | ALSA/controller trigger callbacks ran | content semantics |
| HDA controller start | normal playback | `snd_hdac_stream_start tag=1` | controller stream was started | downstream converter acceptance |
| DMA position | audible path | 497 advancing observations, expected approximate rate and wrap | position advanced normally | whether buffer contents were nonzero |
| converter programming | working implicitly | generic PREPARE requested restore | source path reprograms converters | command acceptance/functional route |
| CS8409 ASP/TDM setup | working | known coefficients replayed without transaction error | requests completed | PLL lock, pad clocks, electrical data |
| CS8409 GPIO latch | `GPIO_DATA=0x12` | `GPIO_DATA=0x12` | logical data readback matches | physical SDZ pad and FAULTZ state |
| TAS I2C | readbacks and audible | all required writes/readbacks complete | I2C communication | active state, clocks, power-stage output |
| TAS `reg08` | all four `0x10` | all four `0x18` | exact synchronous differential | exact TAS5764L meaning |
| CS42L83 | configured/buffers on | configured/buffers on | companion transactions succeed | speaker-path relevance, indirect clock effect |
| side `0x64/0x28` | zero readbacks | zero readbacks | no visible delta | identity, semantics, physical role |
| transducers | audible | silent | physical outcome changed | precise upstream failure boundary |

## Observation-to-hypothesis effects

| Observation | Evidence level | Strongly supports | Rules down | Does not prove |
|---|---|---|---|---|
| Audible before S3, silent after | **PROVEN recorded outcome** | S3-specific state loss | ordinary V10 success-path defect | which component failed |
| Full iMac INIT executed after resume | **PROVEN** | hidden/unverified restore gap | missing invocation of full INIT | that every write took effect electrically |
| Final CS42 readiness succeeded | **PROVEN** | I2C/companion recovery | final CS42 transaction failure | CS42 speaker-clock relevance |
| TAS boot/channel sequences returned success | **PROVEN** | I2C path alive | broad I2C outage | amp active state or serial clocks |
| Two PREPARE calls completed | **PROVEN** | repeatable software path | missing PREPARE callback | downstream sample transport |
| HDA START and DMA progress | **PROVEN** | failure is downstream of controller DMA, or data is zero | controller non-start/stall | converter/ASP/TAS delivery or content |
| Same stream tag/format/rate/channels | **PROVEN** software diagnostics | successful-path request is unchanged | obvious PCM parameter mismatch | controller-to-codec functional application |
| Coefficient `0x01=0x0220` both paths | **PROVEN** readback | one visible TDM state matches | loss of that single coefficient | `DEV_CFG1`, PLL lock, all vendor state |
| GPIO-data `0x12` both paths | **PROVEN** readback | logical GPIO data restored | simple missing GPIO-data write | physical level/direction/fault input |
| Four `reg08` values change together | **PROVEN** | shared clock/shutdown/power state | single-amp or channel-selector failure | exact field semantics or causation |
| Linux writes `0x18` in both paths | **PROVEN** | changed bit likely reflects state on closest comparator | simple different-request explanation | exact TAS5764L access type |
| Side-device readbacks unchanged | **PROVEN** | no visible side-device delta | gross missing write/readback | hidden state or real identity |
| Runtime PM remains audible | **PROVEN recorded outcome** | state retained in runtime transition but lost in deeper S3 | universally broken resume INIT | exact power-domain boundary |

## Converter-state result

Linux 7.0 source establishes:

1. `hda_call_codec_suspend()` invokes `hda_cleanup_all_streams()` unless a
   codec opts out.
2. V10 does not set `no_stream_clean_at_suspend`.
3. `really_cleanup_stream()` clears converter stream/channel and format and
   clears the software cache.
4. The next `snd_hda_multi_out_analog_prepare()` calls
   `snd_hda_codec_setup_stream()` for both speaker DACs.
5. The two-channel failing path reuses the requested stream tag/channel as
   designed; the four-channel path uses the custom offset.

Therefore “S3 clears DAC tags and PREPARE never restores them” has **source
evidence against it**. A command failure or vendor route downstream of those
ordinary verbs remains possible because the PCM model callback does not obtain
a strong functional acknowledgement.

Primary sources:

- Linux HDA [codec](https://github.com/torvalds/linux/blob/v7.0/sound/hda/common/codec.c),
  [controller](https://github.com/torvalds/linux/blob/v7.0/sound/hda/common/controller.c),
  and [stream core](https://github.com/torvalds/linux/blob/v7.0/sound/hda/core/stream.c).
- Frozen iMac [V10 source](../driver/source/cs8409.c).

## State-loss/restoration matrix

| State | Cleared/lost by generic suspend? | Restored by generic resume/PREPARE? | Restored by exact iMac INIT/PREPARE? | Residual confidence |
|---|---|---|---|---|
| HDA stream descriptor/DMA | controller shutdown/reinit | rebuilt on PCM start | no model-specific need | high; trace proves progress |
| DAC stream/channel | explicitly cleared | yes, by setup_stream | custom four-channel sync supplements path | high source proof |
| DAC format | explicitly cleared | yes | diagnostic only for custom path | high source proof |
| pin state | shut up | generic init/prepare | model pin enable | high logical; electrical unknown |
| AFG D0/D3 | D3 | D0 | no separate action | high |
| generic cached HDA verbs | dirty/synced | regmap/init verbs | n/a | high |
| CS8409 vendor coefficients | vendor-defined | not generally cached | selected TDM/ASP values replayed | incomplete |
| `DEV_CFG1` complete value | retention unknown | no generic vendor restore | no complete reconstruction; bit-3 RMW only | critical unknown |
| PLL lock | unknown | no public generic proof | no verified lock/status | critical unknown |
| ASP electrical clock/data pads | unknown | no generic proof | writes replayed | critical unknown |
| GPIO data/mask/direction registers | power behavior unknown | generic/model writes | explicitly replayed and read | logical state high; pad state unknown |
| TAS registers | external/vendor-defined | none | known sequence replayed | I2C state known; operation unknown |
| TAS SDZ/FAULTZ | physical board state | none | logical GPIO configuration only | critical unknown |
| amp rail | platform-defined | platform/firmware | no explicit exact iMac rail control | unknown |
| CS42L83 state | external/vendor-defined | generic loop not used for iMac | complete iMac sequence | transactions known |
| side-device hidden state | unknown | none | three writes replayed | semantics unknown |

## Runtime PM versus S3 differential

Both transitions converge on the same codec runtime callbacks and full iMac
INIT. Both controller resumes converge on `__azx_runtime_resume()`. System S3
adds coordinated PCI noirq/platform/firmware power handling beyond the ordinary
runtime transition. The operational difference is therefore evidence about
**retention depth**, not evidence for a different codec function.

| Candidate state | Can runtime retain while S3 loses? | Exact evidence |
|---|---|---|
| CS8409 vendor coefficient/PLL state | yes in principle | retention is vendor-defined; actual loss not captured |
| external ASP pad supply/reference | yes in principle | no exact measurement |
| shared amp SDZ/pad level | yes in principle | logical latch restored; electrical state unknown |
| external amp rail | yes in principle | no exact ACPI/rail evidence |
| controller DMA state | reinitialized in both | post-S3 progress proven |
| ordinary DAC stream tags | cleared/rebuilt in both | source-proven restore |

## TAS comparative inference boundary

The exact TAS5722L datasheet defines its register `0x08` bit 3 as serial-audio
clock error and says it also reads high in shutdown. The full V10 touched-
register map and defaults match TAS5722L unusually closely. This raises the
all-four `0x18` observation from generic correlation to **STRONG COMPARATIVE
EVIDENCE for a clock-or-shutdown state**.

It remains non-authoritative because no public TAS5764L register definition
was found. It cannot select between clock loss and SDZ shutdown and cannot
authorize a TAS operation.

## Evidence against low-ranked branches

- **Controller non-start/DMA stall:** directly contradicted by the valid trace.
- **One bad amp/selector:** contradicted by synchronous identical status on all
  four and correct per-address selectors.
- **Broad I2C failure:** contradicted by complete transactions to every device.
- **Missing full resume INIT:** contradicted by journal ordering.
- **Final CS42 readiness failure:** contradicted by recovered final pass.
- **V10 error-gate regression:** V11/V10 success path is hardware-equivalent;
  `imac_init_error=0` and PREPARE proceeds normally.
- **Simple DAC stream-tag omission:** generic cleanup and next PREPARE explicitly
  clear then rebuild state.

## Remaining explanations not ruled out

- nonzero sample content was not traced;
- ordinary converter commands may fail or vendor routing may remain wrong;
- `DEV_CFG1` or another PLL/pad prerequisite may be lost;
- ASP writes may complete while no external clock reaches the amps;
- GPIO4 may read high in the HDA register while the physical pad is ineffective;
- a common power resource may remain wrong;
- exact TAS5764L `reg08` semantics may differ from TAS5722L.

## Release conclusion

The evidence is enough to rule down controller/DMA failure and prioritize the
CS8409-to-amplifier common clock/shutdown boundary. It is not enough to select
a safe write. V10 remains the daily-driver baseline with system S3 and
hibernate unsupported.
