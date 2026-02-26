#!/usr/bin/env bash
set -euo pipefail

TODO_FILE="TODO.md"

if [[ ! -f "$TODO_FILE" ]]; then
  echo "ERROR: $TODO_FILE not found" >&2
  exit 1
fi

# Enforce active-backlog-only semantics: no completed checkboxes and no DONE-history references.
if rg -n '^\s*-\s*\[x\]' "$TODO_FILE"; then
  echo "ERROR: $TODO_FILE contains completed checklist items ([x]). Keep only active unfinished work." >&2
  exit 1
fi

if rg -n '\bDONE\.md\b' "$TODO_FILE"; then
  echo "ERROR: $TODO_FILE must not reference DONE.md; completion history is tracked in git history." >&2
  exit 1
fi

echo "TODO.md policy check passed: active/unfinished backlog only."
