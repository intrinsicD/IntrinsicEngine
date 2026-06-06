#!/usr/bin/env bash
# Clean-workshop architecture review bundle (WORKSHOP-009).
#
# Convenience wrapper that runs the existing validators relevant to the
# clean-workshop scorecard and prints where to record findings. It adds NO new
# gate of its own; see docs/agent/clean-workshop-review.md for the scorecard and
# for when this review is required.
set -euo pipefail

ROOT="${1:-.}"
STRICT_FLAG=""
if [[ "${2:-}" == "--strict" ]]; then
  STRICT_FLAG="--strict"
fi

echo "[clean-workshop-review] Running scorecard-relevant validators at root: ${ROOT}"

# Row 1/2 — promoted layer imports + CMake link edges match AGENTS.md §2.
python3 tools/repo/check_layering.py --root "${ROOT}/src" ${STRICT_FLAG}
# Row 8 — every layering allowlist exception names an open removal owner.
python3 tools/repo/check_layering_allowlist_quality.py --root "${ROOT}" ${STRICT_FLAG}
# Row 7 — scaffold/parity closures name their follow-up maturity gate.
python3 tools/agents/check_task_policy.py --root "${ROOT}" ${STRICT_FLAG}
# Cross-doc links for any review record / follow-up task added.
python3 tools/docs/check_doc_links.py --root "${ROOT}" ${STRICT_FLAG}

echo "[clean-workshop-review] Automated rows complete."
echo "[clean-workshop-review] Score the manual rows (3-6) and record findings in:"
echo "    docs/reviews/<YYYY-MM-DD>-clean-workshop-review.md"
echo "[clean-workshop-review] Scorecard + 'when required' guidance:"
echo "    docs/agent/clean-workshop-review.md"
