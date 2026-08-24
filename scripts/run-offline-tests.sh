#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
"$root/scripts/verify-source.sh"
python3 -m unittest discover -s "$root/tests" -p 'test_*.py' -v
printf 'PASS: offline source-contract tests completed; no hardware was accessed.\n'
