# Hardware scope

## Supported and tested

| Property | Exact value | Evidence |
|---|---|---|
| Computer | Apple iMac19,2, 21.5-inch 2019 | Machine-observed |
| HDA codec | Cirrus Logic CS8409, `1013:8409` | PROVEN |
| Codec subsystem | `106b:0f00` | PROVEN |
| Historical V10 kernel | `7.0.0-30-generic` | PROVEN |
| Current local tested kernel | `7.0.0-31-generic` | OBSERVED; binary identity recorded separately |
| Internal amplifiers | Four devices at driver addresses `d8/da/dc/de` | PROVEN transaction topology |
| Playback rate | 44.1 kHz only | PROVEN source and runtime |
| PCM container | S32_LE, 32-bit maximum sample width | PROVEN source |

The four amplifier selector values are `2/0/3/1`; the exact physical
transducer mapping of every lane is not claimed here.

## Not claimed

No support claim is made for iMac18,3, another CS8409 Mac, another subsystem,
another kernel, 48-kHz playback, DSP, or capture release qualification. Some
shared upstream code remains present because this is derived from the Linux
CS8409 driver; that does not turn untested models into supported targets.

## Hardware writes

The production source retains machine-proven CS8409 ASP/TDM, CS42L83,
TAS5764L, GPIO, and two unidentified side-device sequences. Several values
have incomplete public semantic documentation. They must not be generalized
to other boards.
