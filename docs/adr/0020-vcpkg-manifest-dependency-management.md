# ADR 0020: vcpkg manifest dependency management

- **Status:** Accepted
- **Date:** 2026-06-11
- **Owners:** IntrinsicEngine maintainers
- **Related tasks:** `INFRA-001`

## Context

The former CMake source-population path made hot configures workable, but it
still left cold clones slow and fragile: every dependency tag had to be
resolved, source trees had to be extracted locally, cache corruption recovery
was repository-owned, and CI had no shared binary package cache.

IntrinsicEngine also has a non-negotiable compiler requirement for C++23 modules:
all presets must keep selecting Clang 20+ plus matching `clang-scan-deps`. Any
package-manager cutover must preserve that toolchain path.

## Decision

Repository presets use vcpkg manifest mode. `CMakePresets.json` points the
top-level toolchain at `external/vcpkg/scripts/buildsystems/vcpkg.cmake` and
chainloads `cmake/IntrinsicClangToolchain.cmake`, so vcpkg resolves packages
while the engine still enforces its Clang module-scanning requirements.

The manifest is pinned with `builtin-baseline`; version bumps go through
`vcpkg x-update-baseline` plus manifest edits. Binary caching is configured by
`VCPKG_BINARY_SOURCES` and CI/local cache wiring, not by CMake dependency
helpers.

The Vulkan SDK remains outside vcpkg. The repository keeps
`find_package(Vulkan REQUIRED)` pointed at the host/LunarG SDK. To preserve that
boundary while still compiling Dear ImGui's GLFW/Vulkan backend sources, the
repo owns a small ImGui overlay port that builds `imgui::imgui` from the docking
branch and installs backend sources/headers for the repository-owned
`imgui_lib` target.

## Consequences

- Fresh configure uses a deterministic manifest solver and can reuse binary
  packages instead of rebuilding every dependency from source.
- The existing Clang toolchain selection and module-scanning checks remain in
  force through vcpkg chainloading.
- The ImGui overlay port is repository-owned package metadata and must be kept
  in sync when the ImGui baseline changes.
- The old FetchContent path has been retired. Dependency additions and version
  bumps now flow through `vcpkg.json`, `vcpkg-configuration.json`, and the
  repository overlay ports only.

## Alternatives Considered

- **Keep FetchContent as the primary path.** Rejected because it preserves cold
  configure time and repository-owned cache validation/recovery.
- **Enable vcpkg's upstream `imgui[vulkan-binding]` feature.** Rejected because
  it pulls the vcpkg `vulkan` port, conflicting with the repository policy that
  Vulkan SDK ownership stays system/LunarG-managed.
- **Switch package managers again.** Rejected as churn; all current third-party
  libraries have vcpkg ports or a small overlay surface.

## Validation

- `tools/setup/bootstrap_vcpkg.sh`
- `external/vcpkg/vcpkg install --dry-run --no-print-usage`
- `external/vcpkg/vcpkg install --dry-run --x-no-default-features --no-print-usage`
- `cmake --preset ci`
- `cmake --build --preset ci --target IntrinsicTests`
- Default CPU CTest gate:
  `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
