#!/usr/bin/env bash
#
# Bootstrap the repository-local vcpkg checkout for the manifest introduced by
# INFRA-001. CMake presets consume this checkout through vcpkg manifest mode.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
vcpkg_root="${VCPKG_ROOT:-${repo_root}/external/vcpkg}"
baseline="${INTRINSIC_VCPKG_BASELINE:-06a7fdd564234908731c59ac46a624f808e87b1c}"
binary_cache="${VCPKG_DEFAULT_BINARY_CACHE:-${repo_root}/external/vcpkg-bincache}"

usage() {
    sed -n '2,28p' "${BASH_SOURCE[0]}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --root)
            [[ $# -ge 2 ]] || { echo "--root requires a value" >&2; exit 2; }
            vcpkg_root="$2"; shift 2 ;;
        --baseline)
            [[ $# -ge 2 ]] || { echo "--baseline requires a value" >&2; exit 2; }
            baseline="$2"; shift 2 ;;
        --binary-cache)
            [[ $# -ge 2 ]] || { echo "--binary-cache requires a value" >&2; exit 2; }
            binary_cache="$2"; shift 2 ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

mkdir -p "$(dirname "${vcpkg_root}")" "${binary_cache}"

if [[ -e "${vcpkg_root}" && ! -d "${vcpkg_root}/.git" ]]; then
    echo "[bootstrap_vcpkg] ${vcpkg_root} exists but is not a git checkout" >&2
    exit 1
fi

if [[ ! -d "${vcpkg_root}/.git" ]]; then
    echo "[bootstrap_vcpkg] cloning microsoft/vcpkg into ${vcpkg_root}"
    git clone --depth 1 --filter=blob:none https://github.com/microsoft/vcpkg.git "${vcpkg_root}"
fi

echo "[bootstrap_vcpkg] checkout baseline ${baseline}"
if ! git -C "${vcpkg_root}" fetch --depth 1 origin "${baseline}" >/dev/null 2>&1; then
    git -C "${vcpkg_root}" fetch --depth 1 origin master
fi
if ! git -C "${vcpkg_root}" checkout --detach "${baseline}"; then
    git -C "${vcpkg_root}" fetch origin master
    git -C "${vcpkg_root}" checkout --detach "${baseline}"
fi

# Fail loudly and early when the environment's egress policy blocks the vcpkg
# tool download, instead of surfacing a cryptic `curl: (22) ... 403` from
# bootstrap-vcpkg.sh (BUG-065). Set INTRINSIC_VCPKG_FORCE=1 to attempt anyway.
if [[ "${INTRINSIC_VCPKG_FORCE:-0}" != "1" ]]; then
    if ! "${repo_root}/tools/setup/vcpkg_preflight.sh" --repo-root "${repo_root}" >/dev/null; then
        echo "[bootstrap_vcpkg] Aborting before the tool download: egress preflight failed (diagnosis above)." >&2
        echo "[bootstrap_vcpkg] Re-run with INTRINSIC_VCPKG_FORCE=1 to attempt the download regardless." >&2
        exit 3
    fi
fi

if ! "${vcpkg_root}/bootstrap-vcpkg.sh" -disableMetrics; then
    echo "[bootstrap_vcpkg] bootstrap-vcpkg.sh failed (tool download or unpack)." >&2
    "${repo_root}/tools/setup/vcpkg_preflight.sh" --repo-root "${repo_root}" >/dev/null || true
    exit 1
fi

cat <<EOF
[bootstrap_vcpkg] OK
[bootstrap_vcpkg] root          : ${vcpkg_root}
[bootstrap_vcpkg] baseline      : ${baseline}
[bootstrap_vcpkg] binary cache  : ${binary_cache}

For local binary caching, export:
  export VCPKG_BINARY_SOURCES="clear;files,${binary_cache},readwrite"

Repository presets configure CMake with:
  CMAKE_TOOLCHAIN_FILE=${vcpkg_root}/scripts/buildsystems/vcpkg.cmake
EOF
