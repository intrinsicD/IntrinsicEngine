#!/usr/bin/env bash
set -euo pipefail

# Guardrail: large UI-controller diffs must be accompanied by EditorUI contract tests.
# Usage:
#   tools/check_ui_contract_guard.sh [base_ref] [threshold]
# Defaults:
#   base_ref=origin/main (falls back to merge-base/main/master/root commit)
#   threshold=120 (added+deleted lines across guarded files)

BASE_REF="${1:-origin/main}"
THRESHOLD="${2:-120}"

if ! git rev-parse --verify --quiet "$BASE_REF" >/dev/null; then
  if git rev-parse --verify --quiet "main" >/dev/null; then
    BASE_REF="$(git merge-base HEAD main)"
  elif git rev-parse --verify --quiet "master" >/dev/null; then
    BASE_REF="$(git merge-base HEAD master)"
  else
    # Last-resort fallback: compare against repo root commit to keep this a PR-level
    # guard even in shallow/offline checkouts with no remote refs.
    BASE_REF="$(git rev-list --max-parents=0 HEAD | tail -n 1)"
  fi
fi

GUARDED_REGEX='^src/Runtime/EditorUI/Runtime\.EditorUI\.(InspectorController|Widgets|GeometryWorkflowController|SpatialDebugController)\.cpp$'
GUARDED_AWK_REGEX='^src/Runtime/EditorUI/Runtime[.]EditorUI[.](InspectorController|Widgets|GeometryWorkflowController|SpatialDebugController)[.]cpp$'
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
  | awk -v re="$GUARDED_AWK_REGEX" '$3 ~ re { add += $1; del += $2 } END { print add + del + 0 }')

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
