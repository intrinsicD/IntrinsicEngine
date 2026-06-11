---
id: INFRA-001
theme: F
depends_on: []
---
# INFRA-001 — Move third-party dependencies to a vcpkg manifest

## Goal
- Replace the `FetchContent_*` machinery in `cmake/Dependencies.cmake` with a
  declarative `vcpkg.json` manifest plus a vcpkg toolchain wired into the
  build presets.
- Make a cold configure (no caches) complete in seconds, not minutes, by
  consuming pre-built binary packages from a shared vcpkg binary cache.
- Eliminate every remaining failure mode that currently makes
  `external/cache/` flaky: extract-races, partial populations, ad-hoc
  validators, lockfile drift, and `file(REMOVE_RECURSE)` footguns.

## Non-goals
- Replacing the Vulkan SDK pin (still installed system-wide via the LunarG
  SDK; vcpkg only manages C/C++ libraries).
- Switching package managers more than once (no Conan/CPM detour).
- Changing the layering, module structure, or any non-`cmake/` source.

## Context
- INFRA Option A (this PR) made the existing FetchContent cache
  safe and fast on a hot cache by:
    - Sealing `external/cache/` when populated (`INTRINSIC_DEPS_SEAL=ON`).
    - Removing the destructive auto-`REMOVE_RECURSE` in
      `intrinsic_validate_dependency_source`.
    - Defaulting `FETCHCONTENT_FULLY_DISCONNECTED=ON` whenever the cache
      is hot, so the hot-path configure is a no-op.
    - Providing `tools/setup/populate_deps.sh` for the one-shot online
      hydration step.
  Option A does *not* fix: cold-clone configure time (~4 min while every
  GIT_TAG is resolved + extracted), no version solver, no binary caching
  for CI, manual recovery still required when a dependency genuinely
  corrupts.
- Every dependency we currently fetch is already a first-class vcpkg port:
  `glm`, `eigen3`, `glfw3`, `entt`, `imgui[docking-experimental]`, `imguizmo`,
  `nlohmann-json`, `vulkan-memory-allocator`, `volk`, `stb`, `draco`,
  `tinygltf`, `gtest`.
- Vulkan SDK stays out of vcpkg; `find_package(Vulkan REQUIRED)` keeps
  pointing at the LunarG install via `VULKAN_SDK`.
- CI gets a shared binary cache (GHA cache or a self-hosted NuGet feed)
  so configure cost amortizes to a few seconds across the fleet.

## Required changes
- [x] Add `vcpkg.json` at the repo root with every dependency currently
      declared in `cmake/Dependencies.cmake`, pinned via
      `builtin-baseline` to a known-good vcpkg commit.
- [ ] Add `vcpkg-configuration.json` registering the binary cache and any
      custom overlays (e.g. for the ImGui docking branch).
- [x] Add a bootstrap script `tools/setup/bootstrap_vcpkg.sh` that clones
      `microsoft/vcpkg` into `external/vcpkg/` at the baseline commit and
      runs `bootstrap-vcpkg.sh -disableMetrics`.
- [ ] Update `CMakePresets.json` to set
      `CMAKE_TOOLCHAIN_FILE=${sourceDir}/external/vcpkg/scripts/buildsystems/vcpkg.cmake`
      and `VCPKG_MANIFEST_MODE=ON` for the `ci` preset (and any future
      presets).
- [ ] Strip `cmake/Dependencies.cmake` down to:
    - `find_package` calls for each dependency.
    - A `target_link_libraries` helper for ImGui where vcpkg's port
      already provides `imgui::imgui` and the docking-branch backends.
    - Removal of `intrinsic_make_available`, `intrinsic_populate_source`,
      `intrinsic_validate_dependency_source`, lock helpers, and
      `INTRINSIC_OFFLINE_DEPS` / `INTRINSIC_UPDATE_DEPS` /
      `INTRINSIC_DEPS_SEAL` knobs (deprecate, then delete).
- [ ] Wire a vcpkg binary cache for CI:
    - GitHub Actions: `actions/cache` keyed on the manifest hash +
      vcpkg baseline; OR `vcpkg` GHA binary cache provider.
    - Document a manual local cache path under `external/vcpkg-bincache/`
      for offline / repeat developer builds.
- [ ] Retire `external/cache/` (delete `tools/setup/populate_deps.sh`
      once vcpkg is the single source of truth and no developer flows
      depend on the old layout).
- [ ] Update `cmake/IntrinsicModule.cmake` only if a vcpkg port name
      differs from the current target alias; provide an alias shim if so.

## Tests
- [ ] Cold-clone integration test (CI job or scripted):
      `git clean -fdx && tools/setup/bootstrap_vcpkg.sh && cmake --preset ci`
      completes in < 60 s with a populated binary cache and < 10 min
      without one. Capture wall-clock numbers in the PR.
- [ ] `cmake --build --preset ci --target IntrinsicTests` followed by
      the default CPU correctness gate
      (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`)
      is green.
- [ ] Vulkan GPU smoke gate (`IntrinsicGraphicsVulkanSmokeTests`) is
      green on a Vulkan-capable host.
- [ ] Headless build with `INTRINSIC_HEADLESS_NO_GLFW=ON` succeeds
      without pulling `glfw3` / `imguizmo` from vcpkg (manifest
      feature-flag gates them).

## Docs
- [ ] Replace the dependency section of `docs/build-troubleshooting.md`
      with the vcpkg workflow (bootstrap, manifest edits, binary cache,
      version bumps via `vcpkg x-update-baseline`).
- [ ] Update `AGENTS.md` §5 "Coding rules" to drop the
      `INTRINSIC_OFFLINE_DEPS` / `external/cache/` mention and point at
      the vcpkg toolchain file instead.
- [ ] Add an ADR under `docs/adr/` recording the move from FetchContent
      to vcpkg (drivers: cold configure time, deterministic version
      solver, binary caching, removal of the in-tree validator + cache).
- [ ] Update `tasks/backlog/README.md` to mark INFRA Option C done
      and link to the ADR.

## Acceptance criteria
- [ ] No `FetchContent_*` calls remain in `cmake/`.
- [ ] `external/cache/` is no longer referenced anywhere in `cmake/`,
      `tools/`, `tests/`, or `docs/` (the directory itself may remain
      until the next housekeeping pass).
- [ ] A fresh `git clone` followed by the documented bootstrap commands
      configures and builds `IntrinsicTests` without any in-tree
      validator code path firing.
- [ ] CI uses a shared binary cache; warm-cache configure is ≤ 10 s.
- [ ] Version bumps flow through `vcpkg x-update-baseline` + manifest
      edit only — no hand-edits to any `GIT_TAG` strings anywhere.

## Status
- Active 2026-06-11; owner: Codex; branch: `main`.
- Current slice: Slice A — manifest + bootstrap (no behavior change). CMake
  presets and `cmake/Dependencies.cmake` still use the existing FetchContent
  path until Slice B.
- Slice A stages `vcpkg-configuration.json` with empty overlay lists. The
  binary cache remains configured by `VCPKG_BINARY_SOURCES` / CI wiring in
  Slice C because vcpkg binary sources are not a manifest configuration field.
- Next verification step: validate the vcpkg manifest/bootstrap script and keep
  the existing `ci` configure/build path green.
- Slice A verification 2026-06-11:
    - `tools/setup/bootstrap_vcpkg.sh` — passed; checked out baseline
      `06a7fdd564234908731c59ac46a624f808e87b1c` under ignored
      `external/vcpkg/`.
    - `external/vcpkg/vcpkg format-manifest vcpkg.json` — passed.
    - `external/vcpkg/vcpkg install --dry-run --no-print-usage` — passed; the
      default plan includes the `windowing` feature dependencies.
    - `external/vcpkg/vcpkg install --dry-run --x-no-default-features
      --no-print-usage` — passed; the headless plan excludes `glfw3`,
      `imguizmo`, `volk`, and `vulkan-memory-allocator`.
    - `cmake --preset ci`; `cmake --build --preset ci --target
      IntrinsicTests`; default CPU CTest gate — all passed.

## Verification
```bash
# Cold path (binary cache empty)
git clean -fdx external/ build/
tools/setup/bootstrap_vcpkg.sh
time cmake --preset ci
time cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
    -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

# Warm path (binary cache hot) — should finish in seconds
rm -rf build/ci
time cmake --preset ci

# Structural checks
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing the vcpkg migration with any unrelated refactor (no module-graph
  reshuffles, no test-layout reorgs).
- Removing `tools/setup/populate_deps.sh` or the `INTRINSIC_DEPS_SEAL`
  knob in the same PR as the vcpkg cut-over; leave them in place as a
  one-release deprecation window so contributors with hot
  `external/cache/` trees are not surprised.
- Introducing a second package manager (Conan, CPM, system apt) as a
  fallback inside the same change.

## Maturity
- Target: Operational on every supported host (Linux/macOS/Windows CI)
  and for every developer flow currently covered by the `ci` preset.
- This slice replaces a Scaffolded-quality in-tree validator with an
  Operational-quality external package manager. Closure requires the
  cold-clone test in the Tests section to pass on at least one CI host.

## Slice plan

Landing order respects the Forbidden-changes deprecation window (the
FetchContent path and `tools/setup/populate_deps.sh` survive until the
final slice):

- **Slice A — manifest + bootstrap (no behavior change).** Add `vcpkg.json`,
  `vcpkg-configuration.json`, and `tools/setup/bootstrap_vcpkg.sh`; nothing
  consumes them yet. Defers preset wiring to Slice B.
- **Slice B — preset cutover + ADR + docs.** Wire the vcpkg toolchain into
  the `ci` preset, reduce `cmake/Dependencies.cmake` to `find_package` calls
  plus the ImGui docking shim, keep the FetchContent path available for the
  deprecation window, land the ADR, and rewrite the dependency section of
  `docs/build-troubleshooting.md`. Default CPU gate green; headless build
  skips `glfw3`/`imguizmo` via manifest features.
- **Slice C — CI binary cache + timings.** Wire the GHA binary cache,
  capture cold/warm configure timings in the PR, and run the Vulkan smoke
  gate on a capable host.
- **Slice D — deprecation cleanup (separate PR, one release window later).**
  Delete the FetchContent helpers, the `INTRINSIC_OFFLINE_DEPS` /
  `INTRINSIC_UPDATE_DEPS` / `INTRINSIC_DEPS_SEAL` knobs,
  `tools/setup/populate_deps.sh`, and remaining `external/cache/`
  references; update `AGENTS.md` §5.
