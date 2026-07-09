#!/usr/bin/env bash
#
# vcpkg egress preflight for IntrinsicEngine session setup (BUG-065).
#
# The ci/dev CMake presets chainload the repository-local vcpkg toolchain, whose
# bootstrap downloads a prebuilt tool binary (vcpkg-glibc / vcpkg-muslc) from
# GitHub release assets. Some managed/cloud environments allow git-protocol
# clones and the GitHub API but DENY general HTTPS GETs to github.com (HTTP 403).
# In those environments the tool download — and therefore every preset
# configure — fails with a cryptic `curl: (22) ... 403`, which reads as a build
# bug rather than the environment egress policy it actually is.
#
# This helper probes that exact egress class up front so callers can fail loudly
# with an actionable diagnosis. It is diagnostic only: it performs a SINGLE probe
# and never retries or routes around a policy denial (see /root/.ccr/README.md —
# 403/407 are organization egress denials; report, do not retry).
#
# Interface:
#   stdout : exactly one machine-readable status token, one of
#              ready      vcpkg is already bootstrapped locally
#              reachable  the tool download host answered (bootstrap should work)
#              blocked    the tool download host is denied by egress policy
#              unknown    reachability could not be determined (no curl/network)
#   stderr : human-readable status + (on `blocked`) the actionable diagnosis,
#            unless --quiet is given.
#   exit   : 0 for ready/reachable/unknown, 3 for blocked.
#
# Usage:
#   tools/setup/vcpkg_preflight.sh [--repo-root PATH] [--quiet]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
QUIET=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --repo-root)
            [[ $# -ge 2 ]] || { echo "--repo-root requires a value" >&2; exit 2; }
            REPO_ROOT="$2"; shift 2 ;;
        --quiet)
            QUIET=1; shift ;;
        -h|--help)
            sed -n '2,33p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)
            echo "vcpkg_preflight: unknown argument: $1" >&2; exit 2 ;;
    esac
done

VCPKG_ROOT="${VCPKG_ROOT:-${REPO_ROOT}/external/vcpkg}"

log() { [[ "$QUIET" -eq 1 ]] || printf '%s\n' "$*" >&2; }

# The release tag vcpkg's bootstrap wants, when the checkout metadata is present.
vcpkg_tool_tag() {
    local meta="${VCPKG_ROOT}/scripts/vcpkg-tool-metadata.txt"
    [[ -f "$meta" ]] || return 0
    awk -F= '/^VCPKG_TOOL_RELEASE_TAG=/{print $2; exit}' "$meta"
}

# Single diagnostic probe of the GitHub release-asset class vcpkg needs. Uses a
# one-byte ranged GET so a reachable host is not fully downloaded. Echoes the
# HTTP status code, or "000" when curl cannot run at all.
probe_release_asset_http_code() {
    command -v curl >/dev/null 2>&1 || { echo "000"; return 0; }
    local tag url code
    tag="$(vcpkg_tool_tag)"
    if [[ -n "$tag" ]]; then
        url="https://github.com/microsoft/vcpkg-tool/releases/download/${tag}/vcpkg-glibc"
    else
        url="https://github.com/microsoft/vcpkg-tool/releases/latest/download/vcpkg-glibc"
    fi
    code="$(curl -sS -o /dev/null -w '%{http_code}' --max-time 20 -L \
                -r 0-0 "$url" 2>/dev/null || true)"
    [[ -n "$code" ]] || code="000"
    echo "$code"
}

print_blocked_diagnosis() {
    local tag="$1"
    cat >&2 <<EOF
==============================================================================
  vcpkg bootstrap is blocked by this environment's network egress policy.
==============================================================================
  The ci/dev CMake presets chainload the repository-local vcpkg toolchain,
  whose bootstrap downloads a prebuilt tool binary (vcpkg-glibc, release tag
  ${tag:-<latest>}) from GitHub release assets. This environment denies general
  HTTPS GETs to github.com with HTTP 403 (git-protocol clones and the GitHub
  API still work), so that download -- and therefore \`cmake --preset ci\` and
  the CPU test gate -- cannot run in this session.

  Confirm the policy state with:
    curl -sS "\$HTTPS_PROXY/__agentproxy/status"

  Fix (environment-level; not a repository defect), see BUG-065:
    1. Allow the vcpkg tool download host in the environment network policy
       (github.com release assets / objects.githubusercontent.com), OR
    2. Pre-bake vcpkg into the environment snapshot: run
       tools/setup/bootstrap_vcpkg.sh with network available and ship the
       bootstrapped external/vcpkg + a populated external/vcpkg-bincache, then
       export VCPKG_BINARY_SOURCES at that cache.

  Network policies are chosen per environment; see
  https://code.claude.com/docs/en/claude-code-on-the-web
==============================================================================
EOF
}

main() {
    if [[ -x "${VCPKG_ROOT}/vcpkg" ]]; then
        log "vcpkg: already bootstrapped at ${VCPKG_ROOT}/vcpkg."
        echo "ready"
        return 0
    fi

    local tag code
    tag="$(vcpkg_tool_tag)"
    code="$(probe_release_asset_http_code)"

    case "$code" in
        200|206|302)
            log "vcpkg: tool download host reachable (HTTP ${code}); bootstrap should succeed."
            echo "reachable"
            return 0 ;;
        403|407)
            [[ "$QUIET" -eq 1 ]] || print_blocked_diagnosis "$tag"
            echo "blocked"
            return 3 ;;
        000)
            log "vcpkg: reachability unknown (no curl or no network tooling); will attempt on demand."
            echo "unknown"
            return 0 ;;
        *)
            log "vcpkg: tool download host returned HTTP ${code}; bootstrap may fail (see tools/setup/bootstrap_vcpkg.sh)."
            echo "unknown"
            return 0 ;;
    esac
}

main
