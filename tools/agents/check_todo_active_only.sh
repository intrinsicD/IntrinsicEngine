#!/usr/bin/env bash
set -euo pipefail

# Compatibility wrapper during task-system migration.
# TODO(RORG-033): remove wrapper after CI and docs use tools/agents/check_task_policy.py directly.
exec python3 "$(dirname "$0")/check_task_policy.py" --root . --strict
