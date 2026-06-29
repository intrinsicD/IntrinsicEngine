#!/usr/bin/env bash
#
# Shared IntrinsicEngine session-start setup for agents and humans.
#
# Provisions the C++23 toolchain (`clang-20+` / `clang-scan-deps-20+`) and
# windowing/Vulkan dev headers required by the repository build presets, then
# optionally pre-builds core library targets so the first development build is
# incremental. The Knowledge Graph provisioning path is optional and nonfatal.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${INTRINSIC_SESSION_BUILD_DIR:-${PROJECT_ROOT}/build}"

LOG="${INTRINSIC_SESSION_SETUP_LOG:-/tmp/intrinsic-session-setup.log}"
DONE_MARKER="${INTRINSIC_SESSION_SETUP_DONE:-/tmp/intrinsic-session-setup.done}"
FAIL_MARKER="${INTRINSIC_SESSION_SETUP_FAILED:-/tmp/intrinsic-session-setup.failed}"
ASYNC_TIMEOUT_MS="${INTRINSIC_SESSION_ASYNC_TIMEOUT_MS:-1800000}"

ASYNC_JSON=0
RUN_KNOWLEDGE_GRAPH=1

usage() {
    cat <<EOF
Shared IntrinsicEngine session-start setup for agents and humans.

Provisions the C++23 toolchain and windowing/Vulkan development headers, then
optionally pre-builds core library targets.

Usage:
  tools/setup/agent_session_setup.sh [options]

Options:
  --async-json             Emit Claude-style async JSON before backgrounding.
  --skip-knowledge-graph   Do not start optional Knowledge Graph provisioning.
  --project-root PATH      Repository root. Defaults to this script's repo.
  --build-dir PATH         CMake build directory. Defaults to build/.
  --log PATH               Setup log. Defaults to /tmp/intrinsic-session-setup.log.
  -h, --help               Show this help.

Notes:
  System package installation may use sudo on Debian/Ubuntu hosts.
  This setup helper does not replace preset-based verification.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --async-json)
            ASYNC_JSON=1; shift ;;
        --skip-knowledge-graph)
            RUN_KNOWLEDGE_GRAPH=0; shift ;;
        --project-root)
            [[ $# -ge 2 ]] || { echo "--project-root requires a value" >&2; exit 2; }
            PROJECT_ROOT="$2"; shift 2 ;;
        --build-dir)
            [[ $# -ge 2 ]] || { echo "--build-dir requires a value" >&2; exit 2; }
            BUILD_DIR="$2"; shift 2 ;;
        --log)
            [[ $# -ge 2 ]] || { echo "--log requires a value" >&2; exit 2; }
            LOG="$2"; shift 2 ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ "$(id -u)" -eq 0 ]]; then
    SUDO=""
else
    SUDO="sudo"
fi

start_knowledge_graph() {
    if [[ "$RUN_KNOWLEDGE_GRAPH" -eq 0 ]]; then
        return 0
    fi

    "${PROJECT_ROOT}/tools/setup/provision_knowledge_graph.sh" \
        --project-root "$PROJECT_ROOT" \
        >/dev/null 2>&1 || true &
}

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
        command -v "clang-${ver}" >/dev/null 2>&1 || continue
        command -v "clang++-${ver}" >/dev/null 2>&1 || continue
        command -v "clang-scan-deps-${ver}" >/dev/null 2>&1 || continue
        for pkg in "${required_pkgs[@]}"; do
            dpkg -s "$pkg" >/dev/null 2>&1 || return 1
        done
        return 0
    done
    return 1
}

start_knowledge_graph

if toolchain_present; then
    printf 'IntrinsicEngine: clang-20+ toolchain and windowing/Vulkan dev headers already present; skipping provisioning.\n' >"$LOG"
    : >"$DONE_MARKER"
    rm -f "$FAIL_MARKER"
    if [[ "$ASYNC_JSON" -eq 0 ]]; then
        cat "$LOG"
    fi
    exit 0
fi

if [[ "$ASYNC_JSON" -eq 1 ]]; then
    printf '{"async": true, "asyncTimeout": %s}\n' "$ASYNC_TIMEOUT_MS"
fi

rm -f "$DONE_MARKER" "$FAIL_MARKER"
: >"$LOG"

if [[ "$ASYNC_JSON" -eq 1 ]]; then
    exec </dev/null >>"$LOG" 2>&1
else
    printf 'IntrinsicEngine setup running. Log: %s\n' "$LOG"
    exec > >(tee -a "$LOG") 2>&1
fi

trap 'rc=$?; echo "==> Provisioning FAILED (line $LINENO, rc=$rc)."; : >"$FAIL_MARKER"; exit "$rc"' ERR

echo "========================================"
echo " IntrinsicEngine - Session Setup"
echo " $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "========================================"

install_system_deps() {
    local missing=()

    dpkg -s build-essential &>/dev/null || missing+=(build-essential)
    dpkg -s ninja-build &>/dev/null || missing+=(ninja-build)
    dpkg -s cmake &>/dev/null || missing+=(cmake)

    dpkg -s libvulkan-dev &>/dev/null || missing+=(libvulkan-dev)
    dpkg -s vulkan-tools &>/dev/null || missing+=(vulkan-tools)
    dpkg -s spirv-tools &>/dev/null || missing+=(spirv-tools)
    dpkg -s glslc 2>/dev/null || command -v glslc &>/dev/null || missing+=(glslc)

    dpkg -s libx11-dev &>/dev/null || missing+=(libx11-dev)
    dpkg -s libxcursor-dev &>/dev/null || missing+=(libxcursor-dev)
    dpkg -s libxrandr-dev &>/dev/null || missing+=(libxrandr-dev)
    dpkg -s libxinerama-dev &>/dev/null || missing+=(libxinerama-dev)
    dpkg -s libxi-dev &>/dev/null || missing+=(libxi-dev)
    dpkg -s libxext-dev &>/dev/null || missing+=(libxext-dev)
    dpkg -s libxfixes-dev &>/dev/null || missing+=(libxfixes-dev)
    dpkg -s libwayland-dev &>/dev/null || missing+=(libwayland-dev)
    dpkg -s libxkbcommon-dev &>/dev/null || missing+=(libxkbcommon-dev)
    dpkg -s libgl1-mesa-dev &>/dev/null || missing+=(libgl1-mesa-dev)

    dpkg -s libstdc++-14-dev &>/dev/null || missing+=(libstdc++-14-dev)

    if ! dpkg -s clang-22 &>/dev/null && ! dpkg -s clang-20 &>/dev/null; then
        missing+=(clang-20)
    fi
    if ! dpkg -s clang-tools-22 &>/dev/null && ! dpkg -s clang-tools-20 &>/dev/null; then
        missing+=(clang-tools-20)
    fi

    if [[ ${#missing[@]} -gt 0 ]]; then
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

configure_build() {
    detect_clang

    local cmake_args=(
        -B "$BUILD_DIR"
        -G "Ninja"
        -DCMAKE_BUILD_TYPE=Debug
        -DCMAKE_C_COMPILER="$CC"
        -DCMAKE_CXX_COMPILER="$CXX"
    )

    if [[ -n "$SCAN_DEPS" ]]; then
        cmake_args+=(-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="$SCAN_DEPS")
    fi

    if command -v ccache &>/dev/null; then
        cmake_args+=(
            -DCMAKE_C_COMPILER_LAUNCHER=ccache
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
        )
        echo "==> ccache detected, enabling compiler caching."
    fi

    if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
        if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
            echo "==> Removing stale CMakeCache.txt (no build.ninja present)."
            rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
        fi
        echo "==> Configuring CMake build ..."
        cmake "${cmake_args[@]}" "$PROJECT_ROOT"
    else
        echo "==> Build already configured (${BUILD_DIR}/build.ninja exists). Skipping configure."
    fi
}

build_project() {
    echo "==> Building core libraries ..."
    if cmake --build "$BUILD_DIR" --parallel "$(nproc)" --target \
        IntrinsicCore IntrinsicGeometry IntrinsicECS IntrinsicRHI \
        IntrinsicGraphics IntrinsicInterface IntrinsicRuntime; then
        echo "==> Library build succeeded."
    else
        echo "==> Library build failed (exit $?). Tests/agents can still iterate; rerun manually." >&2
        return 1
    fi
}

install_system_deps
configure_build
build_project || true

echo "========================================"
echo " Setup complete: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "========================================"

: >"$DONE_MARKER"
exit 0
