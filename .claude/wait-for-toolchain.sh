#!/usr/bin/env bash
# Compatibility adapter for the shared IntrinsicEngine setup wait helper.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

exec "${PROJECT_ROOT}/tools/setup/wait_for_agent_setup.sh" "$@"
