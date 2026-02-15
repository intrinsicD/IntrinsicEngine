#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

# Use sudo only when not already root
if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
else
    SUDO="sudo"
fi

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

    # Clang tools for C++23 module dependency scanning
    dpkg -s clang-tools-18  &>/dev/null || missing+=(clang-tools-18)

    if [ ${#missing[@]} -gt 0 ]; then
        echo "==> Installing missing packages: ${missing[*]}"
        $SUDO apt-get update -qq
        $SUDO apt-get install -y -qq "${missing[@]}"
    else
        echo "==> All system dependencies already installed."
    fi
}

# --------------------------------------------------------------------------
# 2. Detect the best available Clang version (prefer 20, fall back to 18)
# --------------------------------------------------------------------------
detect_clang() {
    for ver in 20 18; do
        if command -v "clang++-${ver}" &>/dev/null; then
            CLANG_VER="$ver"
            CC="clang-${ver}"
            CXX="clang++-${ver}"
            SCAN_DEPS="$(command -v "clang-scan-deps-${ver}" 2>/dev/null || echo "")"
            return 0
        fi
    done
    echo "ERROR: No supported Clang (18+) found. Install clang-18 or clang-20." >&2
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
        echo "==> Library build failed (exit $?). You may need to fix build errors manually."
        return 1
    fi
}

# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------
echo "========================================"
echo " IntrinsicEngine – Session Setup"
echo "========================================"

install_system_deps
configure_build
build_project || true

echo "========================================"
echo " Setup complete."
echo "========================================"
