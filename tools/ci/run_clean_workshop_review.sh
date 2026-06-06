#!/usr/bin/env bash
# Clean-workshop architecture review bundle (WORKSHOP-009).
#
# Convenience wrapper that runs the existing validators relevant to the
# clean-workshop scorecard and prints where to record findings. It adds NO new
# gate of its own; see docs/agent/clean-workshop-review.md for the scorecard and
# for when this review is required.
#
# Usage: run_clean_workshop_review.sh [REPO_ROOT] [--strict]
# REPO_ROOT defaults to the repository root derived from this script's location,
# so the bundle is runnable from any working directory.
set -euo pipefail

# Resolve the repo root: explicit first argument, else derive it from this
# script's location (<root>/tools/ci/run_clean_workshop_review.sh).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${1:-${SCRIPT_DIR}/../..}"
STRICT_FLAG=""
if [[ "${2:-}" == "--strict" ]]; then
  STRICT_FLAG="--strict"
fi

# Run from the repo root so BOTH the validator script paths (tools/...) and
# their --root values resolve regardless of the caller's working directory.
# Without this, an absolute ROOT only fixed --root while `tools/...` stayed
# relative to $PWD and the bundle failed before any check ran.
cd "${ROOT}"

echo "[clean-workshop-review] Running scorecard-relevant validators at root: $(pwd)"

# Row 1/2 — promoted layer imports + CMake link edges match AGENTS.md §2.
python3 tools/repo/check_layering.py --root src ${STRICT_FLAG}
# Row 8 — every layering allowlist exception names an open removal owner.
python3 tools/repo/check_layering_allowlist_quality.py --root . ${STRICT_FLAG}
# Row 7 — scaffold/parity closures name their follow-up maturity gate.
python3 tools/agents/check_task_policy.py --root . ${STRICT_FLAG}
# Cross-doc links for any review record / follow-up task added.
python3 tools/docs/check_doc_links.py --root . ${STRICT_FLAG}

echo "[clean-workshop-review] Automated rows complete."
echo "[clean-workshop-review] Score the manual rows (3-6) and record findings in:"
echo "    docs/reviews/<YYYY-MM-DD>-clean-workshop-review.md"
echo "[clean-workshop-review] Scorecard + 'when required' guidance:"
echo "    docs/agent/clean-workshop-review.md"
