#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
manifest="$root/driver/V10-SOURCE-MANIFEST.sha256"

[[ -f $manifest && ! -L $manifest ]] || {
	printf 'REFUSE: source manifest is missing or symlinked\n' >&2
	exit 1
}

cd "$root/driver"
sha256sum --check --strict V10-SOURCE-MANIFEST.sha256
printf 'PASS: exact V10 production source and support headers verified.\n'
