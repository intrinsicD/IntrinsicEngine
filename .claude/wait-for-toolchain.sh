#!/usr/bin/env bash
# Block until the session-start hook (.claude/setup.sh) finishes provisioning
# the clang-20+ toolchain. Returns 0 on success, non-zero if provisioning
# failed or the timeout elapses.
#
# Usage:
#   .claude/wait-for-toolchain.sh                 # 30 min default timeout
#   .claude/wait-for-toolchain.sh --timeout 600   # 10 min override
#
# Intended for agents that are about to run a clang-20-gated build/test
# (e.g. `cmake --preset ci`) and want to fail fast if provisioning is broken.
set -euo pipefail

TIMEOUT_SECS=1800
while [ $# -gt 0 ]; do
    case "$1" in
        --timeout) TIMEOUT_SECS="$2"; shift 2 ;;
        --timeout=*) TIMEOUT_SECS="${1#*=}"; shift ;;
        -h|--help)
            sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

DONE_MARKER="/tmp/intrinsic-session-setup.done"
FAIL_MARKER="/tmp/intrinsic-session-setup.failed"
LOG="/tmp/intrinsic-session-setup.log"

# Fast-fast path: toolchain already present means provisioning was a no-op.
for ver in 22 21 20; do
    if command -v "clang-${ver}" >/dev/null 2>&1 \
        && command -v "clang++-${ver}" >/dev/null 2>&1 \
        && command -v "clang-scan-deps-${ver}" >/dev/null 2>&1; then
        # Still confirm the .done marker; if absent, write one so subsequent
        # callers can short-circuit too.
        [ -f "$DONE_MARKER" ] || : >"$DONE_MARKER"
        exit 0
    fi
done

start=$(date +%s)
while :; do
    if [ -f "$DONE_MARKER" ]; then
        exit 0
    fi
    if [ -f "$FAIL_MARKER" ]; then
        echo "Session provisioning failed. Tail of $LOG:" >&2
        tail -n 40 "$LOG" >&2 || true
        exit 1
    fi
    now=$(date +%s)
    if [ $((now - start)) -ge "$TIMEOUT_SECS" ]; then
        echo "Timed out after ${TIMEOUT_SECS}s waiting for $DONE_MARKER." >&2
        echo "Tail of $LOG:" >&2
        tail -n 40 "$LOG" >&2 || true
        exit 124
    fi
    sleep 2
done
