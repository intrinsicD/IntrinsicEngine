#!/usr/bin/env bash
#
# Wait until shared session provisioning finishes.
#
# Returns 0 when a complete Clang 20+ toolchain is available or the setup done
# marker appears. Returns nonzero if provisioning failed or the timeout elapses.

set -euo pipefail

TIMEOUT_SECS=1800
DONE_MARKER="${INTRINSIC_SESSION_SETUP_DONE:-/tmp/intrinsic-session-setup.done}"
FAIL_MARKER="${INTRINSIC_SESSION_SETUP_FAILED:-/tmp/intrinsic-session-setup.failed}"
LOG="${INTRINSIC_SESSION_SETUP_LOG:-/tmp/intrinsic-session-setup.log}"

usage() {
    cat <<EOF
Wait until shared session provisioning finishes.

Usage:
  tools/setup/wait_for_agent_setup.sh [--timeout SECONDS]
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout)
            [[ $# -ge 2 ]] || { echo "--timeout requires a value" >&2; exit 2; }
            TIMEOUT_SECS="$2"; shift 2 ;;
        --timeout=*)
            TIMEOUT_SECS="${1#*=}"; shift ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

for ver in $(seq 99 -1 20); do
    if command -v "clang-${ver}" >/dev/null 2>&1 \
        && command -v "clang++-${ver}" >/dev/null 2>&1 \
        && command -v "clang-scan-deps-${ver}" >/dev/null 2>&1; then
        [[ -f "$DONE_MARKER" ]] || : >"$DONE_MARKER"
        exit 0
    fi
done

start=$(date +%s)
while :; do
    if [[ -f "$DONE_MARKER" ]]; then
        exit 0
    fi
    if [[ -f "$FAIL_MARKER" ]]; then
        echo "Session provisioning failed. Tail of $LOG:" >&2
        tail -n 40 "$LOG" >&2 || true
        exit 1
    fi
    now=$(date +%s)
    if [[ $((now - start)) -ge "$TIMEOUT_SECS" ]]; then
        echo "Timed out after ${TIMEOUT_SECS}s waiting for $DONE_MARKER." >&2
        echo "Tail of $LOG:" >&2
        tail -n 40 "$LOG" >&2 || true
        exit 124
    fi
    sleep 2
done
