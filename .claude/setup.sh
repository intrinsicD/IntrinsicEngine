#!/usr/bin/env bash
# Claude SessionStart adapter for the shared IntrinsicEngine setup script.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

exec "${PROJECT_ROOT}/tools/setup/agent_session_setup.sh" --async-json "$@"
