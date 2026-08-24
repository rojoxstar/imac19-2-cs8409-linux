#!/usr/bin/env bash
set -euo pipefail

# Offline builder for the D12 diagnostic module.
#
# Builds from a private temporary copy of frozen V10 with
# diagnostics/d12-coef0-rmw-logging.patch applied inside that copy only.
# The tracked driver/ tree is never patched in place. Nothing is installed,
# loaded or depmod'ed; no privileged command is used; nothing is written
# beneath /lib/modules, /usr/lib/modules or /boot.
#
# Fail closed on: wrong kernel, frozen-source mismatch, patch failure,
# unexpected module metadata, missing output, existing artifacts, or any
# output path outside <repo>/artifacts/d12/<kernel>/.

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
kver=7.0.0-30-generic
v10_srcversion=984693ABD54C8FF1DE34E39
expected_vermagic="$kver SMP preempt mod_unload modversions "
expected_layout=0xe9196a28
expected_license=GPL
patch_rel=diagnostics/d12-coef0-rmw-logging.patch
marker='iMac D12 diag: disable-path coef0 old='
build_root="$root/build"
output_name=snd-hda-codec-cs8409-d12.ko
manifest_name=snd-hda-codec-cs8409-d12.manifest
work=

cleanup() {
	[[ -n ${work:-} ]] || return 0
	case "$work" in
	"$build_root"/work.*) rm -rf -- "$work" ;;
	*) printf 'REFUSE: unsafe temporary build path: %s\n' "$work" >&2 ;;
	esac
}
trap cleanup EXIT INT TERM

refuse() {
	printf 'REFUSE: %s\n' "$1" >&2
	exit 1
}

driver_tree_digest() {
	(cd "$root" && find driver -type f -print0 |
		LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum | awk '{print $1}')
}

sha_of() { sha256sum "$1" | awk '{print $1}'; }

[[ $# -eq 0 ]] || { printf 'Usage: %s\n' "$0" >&2; exit 2; }

[[ $(uname -r) == "$kver" ]] ||
	refuse "running kernel is '$(uname -r)', required exact kernel is '$kver'"
[[ -d /lib/modules/$kver/build ]] ||
	refuse "exact kernel build tree unavailable: /lib/modules/$kver/build"

"$root/scripts/verify-source.sh"
for tool in make patch cp install modinfo modprobe sha256sum awk find xargs git date; do
	command -v "$tool" >/dev/null || refuse "required tool missing: $tool"
done

[[ -f $root/$patch_rel && ! -L $root/$patch_rel ]] ||
	refuse "D12 patch artifact missing or symlinked: $patch_rel"

real_root=$(cd "$root" && pwd -P)
before_digest=$(driver_tree_digest)

mkdir -p -- "$build_root"
work=$(mktemp -d "$build_root/work.XXXXXX")
cp -a -- "$root/driver/." "$work/"

patch --dry-run -d "$work" -p1 < "$root/$patch_rel" >/dev/null ||
	refuse "D12 patch does not apply cleanly to the pristine V10 copy"
patch -d "$work" -p1 < "$root/$patch_rel" >/dev/null ||
	refuse "D12 patch application failed"
grep -qF -- "$marker" "$work/source/cs8409.c" ||
	refuse "D12 instrumentation marker absent after patching"

make -C "/lib/modules/$kver/build" M="$work/source" W=1 modules -j4

built="$work/source/snd-hda-codec-cs8409.ko"
[[ -f $built && ! -L $built ]] ||
	refuse "build did not produce a regular module file"

srcversion=$(modinfo -F srcversion "$built")
vermagic=$(modinfo -F vermagic "$built")
license=$(modinfo -F license "$built")
name=$(modinfo -F name "$built")
layout=$(modprobe --show-modversions "$built" |
	awk '$2 == "module_layout" { print $1 }')

[[ $vermagic == "$expected_vermagic" ]] ||
	refuse "unexpected vermagic: '$vermagic'"
[[ $license == "$expected_license" ]] ||
	refuse "unexpected module license: '$license'"
[[ $name == snd_hda_codec_cs8409 ]] ||
	refuse "unexpected module name: '$name'"
[[ $srcversion != "$v10_srcversion" ]] ||
	refuse "srcversion equals frozen V10 ($v10_srcversion); the D12 source was not built"
[[ -n $layout && $layout =~ ^0x[0-9a-f]+$ ]] ||
	refuse "module_layout CRC unobtainable via modprobe --show-modversions"
[[ $layout == "$expected_layout" ]] ||
	refuse "module_layout CRC mismatch: '$layout' (kernel ABI differs from validated target)"

artifact_dir="$root/artifacts/d12/$kver"
mkdir -p -- "$artifact_dir"
real_artifact=$(cd "$artifact_dir" && pwd -P)
case "$real_artifact" in
"$real_root"/artifacts/*) ;;
*) refuse "resolved artifact path escaped the repository: $real_artifact" ;;
esac
case "$real_artifact" in
/lib/modules/*|/usr/lib/modules/*|/boot/*)
	refuse "forbidden output location: $real_artifact" ;;
esac

output="$artifact_dir/$output_name"
manifest="$artifact_dir/$manifest_name"
[[ ! -e $output ]] || refuse "refusing to overwrite existing artifact: ${output#$root/}"
[[ ! -e $manifest ]] ||
	refuse "refusing to overwrite existing manifest: ${manifest#$root/}"

head=$(git -C "$root" rev-parse HEAD)
patch_sha=$(sha_of "$root/$patch_rel")
module_sha=$(sha_of "$built")
frozen_cs8409_c=$(sha_of "$root/driver/source/cs8409.c")
frozen_cs8409_h=$(sha_of "$root/driver/source/cs8409.h")
frozen_tables=$(sha_of "$root/driver/source/cs8409-tables.c")
timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)

{
	printf 'artifact: %s\n' "$output_name"
	printf 'git_head: %s\n' "$head"
	printf 'kernel: %s\n' "$kver"
	printf 'uname: %s\n' "$(uname -r)"
	printf 'd12_patch_sha256: %s\n' "$patch_sha"
	printf 'frozen_driver_source_cs8409.c_sha256: %s\n' "$frozen_cs8409_c"
	printf 'frozen_driver_source_cs8409.h_sha256: %s\n' "$frozen_cs8409_h"
	printf 'frozen_driver_source_cs8409-tables.c_sha256: %s\n' "$frozen_tables"
	printf 'frozen_driver_tree_digest_before_after_match: yes\n'
	printf 'module_name: %s\n' "$name"
	printf 'module_sha256: %s\n' "$module_sha"
	printf 'srcversion: %s\n' "$srcversion"
	printf 'vermagic: %s\n' "$vermagic"
	printf 'license: %s\n' "$license"
	printf 'module_layout_crc: %s\n' "$layout"
	printf 'installed: no\n'
	printf 'built_utc: %s\n' "$timestamp"
} > "$work/D12.manifest"

install -m 0644 -- "$built" "$output"
install -m 0644 -- "$work/D12.manifest" "$manifest"
[[ -f $output && ! -L $output && -f $manifest && ! -L $manifest ]] ||
	refuse "artifact copy verification failed"
[[ $(sha_of "$output") == "$module_sha" ]] ||
	refuse "copied artifact hash differs from built module"

after_digest=$(driver_tree_digest)
[[ $after_digest == "$before_digest" ]] ||
	refuse "tracked driver/ tree changed during the build"
"$root/scripts/verify-source.sh"

printf 'PASS: offline D12 diagnostic module built; nothing installed or loaded.\n'
printf 'output: %s\n' "${output#$root/}"
printf 'manifest: %s\n' "${manifest#$root/}"
printf 'sha256: %s\nsrcversion: %s\nvermagic: %s\nmodule_layout: %s\n' \
	"$module_sha" "$srcversion" "$vermagic" "$layout"
printf 'tracked driver/ byte-identical before and after: yes\n'
