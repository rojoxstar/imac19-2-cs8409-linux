#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
candidate=${1:-$root/build/snd-hda-codec-cs8409-v10.ko}
kver=7.0.0-30-generic
canonical_sha=8e751170a1006e682a12872464dde89bd1bea08936f48d5165b96f27a241d9b3
expected_srcversion=984693ABD54C8FF1DE34E39
expected_vermagic="$kver SMP preempt mod_unload modversions "
expected_layout=0xe9196a28

[[ -f $candidate && ! -L $candidate ]] || {
	printf 'REFUSE: candidate must be a regular, non-symlink file\n' >&2
	exit 1
}
[[ $(modinfo -F srcversion "$candidate") == "$expected_srcversion" ]] || exit 1
[[ $(modinfo -F vermagic "$candidate") == "$expected_vermagic" ]] || exit 1
[[ $(modinfo -F license "$candidate") == GPL ]] || exit 1
layout=$(modprobe --show-modversions "$candidate" |
	awk '$2 == "module_layout" { print $1 }')
[[ $layout == "$expected_layout" ]] || exit 1

actual_sha=$(sha256sum "$candidate" | awk '{print $1}')
printf 'PASS: V10 metadata matches the tested kernel ABI.\n'
printf 'sha256: %s\n' "$actual_sha"
if [[ $actual_sha == "$canonical_sha" ]]; then
	printf 'binary identity: exact validated canonical build\n'
else
	printf 'binary identity: not the exact validated canonical build\n'
fi
