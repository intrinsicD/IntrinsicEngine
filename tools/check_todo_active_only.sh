#!/usr/bin/env bash
set -euo pipefail

# Compatibility wrapper retained during tools path migration (RORG-071).
exec "$(dirname "$0")/agents/check_todo_active_only.sh" "$@"
