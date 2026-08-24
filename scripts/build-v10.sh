#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
kver=7.0.0-30-generic
expected_srcversion=984693ABD54C8FF1DE34E39
expected_vermagic="$kver SMP preempt mod_unload modversions "
expected_layout=0xe9196a28
canonical_sha=8e751170a1006e682a12872464dde89bd1bea08936f48d5165b96f27a241d9b3
build_root="$root/build"
work=

cleanup() {
	[[ -n ${work:-} ]] || return 0
	case "$work" in
	"$build_root"/work.*) rm -rf -- "$work" ;;
	*) printf 'REFUSE: unsafe temporary build path: %s\n' "$work" >&2 ;;
	esac
}
trap cleanup EXIT INT TERM

[[ $# -eq 0 ]] || {
	printf 'Usage: %s\n' "$0" >&2
	exit 2
}
[[ -d /lib/modules/$kver/build ]] || {
	printf 'REFUSE: exact kernel build tree is unavailable: %s\n' "$kver" >&2
	exit 1
}

"$root/scripts/verify-source.sh"
for tool in make cp install modinfo modprobe sha256sum awk; do
	command -v "$tool" >/dev/null || {
		printf 'REFUSE: required build tool missing: %s\n' "$tool" >&2
		exit 1
	}
done

mkdir -p -- "$build_root"
work=$(mktemp -d "$build_root/work.XXXXXX")
cp -a -- "$root/driver/." "$work/"

make -C "/lib/modules/$kver/build" M="$work/source" W=1 modules -j4
module="$work/source/snd-hda-codec-cs8409.ko"
[[ -f $module && ! -L $module ]] || {
	printf 'REFUSE: build did not produce a regular module\n' >&2
	exit 1
}

[[ $(modinfo -F srcversion "$module") == "$expected_srcversion" ]] || {
	printf 'REFUSE: srcversion mismatch\n' >&2
	exit 1
}
[[ $(modinfo -F vermagic "$module") == "$expected_vermagic" ]] || {
	printf 'REFUSE: vermagic mismatch\n' >&2
	exit 1
}
[[ $(modinfo -F license "$module") == GPL ]] || {
	printf 'REFUSE: module license mismatch\n' >&2
	exit 1
}
layout=$(modprobe --show-modversions "$module" |
	awk '$2 == "module_layout" { print $1 }')
[[ $layout == "$expected_layout" ]] || {
	printf 'REFUSE: module_layout mismatch\n' >&2
	exit 1
}

output="$build_root/snd-hda-codec-cs8409-v10.ko"
install -m 0644 -- "$module" "$output"
actual_sha=$(sha256sum "$output" | awk '{print $1}')

printf 'PASS: offline V10 module built; nothing installed or loaded.\n'
printf 'output: %s\nsha256: %s\nsrcversion: %s\n' \
	"$output" "$actual_sha" "$expected_srcversion"
if [[ $actual_sha == "$canonical_sha" ]]; then
	printf 'binary identity: exact validated canonical build\n'
else
	printf 'binary identity: build-specific; not the exact validated canonical .ko\n'
fi
