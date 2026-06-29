#!/usr/bin/env bash
#
# Optional Knowledge Graph provisioning for agent navigation.
#
# Builds the deterministic code+method graph served by .mcp.json. If requested,
# also installs graphify with its MCP extra via uv. The graph itself is derived
# by repository Python adapters and does not need graphify, an API key, or LLM
# extraction.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LOG="${INTRINSIC_KNOWLEDGE_GRAPH_LOG:-/tmp/intrinsic-knowledge-graph.log}"
INSTALL_GRAPHIFY=1

usage() {
    cat <<EOF
Optional Knowledge Graph provisioning for agent navigation.

Builds the deterministic code+method graph served by .mcp.json. If requested,
also installs graphify with its MCP extra via uv.

Usage:
  tools/setup/provision_knowledge_graph.sh [options]

Options:
  --project-root PATH   Repository root. Defaults to this script's repo.
  --log PATH            Log path. Defaults to /tmp/intrinsic-knowledge-graph.log.
  --no-install          Do not install graphify; only build the graph artifact.
  -h, --help            Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --project-root)
            [[ $# -ge 2 ]] || { echo "--project-root requires a value" >&2; exit 2; }
            PROJECT_ROOT="$2"; shift 2 ;;
        --log)
            [[ $# -ge 2 ]] || { echo "--log requires a value" >&2; exit 2; }
            LOG="$2"; shift 2 ;;
        --no-install)
            INSTALL_GRAPHIFY=0; shift ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

mkdir -p "$(dirname "$LOG")"

if {
    echo "==> knowledge graph: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    cd "$PROJECT_ROOT"

    if [[ "$INSTALL_GRAPHIFY" -eq 1 ]]; then
        if command -v uv >/dev/null 2>&1; then
            command -v graphify-mcp >/dev/null 2>&1 || uv tool install graphifyy --with mcp
        else
            echo "uv not found; graphify CLI/MCP install skipped."
        fi
    else
        echo "graphify install skipped by --no-install."
    fi

    python3 tools/repo/build_knowledge_graph.py
    echo "==> knowledge graph: done"
} >"$LOG" 2>&1; then
    printf '[knowledge_graph] OK (log: %s)\n' "$LOG"
else
    rc=$?
    printf '[knowledge_graph] FAILED rc=%s (log: %s)\n' "$rc" "$LOG" >&2
    exit "$rc"
fi
