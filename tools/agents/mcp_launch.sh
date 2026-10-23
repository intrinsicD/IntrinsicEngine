#!/usr/bin/env bash
# Starts one of this repository's MCP servers for any client, machine, clone or worktree.
# .mcp.json and .codex/config.toml only have to find this file (docs/architecture/agent-control-lane.md);
# it finds the repository from its own location and the programs even under a bare GUI PATH.
#   mcp_launch.sh intrinsic-sandbox|knowledge-graph|jscpd
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
PATH="$PATH:$HOME/.local/bin"

case "${1:-}" in
intrinsic-sandbox)
    exec python3 "$root/tools/agents/mcp_bridge.py"
    ;;
knowledge-graph)
    command -v graphify-mcp >/dev/null || {
        echo "graphify-mcp not found; run tools/setup/provision_knowledge_graph.sh" >&2
        exit 1
    }
    exec graphify-mcp --graph "$root/build/knowledge-graph/graphify-out/graph.json"
    ;;
jscpd)
    # Prefer jscpd on PATH; else the newest nvm install that has one.
    jscpd=$(command -v jscpd || ls -d "$HOME"/.nvm/versions/node/*/bin/jscpd 2>/dev/null | sort -V | tail -n 1 || true)
    [ -n "$jscpd" ] || {
        echo "jscpd not found; install it (agent-config ./install.sh, see its CODE-TOOLS.md)" >&2
        exit 1
    }
    # jscpd is a node script: the node beside it, else PATH's, else the newest nvm node
    # (an nvm npm installs into ~/.local/bin, where GUI clients find no node).
    PATH="$(dirname "$jscpd"):$PATH"
    newest=$(ls -d "$HOME"/.nvm/versions/node/*/bin 2>/dev/null | sort -V | tail -n 1 || true)
    command -v node >/dev/null || [ -z "$newest" ] || PATH="$newest:$PATH"
    command -v node >/dev/null || {
        echo "node not found for $jscpd; install Node.js" >&2
        exit 1
    }
    exec "$jscpd" --mcp "$root"
    ;;
*)
    echo "usage: $0 intrinsic-sandbox|knowledge-graph|jscpd" >&2
    exit 2
    ;;
esac
