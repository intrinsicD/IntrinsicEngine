#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-.}"
STRICT_FLAG=""

if [[ "${2:-}" == "--strict" ]]; then
  STRICT_FLAG="--strict"
fi

echo "[run_repo_hygiene_checks] Running repository hygiene checks at root: ${ROOT}"

python3 tools/repo/check_root_hygiene.py --root "${ROOT}" ${STRICT_FLAG}
python3 tools/repo/check_expected_top_level.py --root "${ROOT}" ${STRICT_FLAG}
python3 tools/docs/check_doc_links.py --root "${ROOT}" ${STRICT_FLAG}

echo "[run_repo_hygiene_checks] Completed."
