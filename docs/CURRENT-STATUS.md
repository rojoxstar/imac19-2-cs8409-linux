# Current development status

This page separates the frozen public source, later research patches, and the
locally installed binary. They are related but not interchangeable identities.

## Source lineage

The verified branch descends from public main as follows:

```text
2568e591  public S3 research head
86444f4   D12 existing-read diagnostic
e840e33   final local driver review
d6e8df4   PLL1 state-restoration patch
```

`driver/source/` remains the SHA-pinned V10 source. Commit `d6e8df4` adds
`patches/local-imac19-2-pll1-restore-diagnostic.patch`; it does not silently
rewrite that frozen tree.

## Kernel 31 observation

The exact locally installed and live module was observed as:

```text
kernel:     7.0.0-31-generic
path:       /lib/modules/7.0.0-31-generic/updates/snd-hda-codec-cs8409.ko
SHA256:     f791e1c497d797432449e828dc8c41155d5dc266c06e18c1c4acb854ce524d3a
srcversion: 4696DA5BA67978BE4BD703F
vermagic:   7.0.0-31-generic SMP preempt mod_unload modversions
```

Playback at 44.1 kHz was audible on left and right without obvious imbalance
or distortion in the controlled checks. This binary identity is a runtime
observation. The public repository does not contain the binary, and the exact
byte-for-byte build correspondence to `d6e8df4` is not proved.

## PLL1 restoration

`imac19_2_restore_pll1()` reads `CS8409_DEV_CFG1`, preserves all bits, ORs
`BIT(12)`, writes only when changed, and logs old/new/change state. It executes
only for the iMac19,2 fixup before ASP/TDM setup.

Observed S3-related states included:

```text
0x00000000 → 0x00001000
0x00008000 → 0x00009000
0x00009000 → 0x00009000
```

These observations support defensive restoration of the PLL1-enable bit. They
do not prove that the change is necessary or sufficient for S3 recovery, and
they do not establish an audible fidelity benefit. System S3 remains
unqualified.

## TAS5764L safety boundary

Channel setup writes `0xAB` to TAS register `0x04`; earlier boot/trace material
contains `0xCF`. TAS5760M documentation maps these codes to approximately
−18 dB and 0 dB, respectively, but that is an analogy to another part.
TAS5764L register semantics remain unproved. No speculative `0x04` value is a
valid contributor experiment.
