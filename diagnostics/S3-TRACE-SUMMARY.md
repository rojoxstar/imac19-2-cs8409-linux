# Sanitized post-S3 trace summary

No raw trace or process metadata is published. The validated aggregate is:

| Observation | Result |
|---|---:|
| `azx_pcm_trigger cmd=1` | observed |
| `snd_hdac_stream_start tag=1` | observed |
| `azx_get_position` | 497 |
| sustained advancement | observed |
| normal buffer wrap | observed |
| `azx_pcm_trigger cmd=0` | observed |
| relevant events | 500 |
| overrun | 0 |
| commit overrun | 0 |
| dropped events | 0 |

The rate of position change was approximately consistent with 44.1-kHz,
two-channel, S32_LE playback. This proves controller-requested start and
reported DMA-position progress. It does not prove sample values, converter
output, ASP clocks, TAS reception, or acoustic output.

The minimal event set was:

```text
hda_controller:azx_pcm_trigger
hda:snd_hdac_stream_start
hda_controller:azx_get_position
```

No HDA command trace, function tracer, kprobe, BPF program, dynamic debug, or
controller patch was required.
