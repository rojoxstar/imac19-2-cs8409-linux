#!/usr/bin/env bash
set -Eeuo pipefail

# Build the exact D12 existing-read diagnostic from frozen V10. This script
# never installs, loads, depmods, or touches /boot. It performs one V10 control
# build and two clean D12 builds at the same stable source path, then publishes
# only after byte reproducibility and metadata checks pass.

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
kver=7.0.0-30-generic
expected_v10_srcversion=984693ABD54C8FF1DE34E39
expected_vermagic="$kver SMP preempt mod_unload modversions "
expected_layout=0xe9196a28
expected_license=GPL
expected_name=snd_hda_codec_cs8409
expected_patch_sha=a6bd43649482e51e212467e8ff1e322cef9f7ebe2fd703291ba528e3a9b26b2c
expected_patched_c_sha=7047e1c104ac10a43b05d33b27fd8b7ff1bf7c50701a77f4a04be0044890612e
patch_file="$root/diagnostics/d12-coef0-rmw-logging.patch"
manifest_file="$root/driver/V10-SOURCE-MANIFEST.sha256"
build_root="$root/build"
work="$build_root/.d12-canonical-work"
output_parent="$build_root/d12/$kver"
final_dir="$output_parent/candidate"
publish_stage=

refuse() {
	printf 'REFUSE: %s\n' "$*" >&2
	exit 1
}

sha_of() {
	sha256sum -- "$1" | awk '{print $1}'
}

safe_remove_tree() {
	local path=$1
	case "$path" in
	"$work"|"$work/driver"|"$output_parent"/.candidate-stage.*)
		rm -rf -- "$path"
		;;
	*)
		printf 'REFUSE cleanup outside approved build paths: %s\n' "$path" >&2
		return 1
		;;
	esac
}

cleanup() {
	local rc=$?
	trap - EXIT INT TERM
	[[ ! -e $work && ! -L $work ]] || safe_remove_tree "$work" || true
	if [[ -n ${publish_stage:-} && ( -e $publish_stage || -L $publish_stage ) ]]; then
		safe_remove_tree "$publish_stage" || true
	fi
	exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

module_field() {
	modinfo -F "$1" -- "$2" | sed 's/[[:space:]]*$//'
}

module_layout() {
	modprobe --show-modversions "$1" |
		awk '$2 == "module_layout" { print tolower($1); found=1; exit }
		     END { if (!found) exit 1 }'
}

copy_frozen_tree() {
	local destination=$1 expected rel source target

	install -d -m 0755 -- "$destination/driver"
	while read -r expected rel; do
		[[ $expected =~ ^[0-9a-f]{64}$ ]] || refuse "malformed V10 manifest hash"
		case "$rel" in
		source/*|common/*|side-codecs/*|generic.h) ;;
		*) refuse "unexpected V10 manifest path: $rel" ;;
		esac
		source="$root/driver/$rel"
		target="$destination/driver/$rel"
		[[ -f $source && ! -L $source ]] || refuse "non-regular frozen input: $rel"
		[[ $(sha_of "$source") == "$expected" ]] || refuse "frozen input hash changed: $rel"
		install -d -m 0755 -- "${target%/*}"
		install -m 0644 -- "$source" "$target"
	done < "$manifest_file"
	(
		cd "$destination/driver"
		sha256sum --check --strict "$manifest_file"
	) >/dev/null
}

verify_module() {
	local module=$1 expected_src=${2:-} actual_src

	[[ -f $module && ! -L $module ]] || refuse "module output is not a regular file"
	[[ $(module_field name "$module") == "$expected_name" ]] || refuse "module name mismatch"
	[[ $(module_field vermagic "$module") == "${expected_vermagic% }" ]] || refuse "vermagic mismatch"
	[[ $(module_field license "$module") == "$expected_license" ]] || refuse "license mismatch"
	[[ $(module_layout "$module") == "$expected_layout" ]] || refuse "module_layout mismatch"
	actual_src=$(module_field srcversion "$module")
	[[ -n $actual_src ]] || refuse "srcversion is empty"
	if [[ -n $expected_src && $actual_src != "$expected_src" ]]; then
		refuse "srcversion mismatch: $actual_src"
	fi
}

build_one() {
	local mode=$1 result=$2 source_path="$work/driver/source/cs8409.c"

	[[ ! -e $work/driver && ! -L $work/driver ]] || refuse "unclean per-build source path"
	copy_frozen_tree "$work"
	if [[ $mode == d12 ]]; then
		patch --batch --forward --dry-run -d "$work" -p1 < "$patch_file" >/dev/null ||
			refuse "pinned D12 patch dry-run failed"
		patch --batch --forward -d "$work" -p1 < "$patch_file" >/dev/null ||
			refuse "pinned D12 patch application failed"
		[[ $(sha_of "$source_path") == "$expected_patched_c_sha" ]] ||
			refuse "patched cs8409.c identity mismatch"
	else
		[[ $mode == v10 ]] || refuse "unknown build mode: $mode"
	fi

	make -C "/lib/modules/$kver/build" M="$work/driver/source" clean >/dev/null
	SOURCE_DATE_EPOCH=0 KBUILD_BUILD_TIMESTAMP='1970-01-01T00:00:00Z' \
		KBUILD_BUILD_USER=imac-audio KBUILD_BUILD_HOST=local \
		make -C "/lib/modules/$kver/build" M="$work/driver/source" \
		W=1 modules -j4
	verify_module "$work/driver/source/snd-hda-codec-cs8409.ko"
	install -m 0644 -- "$work/driver/source/snd-hda-codec-cs8409.ko" "$result"
	safe_remove_tree "$work/driver"
}

[[ $# -eq 0 ]] || { printf 'Usage: %s\n' "$0" >&2; exit 2; }
[[ $(uname -r) == "$kver" ]] || refuse "running kernel must be exactly $kver"
kernel_build=$(realpath -e "/lib/modules/$kver/build") ||
	refuse "exact kernel build tree is unavailable"
[[ $kernel_build == "/usr/src/linux-headers-$kver" && -d $kernel_build ]] ||
	refuse "kernel build symlink resolves outside the exact expected headers"

for tool in awk cmp date gcc git install make modinfo modprobe patch python3 \
	realpath rm sed sha256sum stat; do
	command -v "$tool" >/dev/null || refuse "required tool missing: $tool"
done

[[ -f $manifest_file && ! -L $manifest_file ]] || refuse "V10 manifest missing or symlinked"
[[ -f $patch_file && ! -L $patch_file ]] || refuse "D12 patch missing or symlinked"
[[ $(sha_of "$patch_file") == "$expected_patch_sha" ]] || refuse "D12 patch hash mismatch"
"$root/scripts/verify-source.sh"
python3 "$root/tests/test_d12_diagnostic.py"

if [[ -e $build_root || -L $build_root ]]; then
	[[ -d $build_root && ! -L $build_root ]] || refuse "build root is not a real directory"
else
	install -d -m 0755 -- "$build_root"
fi
[[ $(realpath -e "$build_root") == "$root/build" ]] || refuse "build root escaped repository"
[[ ! -e $work && ! -L $work ]] || refuse "stale D12 work path exists: $work"
[[ ! -e $final_dir && ! -L $final_dir ]] || refuse "candidate output already exists: $final_dir"

if [[ -e $build_root/d12 || -L $build_root/d12 ]]; then
	[[ -d $build_root/d12 && ! -L $build_root/d12 ]] || refuse "D12 output parent is unsafe"
else
	install -d -m 0755 -- "$build_root/d12"
fi
if [[ -e $output_parent || -L $output_parent ]]; then
	[[ -d $output_parent && ! -L $output_parent ]] || refuse "kernel output parent is unsafe"
else
	install -d -m 0755 -- "$output_parent"
fi
case "$(realpath -e "$output_parent")" in
"$root"/build/d12/*) ;;
*) refuse "resolved output parent escaped repository" ;;
esac

install -d -m 0700 -- "$work"
build_one v10 "$work/v10-control.ko"
verify_module "$work/v10-control.ko" "$expected_v10_srcversion"
build_one d12 "$work/d12-first.ko"
build_one d12 "$work/d12-second.ko"

cmp -s -- "$work/d12-first.ko" "$work/d12-second.ko" ||
	refuse "two clean D12 builds are not byte-identical"

d12_srcversion=$(module_field srcversion "$work/d12-first.ko")
[[ $d12_srcversion != "$expected_v10_srcversion" ]] ||
	refuse "D12 srcversion unexpectedly equals frozen V10"
verify_module "$work/d12-first.ko" "$d12_srcversion"
verify_module "$work/d12-second.ko" "$d12_srcversion"

v10_sha=$(sha_of "$work/v10-control.ko")
d12_sha=$(sha_of "$work/d12-first.ko")
d12_size=$(stat -c '%s' "$work/d12-first.ko")
d12_depends=$(module_field depends "$work/d12-first.ko")
d12_compiler=$(gcc --version | sed -n '1p')
head=$(git -C "$root" rev-parse HEAD)
built_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)

"$root/scripts/verify-source.sh"
python3 "$root/tests/test_d12_diagnostic.py" >/dev/null

publish_stage="$output_parent/.candidate-stage.$$"
[[ ! -e $publish_stage && ! -L $publish_stage ]] || refuse "publish stage already exists"
install -d -m 0755 -- "$publish_stage"
install -m 0644 -- "$work/d12-first.ko" \
	"$publish_stage/snd-hda-codec-cs8409-d12.ko"
install -m 0644 -- "$work/v10-control.ko" \
	"$publish_stage/v10-same-path-control.ko"
{
	printf 'format: imac19-2-d12-candidate-v2\n'
	printf 'git_head: %s\n' "$head"
	printf 'kernel: %s\n' "$kver"
	printf 'v10_control_sha256: %s\n' "$v10_sha"
	printf 'd12_patch_sha256: %s\n' "$expected_patch_sha"
	printf 'd12_patched_cs8409_c_sha256: %s\n' "$expected_patched_c_sha"
	printf 'module_sha256: %s\n' "$d12_sha"
	printf 'srcversion: %s\n' "$d12_srcversion"
	printf 'vermagic: %s\n' "${expected_vermagic% }"
	printf 'module_layout: %s\n' "$expected_layout"
	printf 'license: %s\n' "$expected_license"
	printf 'depends: %s\n' "$d12_depends"
	printf 'compiler: %s\n' "$d12_compiler"
	printf 'size: %s\n' "$d12_size"
	printf 'clean_builds: 2\n'
	printf 'byte_identical: yes\n'
	printf 'installed: no\n'
	printf 'built_utc: %s\n' "$built_utc"
} > "$publish_stage/manifest.txt"

[[ $(sha_of "$publish_stage/snd-hda-codec-cs8409-d12.ko") == "$d12_sha" ]] ||
	refuse "staged candidate hash mismatch"
[[ $(sha_of "$publish_stage/v10-same-path-control.ko") == "$v10_sha" ]] ||
	refuse "staged V10 control hash mismatch"
verify_module "$publish_stage/snd-hda-codec-cs8409-d12.ko" "$d12_srcversion"
verify_module "$publish_stage/v10-same-path-control.ko" "$expected_v10_srcversion"
mv -T -- "$publish_stage" "$final_dir"
publish_stage=

printf 'PASS: D12 built twice byte-identically from exact V10; nothing installed.\n'
printf 'candidate: %s\n' "$final_dir/snd-hda-codec-cs8409-d12.ko"
printf 'sha256: %s\n' "$d12_sha"
printf 'srcversion: %s\n' "$d12_srcversion"
printf 'vermagic: %s\n' "${expected_vermagic% }"
printf 'module_layout: %s\n' "$expected_layout"
printf 'size: %s\n' "$d12_size"
