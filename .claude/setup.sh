#!/usr/bin/env bash
# IntrinsicEngine session-start hook.
#
# Provisions the C++23 toolchain (`clang-20+` / `clang-scan-deps-20+`) and
# windowing/Vulkan dev headers required by the `ci` CMake preset, then
# optionally pre-builds the core library targets so the first build in the
# session is incremental.
#
# Behavior:
#   * Fast path: if a complete Clang 20+ toolchain is already present, the hook
#     returns immediately and the agent loop starts fully provisioned.
#   * Slow path: otherwise the hook emits the async marker
#     `{"async": true, "asyncTimeout": ...}` and detaches; provisioning
#     continues in the background while the agent works in parallel.
#     Progress is logged to `/tmp/intrinsic-session-setup.log`; completion
#     state is signalled by `/tmp/intrinsic-session-setup.done` (success) or
#     `/tmp/intrinsic-session-setup.failed` (failure).
#
# Agents that genuinely need the toolchain ready (e.g. before invoking the
# `cmake --preset ci` gate) should call `.claude/wait-for-toolchain.sh` to
# block until provisioning finishes.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

LOG="/tmp/intrinsic-session-setup.log"
DONE_MARKER="/tmp/intrinsic-session-setup.done"
FAIL_MARKER="/tmp/intrinsic-session-setup.failed"

# Use sudo only when not already root.
if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
else
    SUDO="sudo"
fi

# --------------------------------------------------------------------------
# Fast path: nothing to install. Return immediately, no async, no log churn.
# --------------------------------------------------------------------------
# Fast path requires BOTH the C++23 toolchain AND the windowing/Vulkan dev
# headers consumed by the `ci-vulkan` preset + GLFW. Checking only clang would
# pass on a base image where clang-20 is preinstalled but the X11/Vulkan
# packages are not, leaving `ci-linux-clang`/`ci-vulkan`/`nightly-deep` unable
# to build. The probe stays cheap: a handful of `dpkg -s` calls.
toolchain_present() {
    local ver pkg
    local required_pkgs=(
        libvulkan-dev
        vulkan-tools
        spirv-tools
        libx11-dev
        libxcursor-dev
        libxrandr-dev
        libxinerama-dev
        libxi-dev
        libxext-dev
        libxfixes-dev
        libwayland-dev
        libxkbcommon-dev
        libgl1-mesa-dev
    )
    for ver in $(seq 99 -1 20); do
        command -v "clang-${ver}"            >/dev/null 2>&1 || continue
        command -v "clang++-${ver}"          >/dev/null 2>&1 || continue
        command -v "clang-scan-deps-${ver}"  >/dev/null 2>&1 || continue
        for pkg in "${required_pkgs[@]}"; do
            dpkg -s "$pkg" >/dev/null 2>&1 || return 1
        done
        return 0
    done
    return 1
}

if toolchain_present; then
    printf 'IntrinsicEngine: clang-20+ toolchain and windowing/Vulkan dev headers already present; skipping provisioning.\n' >"$LOG"
    : >"$DONE_MARKER"
    rm -f "$FAIL_MARKER"
    exit 0
fi

# --------------------------------------------------------------------------
# Slow path: announce async, detach IO, then provision in background.
# --------------------------------------------------------------------------
# IMPORTANT: the JSON line must be the *only* thing on stdout before we
# detach, so the harness parses it cleanly and treats the rest of this
# process as backgrounded.
printf '{"async": true, "asyncTimeout": 1800000}\n'

# Reset markers and log for this provisioning run.
rm -f "$DONE_MARKER" "$FAIL_MARKER"
: >"$LOG"

# Detach stdio so the harness treats the hook as complete.
exec </dev/null >>"$LOG" 2>&1

trap 'echo "==> Provisioning FAILED (line $LINENO, rc=$?)."; : >"$FAIL_MARKER"; exit 1' ERR

echo "========================================"
echo " IntrinsicEngine – Session Setup (async)"
echo " $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "========================================"

# --------------------------------------------------------------------------
# 1. Install system dependencies (idempotent – skips if already present)
# --------------------------------------------------------------------------
install_system_deps() {
    local missing=()

    # Build tools
    dpkg -s build-essential &>/dev/null || missing+=(build-essential)
    dpkg -s ninja-build     &>/dev/null || missing+=(ninja-build)
    dpkg -s cmake           &>/dev/null || missing+=(cmake)

    # Vulkan SDK & shader tools
    dpkg -s libvulkan-dev              &>/dev/null || missing+=(libvulkan-dev)
    dpkg -s vulkan-tools               &>/dev/null || missing+=(vulkan-tools)
    dpkg -s spirv-tools                &>/dev/null || missing+=(spirv-tools)
    dpkg -s glslc 2>/dev/null || command -v glslc &>/dev/null || missing+=(glslc)

    # X11 / windowing libraries required by GLFW
    dpkg -s libx11-dev      &>/dev/null || missing+=(libx11-dev)
    dpkg -s libxcursor-dev  &>/dev/null || missing+=(libxcursor-dev)
    dpkg -s libxrandr-dev   &>/dev/null || missing+=(libxrandr-dev)
    dpkg -s libxinerama-dev &>/dev/null || missing+=(libxinerama-dev)
    dpkg -s libxi-dev       &>/dev/null || missing+=(libxi-dev)
    dpkg -s libxext-dev     &>/dev/null || missing+=(libxext-dev)
    dpkg -s libxfixes-dev   &>/dev/null || missing+=(libxfixes-dev)
    dpkg -s libwayland-dev  &>/dev/null || missing+=(libwayland-dev)
    dpkg -s libxkbcommon-dev &>/dev/null || missing+=(libxkbcommon-dev)
    dpkg -s libgl1-mesa-dev &>/dev/null || missing+=(libgl1-mesa-dev)

    # C++23 <format> / <expected> support (GCC 14 stdlib)
    dpkg -s libstdc++-14-dev &>/dev/null || missing+=(libstdc++-14-dev)

    # Clang 20+ for C++23 modules. Try newest available; clang-20 ships in
    # Ubuntu 24.04 noble-updates/universe so no PPA is required.
    if ! dpkg -s clang-22 &>/dev/null && ! dpkg -s clang-20 &>/dev/null; then
        missing+=(clang-20)
    fi
    if ! dpkg -s clang-tools-22 &>/dev/null && ! dpkg -s clang-tools-20 &>/dev/null; then
        missing+=(clang-tools-20)
    fi

    if [ ${#missing[@]} -gt 0 ]; then
        echo "==> Installing missing packages: ${missing[*]}"
        $SUDO apt-get update -qq
        DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y -qq \
            -o Dpkg::Options::="--force-confdef" \
            -o Dpkg::Options::="--force-confold" \
            "${missing[@]}"
    else
        echo "==> All system dependencies already installed."
    fi
}

# --------------------------------------------------------------------------
# 2. Detect the best available Clang version (require 20+)
# --------------------------------------------------------------------------
detect_clang() {
    local ver
    for ver in $(seq 99 -1 20); do
        if command -v "clang-${ver}" &>/dev/null \
            && command -v "clang++-${ver}" &>/dev/null \
            && command -v "clang-scan-deps-${ver}" &>/dev/null; then
            CLANG_VER="$ver"
            CC="clang-${ver}"
            CXX="clang++-${ver}"
            SCAN_DEPS="$(command -v "clang-scan-deps-${ver}")"
            return 0
        fi
    done
    echo "ERROR: Clang 20+ not found after install_system_deps." >&2
    return 1
}

# --------------------------------------------------------------------------
# 3. Configure the CMake build (only if not already configured)
# --------------------------------------------------------------------------
configure_build() {
    detect_clang

    local cmake_args=(
        -B "$BUILD_DIR"
        -G "Ninja"
        -DCMAKE_BUILD_TYPE=Debug
        -DCMAKE_C_COMPILER="$CC"
        -DCMAKE_CXX_COMPILER="$CXX"
    )

    if [ -n "$SCAN_DEPS" ]; then
        cmake_args+=(-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="$SCAN_DEPS")
    fi

    if command -v ccache &>/dev/null; then
        cmake_args+=(
            -DCMAKE_C_COMPILER_LAUNCHER=ccache
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
        )
        echo "==> ccache detected, enabling compiler caching."
    fi

    if [ ! -f "$BUILD_DIR/build.ninja" ]; then
        # Remove stale cache from a previously failed configure — a leftover
        # CMakeCache.txt can override our compiler arguments and cause cryptic
        # "not found in PATH" errors.
        if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
            echo "==> Removing stale CMakeCache.txt (no build.ninja present)."
            rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
        fi
        echo "==> Configuring CMake build …"
        cmake "${cmake_args[@]}" "$PROJECT_ROOT"
    else
        echo "==> Build already configured (${BUILD_DIR}/build.ninja exists). Skipping configure."
    fi
}

# --------------------------------------------------------------------------
# 4. Build the project (targeted — only libraries, not test executables)
# --------------------------------------------------------------------------
build_project() {
    echo "==> Building core libraries …"
    # Build only the library targets.  Test executables are built on-demand
    # during development (e.g. `ninja IntrinsicTests`) to keep setup fast.
    if cmake --build "$BUILD_DIR" --parallel "$(nproc)" --target \
        IntrinsicCore IntrinsicGeometry IntrinsicECS IntrinsicRHI \
        IntrinsicGraphics IntrinsicInterface IntrinsicRuntime; then
        echo "==> Library build succeeded."
    else
        echo "==> Library build failed (exit $?). Tests/agents can still iterate; rerun manually." >&2
        return 1
    fi
}

# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------
install_system_deps
configure_build
build_project || true

echo "========================================"
echo " Setup complete: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "========================================"

: >"$DONE_MARKER"
exit 0
