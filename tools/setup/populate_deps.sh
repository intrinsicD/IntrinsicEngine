#!/usr/bin/env bash
#
# tools/setup/populate_deps.sh
#
# One-shot hydrator for `external/cache/`. Runs an online CMake configure
# into a throwaway build directory whose only purpose is to make every
# FetchContent dependency materialize under `external/cache/<dep>-src/`.
# After this script succeeds, every subsequent configure of the real build
# trees (build/ci, build/ci-vulkan, etc.) takes the INFRA Option A
# fast path:
#
#   * `INTRINSIC_DEPS_SEAL` defaults to ON whenever the cache is hot.
#   * `FETCHCONTENT_FULLY_DISCONNECTED` defaults to ON whenever the cache
#     is hot.
#   * `intrinsic_validate_dependency_source` never auto-deletes a cached
#     source tree.
#
# Usage:
#   tools/setup/populate_deps.sh                # hydrate from origin
#   tools/setup/populate_deps.sh --refresh      # force FetchContent updates
#   tools/setup/populate_deps.sh --preset ci    # use a specific preset
#
# Re-running with `--refresh` is the only path that contacts the network
# after the cache is hot; everything else stays fully offline.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${repo_root}"

preset="ci"
refresh="0"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --preset)
            preset="$2"; shift 2 ;;
        --refresh)
            refresh="1"; shift ;;
        -h|--help)
            sed -n '2,30p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *)
            echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

scratch_build="${repo_root}/build/_deps_bootstrap"

echo "[populate_deps] repo root      : ${repo_root}"
echo "[populate_deps] preset         : ${preset}"
echo "[populate_deps] scratch build  : ${scratch_build}"
echo "[populate_deps] refresh remotes: ${refresh}"

# Always start with a clean scratch tree so the hydrator never inherits a
# half-configured CMake cache from a previous failed attempt.
rm -rf "${scratch_build}"

extra_args=(
    -B "${scratch_build}"
    -DINTRINSIC_DEPS_SEAL=OFF
)
if [[ "${refresh}" == "1" ]]; then
    extra_args+=(-DINTRINSIC_UPDATE_DEPS=ON)
fi

# Drive configure through the existing preset so toolchain/compiler stay
# in sync; --fresh ensures a clean cache for the scratch build dir.
cmake --preset "${preset}" --fresh "${extra_args[@]}"

# Sanity check: every declared dependency that the codebase relies on must
# now have a non-empty `<dep>-src/` directory in the cache.
cache_dir="${repo_root}/external/cache"
required_deps=(glm googletest stb entt json draco tinygltf imgui)
missing=0
for dep in "${required_deps[@]}"; do
    src_dir="${cache_dir}/${dep}-src"
    if [[ ! -d "${src_dir}" ]] || [[ -z "$(ls -A "${src_dir}" 2>/dev/null)" ]]; then
        echo "[populate_deps] MISSING: ${src_dir}" >&2
        missing=$((missing + 1))
    fi
done

# glfw / volk / vma / imguizmo only land when GLFW is enabled.
if [[ "${INTRINSIC_HEADLESS_NO_GLFW:-OFF}" != "ON" ]]; then
    for dep in glfw volk vma imguizmo; do
        src_dir="${cache_dir}/${dep}-src"
        if [[ ! -d "${src_dir}" ]] || [[ -z "$(ls -A "${src_dir}" 2>/dev/null)" ]]; then
            echo "[populate_deps] MISSING (GLFW path): ${src_dir}" >&2
            missing=$((missing + 1))
        fi
    done
fi

if [[ "${missing}" -gt 0 ]]; then
    echo "[populate_deps] FAILED: ${missing} dependency cache entries missing" >&2
    exit 1
fi

echo "[populate_deps] OK — external/cache is hydrated."
echo "[populate_deps] Subsequent configures will take the sealed-cache fast path."
echo "[populate_deps] To refresh remotes later: tools/setup/populate_deps.sh --refresh"

