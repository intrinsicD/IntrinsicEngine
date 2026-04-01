#!/usr/bin/env bash
set -euo pipefail

# Guardrail: large UI-controller diffs must be accompanied by EditorUI contract tests.
# Usage:
#   tools/check_ui_contract_guard.sh [base_ref] [threshold]
# Defaults:
#   base_ref=origin/main (falls back to HEAD~1 if unavailable)
#   threshold=120 (added+deleted lines across guarded files)

BASE_REF="${1:-origin/main}"
THRESHOLD="${2:-120}"

if ! git rev-parse --verify --quiet "$BASE_REF" >/dev/null; then
  BASE_REF="HEAD~1"
fi

GUARDED_REGEX='^src/Runtime/EditorUI/Runtime\.EditorUI\.(InspectorController|Widgets|GeometryWorkflowController|SpatialDebugController)\.cpp$'
TEST_FILE='tests/Test_EditorUI.cpp'

mapfile -t changed_files < <(git diff --name-only "$BASE_REF"...HEAD)

ui_changed=false
for file in "${changed_files[@]}"; do
  if [[ "$file" =~ $GUARDED_REGEX ]]; then
    ui_changed=true
    break
  fi
done

if [[ "$ui_changed" == false ]]; then
  echo "UI contract guard: no guarded UI controllers changed."
  exit 0
fi

churn_total=$(git diff --numstat "$BASE_REF"...HEAD \
  | awk -v re="$GUARDED_REGEX" '$3 ~ re { add += $1; del += $2 } END { print add + del + 0 }')

test_touched=false
for file in "${changed_files[@]}"; do
  if [[ "$file" == "$TEST_FILE" ]]; then
    test_touched=true
    break
  fi
done

if (( churn_total > THRESHOLD )) && [[ "$test_touched" == false ]]; then
  echo "UI contract guard: FAIL"
  echo "  Guarded UI churn: ${churn_total} lines (> ${THRESHOLD})"
  echo "  Required: touch ${TEST_FILE} with contract/regression coverage for this PR"
  exit 1
fi

echo "UI contract guard: PASS (churn=${churn_total}, threshold=${THRESHOLD}, test_touched=${test_touched})"
