#!/bin/bash
# Resync skill references from canonical IntrinsicEngine repo docs.
# Run from the repo root.
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
SKILLS="${1:-${REPO_ROOT}/tools/agents/skills}"

if [[ ! -d "$SKILLS" ]]; then
    echo "Skills dir not found: $SKILLS" >&2
    echo "Usage: $0 [/path/to/skills/root]" >&2
    exit 1
fi

declare -A MAP=(
    ["docs/agent/contract.md"]="intrinsicengine-core/references/contract.md"
    ["docs/agent/roles.md"]="intrinsicengine-core/references/roles.md"
    ["docs/agent/prompt/prompt.md"]="intrinsicengine-core/references/session-onboarding.md"
    ["docs/agent/task-format.md"]="intrinsicengine-task-workflow/references/task-format.md"
    ["docs/agent/task-maturity.md"]="intrinsicengine-task-workflow/references/task-maturity.md"
    ["tasks/templates/task.md"]="intrinsicengine-task-workflow/references/task-template.md"
    ["docs/agent/review-checklist.md"]="intrinsicengine-review/references/review-checklist.md"
    ["docs/agent/architecture-review-checklist.md"]="intrinsicengine-review/references/architecture-review-checklist.md"
    ["docs/agent/agent-output-review-checklist.md"]="intrinsicengine-review/references/agent-output-review-checklist.md"
    ["docs/agent/method-workflow.md"]="intrinsicengine-method/references/method-workflow.md"
    ["docs/agent/method-review-checklist.md"]="intrinsicengine-method/references/method-review-checklist.md"
    ["docs/agent/benchmark-workflow.md"]="intrinsicengine-benchmark/references/benchmark-workflow.md"
    ["docs/agent/benchmark-review-checklist.md"]="intrinsicengine-benchmark/references/benchmark-review-checklist.md"
    ["docs/agent/docs-sync-policy.md"]="intrinsicengine-docs-sync/references/docs-sync-policy.md"
)

for src in "${!MAP[@]}"; do
    dst="${SKILLS}/${MAP[$src]}"
    if [[ ! -f "${REPO_ROOT}/${src}" ]]; then
        echo "MISSING SOURCE: ${REPO_ROOT}/${src}" >&2
        continue
    fi
    mkdir -p "$(dirname "$dst")"
    cp "${REPO_ROOT}/${src}" "$dst"
    echo "  ${src} -> ${dst}"
done

echo "Resynced ${#MAP[@]} reference files."
