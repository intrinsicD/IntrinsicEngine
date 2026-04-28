#!/usr/bin/env bash
# check_perf_regression.sh — Threshold-based performance regression check.
#
# Compares a benchmark JSON output against user-defined thresholds.
# Exit code 0 = pass, 1 = regression detected, 2 = usage error.
#
# Usage:
#   ./tools/benchmark/check_perf_regression.sh <benchmark.json> [--avg-ms <max>] [--p99-ms <max>] [--min-fps <min>]
#
# Examples:
#   ./tools/benchmark/check_perf_regression.sh benchmark.json --avg-ms 16.67 --p99-ms 33.33 --min-fps 60
#   ./tools/benchmark/check_perf_regression.sh benchmark.json --avg-ms 8.33

set -euo pipefail

usage() {
    echo "Usage: $0 <benchmark.json> [--avg-ms <max>] [--p99-ms <max>] [--min-fps <min>]"
    echo ""
    echo "Thresholds (all optional — only specified thresholds are checked):"
    echo "  --avg-ms   Maximum acceptable average frame time in milliseconds"
    echo "  --p99-ms   Maximum acceptable p99 frame time in milliseconds"
    echo "  --min-fps  Minimum acceptable average FPS"
    exit 2
}

if [ $# -lt 1 ]; then
    usage
fi

JSON_FILE="$1"
shift

if [ ! -f "$JSON_FILE" ]; then
    echo "ERROR: Benchmark file not found: $JSON_FILE"
    exit 2
fi

# Default thresholds: unset (skip check)
MAX_AVG_MS=""
MAX_P99_MS=""
MIN_FPS=""

while [ $# -gt 0 ]; do
    case "$1" in
        --avg-ms)
            MAX_AVG_MS="$2"; shift 2 ;;
        --p99-ms)
            MAX_P99_MS="$2"; shift 2 ;;
        --min-fps)
            MIN_FPS="$2"; shift 2 ;;
        *)
            echo "Unknown option: $1"
            usage ;;
    esac
done

# Extract a numeric value from JSON using grep+sed (no jq dependency).
# Returns the first numeric value associated with the given field name.
extract_field() {
    local field="$1"
    local value
    value=$(grep "\"${field}\"" "$JSON_FILE" | head -1 | sed 's/.*"'"${field}"'" *: *\([0-9][0-9.]*\).*/\1/')

    # Validate: must be a non-empty numeric string (integer or decimal).
    if [ -z "$value" ] || ! echo "$value" | grep -qE '^[0-9]+\.?[0-9]*$'; then
        echo ""
        return 1
    fi
    echo "$value"
}

ACTUAL_AVG_MS=$(extract_field "avgFrameTimeMs") || true
ACTUAL_P99_MS=$(extract_field "p99FrameTimeMs") || true
ACTUAL_FPS=$(extract_field "avgFPS") || true
TOTAL_FRAMES=$(extract_field "totalFrames") || true

echo "=== Performance Regression Check ==="
echo "File:   $JSON_FILE"
echo "Frames: ${TOTAL_FRAMES:-(missing)}"
echo "Avg:    ${ACTUAL_AVG_MS:-(missing)} ms (${ACTUAL_FPS:-(missing)} FPS)"
echo "P99:    ${ACTUAL_P99_MS:-(missing)} ms"
echo ""

FAILED=0

check_max() {
    local name="$1" actual="$2" threshold="$3"
    if [ -z "$actual" ]; then
        echo "FAIL: $name could not be extracted from JSON (missing or non-numeric)"
        FAILED=1
        return
    fi
    # Use awk for floating-point comparison (no bc dependency).
    if echo "$actual $threshold" | awk '{exit !($1 > $2)}'; then
        echo "FAIL: $name = ${actual} ms exceeds threshold ${threshold} ms"
        FAILED=1
    else
        echo "PASS: $name = ${actual} ms <= ${threshold} ms"
    fi
}

check_min() {
    local name="$1" actual="$2" threshold="$3"
    if [ -z "$actual" ]; then
        echo "FAIL: $name could not be extracted from JSON (missing or non-numeric)"
        FAILED=1
        return
    fi
    if echo "$actual $threshold" | awk '{exit !($1 < $2)}'; then
        echo "FAIL: $name = ${actual} below minimum ${threshold}"
        FAILED=1
    else
        echo "PASS: $name = ${actual} >= ${threshold}"
    fi
}

if [ -n "$MAX_AVG_MS" ]; then
    check_max "avgFrameTimeMs" "$ACTUAL_AVG_MS" "$MAX_AVG_MS"
fi

if [ -n "$MAX_P99_MS" ]; then
    check_max "p99FrameTimeMs" "$ACTUAL_P99_MS" "$MAX_P99_MS"
fi

if [ -n "$MIN_FPS" ]; then
    check_min "avgFPS" "$ACTUAL_FPS" "$MIN_FPS"
fi

echo ""
if [ "$FAILED" -eq 1 ]; then
    echo "RESULT: REGRESSION DETECTED"
    exit 1
else
    echo "RESULT: PASS"
    exit 0
fi
