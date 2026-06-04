#!/usr/bin/env bash
# Smoke test for agentkit: scaffold a throwaway repo and run the generated
# workflow against itself. Exercises init, rendering, the shipped checks,
# doctor, and new-task. Exits non-zero on any failure.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
AK=("python3" "${HERE}/agentkit.py")
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "==> init"
"${AK[@]}" init --path "$WORK" --name "Selftest Project" --slug selftest --language Python >/dev/null

echo "==> assert no unrendered placeholders"
if grep -rIn '{{[A-Z]' "$WORK" >/dev/null 2>&1; then
    echo "FAIL: unrendered placeholders:" >&2
    grep -rIn '{{[A-Z]' "$WORK" >&2
    exit 1
fi

echo "==> check --strict"
"${AK[@]}" check --path "$WORK" --strict >/dev/null

echo "==> doctor (must report complete)"
"${AK[@]}" doctor --path "$WORK" | grep -q "missing: 0" || { echo "FAIL: doctor found missing files" >&2; exit 1; }

echo "==> new-task + re-check"
"${AK[@]}" new-task --path "$WORK" FEAT-001 "First feature" >/dev/null
"${AK[@]}" check --path "$WORK" --strict >/dev/null

echo "PASS: agentkit selftest"
