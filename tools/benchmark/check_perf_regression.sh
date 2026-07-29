#!/usr/bin/env bash
# Compatibility entry point; the canonical comparator is schema-aware Python.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "${SCRIPT_DIR}/check_perf_regression.py" "$@"
