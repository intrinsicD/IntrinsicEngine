#!/usr/bin/env bash
# Smoke test for agentkit: scaffold throwaway repos and run the generated
# workflow against itself. Exercises init, rendering, the vendored runner, the
# agentkit CLI, doctor, a custom contract filename, and new-task.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
AK=("python3" "${HERE}/agentkit.py")

assert_no_placeholders() {
    if grep -rIn '{{[A-Z]' "$1" >/dev/null 2>&1; then
        echo "FAIL: unrendered placeholders in $1:" >&2
        grep -rIn '{{[A-Z]' "$1" >&2
        exit 1
    fi
}

echo "==> case: default contract (AGENTS.md)"
W1="$(mktemp -d)"; trap 'rm -rf "$W1"' EXIT
"${AK[@]}" init --path "$W1" --name "Selftest Project" --slug selftest --language Python >/dev/null
assert_no_placeholders "$W1"
python3 "$W1/tools/agent/check.py" --strict >/dev/null   # vendored runner, no install
"${AK[@]}" check --path "$W1" --strict >/dev/null         # agentkit CLI
"${AK[@]}" doctor --path "$W1" | grep -q "missing: 0" || { echo "FAIL: doctor found missing files" >&2; exit 1; }
"${AK[@]}" new-task --path "$W1" FEAT-001 "First feature" >/dev/null
"${AK[@]}" check --path "$W1" --strict >/dev/null

echo "==> case: custom contract filename (CONTRACT.md)"
W2="$(mktemp -d)"; trap 'rm -rf "$W1" "$W2"' EXIT
"${AK[@]}" init --path "$W2" --name "Custom" --slug custom --contract-file CONTRACT.md >/dev/null
assert_no_placeholders "$W2"
test -f "$W2/CONTRACT.md"  || { echo "FAIL: CONTRACT.md was not written" >&2; exit 1; }
test ! -e "$W2/AGENTS.md"  || { echo "FAIL: AGENTS.md should not exist for a custom contract" >&2; exit 1; }
test -f "$W2/tools/agent/skills/custom-core/references/contract.md" || { echo "FAIL: contract mirror missing" >&2; exit 1; }
python3 "$W2/tools/agent/check.py" --strict >/dev/null   # must pass with the configured contract

echo "PASS: agentkit selftest"
