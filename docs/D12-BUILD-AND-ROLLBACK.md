# D12 build and rollback

Status: **offline tooling only**. Phase B and C below are NOT authorized.
This document creates no installation command, no reboot or suspend
automation, and no privileged operation.

## Phase A - offline build (safe, no system module changes)

`scripts/build-d12-diagnostic.sh` builds the D12 diagnostic module from a
private temporary copy of frozen V10. It never patches the tracked tree.

Properties:

- Requires exactly `uname -r == 7.0.0-30-generic` and
  `/lib/modules/7.0.0-30-generic/build`; refuses anything else.
- Runs `scripts/verify-source.sh` first; aborts on any V10 hash mismatch.
- Works only inside `mktemp -d "$build_root/work.XXXXXX"`; `trap ... EXIT INT TERM`
  removes it on every path, including failures.
- Applies `diagnostics/d12-coef0-rmw-logging.patch` (`--dry-run`, then apply)
  inside the temporary copy only, then asserts the D12 marker is present.
- Builds with the established method: `make -C /lib/modules/$kver/build M=<tmp>/source W=1 modules -j4`
  (identical to `scripts/build-v10.sh`; warnings are non-fatal there and here,
  errors abort via `set -euo pipefail`).
- Fails closed on: patch failure, missing D12 marker, unexpected metadata
  (vermagic must be `"7.0.0-30-generic SMP preempt mod_unload modversions "`,
  license must be `GPL`, internal name must be `snd_hda_codec_cs8409`),
  a srcversion equal to frozen V10 (which would prove the wrong source was
  built), an unobtainable or mismatched `module_layout` CRC
  (`modprobe --show-modversions`, same method as `scripts/verify-module.sh`;
  expected `0xe9196a28`), missing output, existing output files, or any
  resolved artifact path outside `<repo>/artifacts/d12/<kernel>/`.
- Never writes beneath `/lib/modules`, `/usr/lib/modules` or `/boot`; never
  uses sudo; never runs depmod; never installs or loads anything.
- Proves the tracked `driver/` tree byte-identical before and after via a
  whole-tree digest comparison plus a final `verify-source.sh`.
- Output: `artifacts/d12/7.0.0-30-generic/snd-hda-codec-cs8409-d12.ko`
  plus `snd-hda-codec-cs8409-d12.manifest`. The `-d12` filename cannot be
  confused with production `snd-hda-codec-cs8409.ko`.

Manifest fields: git HEAD, kernel/uname, D12 patch SHA256, frozen source
hashes (`cs8409.c`, `cs8409.h`, `cs8409-tables.c`), module SHA256, srcversion,
vermagic, license, `module_layout` CRC, `installed: no`, UTC build timestamp.

## Phase B - later installation (NOT YET AUTHORIZED)

Installing the D12 module onto the running system is **not authorized** by
this document. When separately authorized, it must be a manual, human-executed
sequence with: exact artifact hash verification against the manifest, an
explicit rollback path staged first, root privileges exercised deliberately by
the operator (this repository's tooling contains no sudo), and explicit
`depmod` handling. No such commands are provided here. No automatic reboot or
suspend may be chained to installation.

## Phase C - exact V10 rollback (NOT YET AUTHORIZED)

Rollback to production V10 is **not authorized** by this document. The
rollback identity is:

| Item | Value |
|---|---|
| kernel | `7.0.0-30-generic` |
| V10 module SHA256 | `8e751170a1006e682a12872464dde89bd1bea08936f48d5165b96f27a241d9b3` |
| V10 srcversion | `984693ABD54C8FF1DE34E39` |
| V10 vermagic | `7.0.0-30-generic SMP preempt mod_unload modversions ` |
| Production module path | `/lib/modules/7.0.0-30-generic/updates/snd-hda-codec-cs8409.ko` |
| Canonical rebuild check | `scripts/verify-module.sh` (metadata + binary identity) |

A future authorized rollback restores the exact validated canonical V10 `.ko`
(verified against the SHA256 above) at the production path, refreshes module
dependency metadata as a deliberate operator step, and re-runs
`scripts/verify-module.sh` for confirmation. The precise commands belong to a
separately reviewed runbook, not this document.

## Verification performed offline

```sh
bash -n scripts/build-d12-diagnostic.sh   # syntax only
./scripts/verify-source.sh                # frozen V10 intact
./scripts/run-offline-tests.sh            # source contracts incl. D12 artifact
git diff --check                          # whitespace hygiene
```

On hosts whose kernel differs from `7.0.0-30-generic` (for example a Codespace
`6.8.0-azure` kernel) the script refuses before touching anything; that block
is the designed fail-closed behavior, not a defect.
