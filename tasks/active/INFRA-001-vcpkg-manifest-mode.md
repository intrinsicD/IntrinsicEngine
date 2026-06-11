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
- [x] Add `vcpkg-configuration.json` registering custom overlays (e.g. for the
      ImGui docking branch). Binary cache sources are configured through
      `VCPKG_BINARY_SOURCES` / CI wiring because vcpkg does not make them a
      manifest-configuration field.
- [x] Add a bootstrap script `tools/setup/bootstrap_vcpkg.sh` that clones
      `microsoft/vcpkg` into `external/vcpkg/` at the baseline commit and
      runs `bootstrap-vcpkg.sh -disableMetrics`.
- [x] Update `CMakePresets.json` to set
      `CMAKE_TOOLCHAIN_FILE=${sourceDir}/external/vcpkg/scripts/buildsystems/vcpkg.cmake`
      and `VCPKG_MANIFEST_MODE=ON` for the `ci` preset (and any future
      presets).
- [x] Route the default `cmake/Dependencies.cmake` path through:
    - `find_package` calls for each dependency.
    - Repository-facing compatibility targets for vcpkg package names that
      differ from the old FetchContent targets.
    - An ImGui overlay-port helper where vcpkg provides `imgui::imgui` and the
      repository-owned `imgui_lib` still compiles GLFW/Vulkan backend source
      files without pulling the Vulkan SDK from vcpkg.
    - Removal of `intrinsic_make_available`, `intrinsic_populate_source`,
      `intrinsic_validate_dependency_source`, lock helpers, and
      `INTRINSIC_OFFLINE_DEPS` / `INTRINSIC_UPDATE_DEPS` /
      `INTRINSIC_DEPS_SEAL` knobs (deprecation fallback remains until Slice D).
- [x] Wire a vcpkg binary cache for CI:
    - GitHub Actions: `actions/cache` keyed on the manifest hash +
      vcpkg baseline; OR `vcpkg` GHA binary cache provider.
    - Document a manual local cache path under `external/vcpkg-bincache/`
      for offline / repeat developer builds.
- [ ] Retire `external/cache/` (delete `tools/setup/populate_deps.sh`
      once vcpkg is the single source of truth and no developer flows
      depend on the old layout).
- [x] Update `cmake/IntrinsicModule.cmake` only if a vcpkg port name
      differs from the current target alias; provide an alias shim if so.

## Tests
- [ ] Cold-clone integration test (CI job or scripted):
      `git clean -fdx && tools/setup/bootstrap_vcpkg.sh && cmake --preset ci`
      completes in < 60 s with a populated binary cache and < 10 min
      without one. Capture wall-clock numbers in the PR.
- [x] `cmake --build --preset ci --target IntrinsicTests` followed by
      the default CPU correctness gate
      (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`)
      is green.
- [x] Vulkan GPU smoke gate (`IntrinsicGraphicsVulkanSmokeTests`) is
      green on a Vulkan-capable host.
- [ ] Headless build with `INTRINSIC_HEADLESS_NO_GLFW=ON` succeeds
      without pulling `glfw3` / `imguizmo` from vcpkg (manifest
      feature-flag gates them).

## Docs
- [x] Replace the dependency section of `docs/build-troubleshooting.md`
      with the vcpkg workflow (bootstrap, manifest edits, binary cache,
      version bumps via `vcpkg x-update-baseline`).
- [x] Update `AGENTS.md` §5 "Coding rules" to drop the
      `INTRINSIC_OFFLINE_DEPS` / `external/cache/` mention and point at
      the vcpkg toolchain file instead.
- [x] Add an ADR under `docs/adr/` recording the move from FetchContent
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
- Current slice: Slice C — CI binary-cache wiring + timing/smoke evidence.
  Slice B cut over the default preset path: it
  now uses vcpkg manifest mode and chainloads `cmake/IntrinsicClangToolchain.cmake`.
- The old FetchContent code path remains under `INTRINSIC_USE_VCPKG_DEPS=OFF`
  only for the Slice D deprecation window; new dependency work must not add to
  `external/cache/`.
- GitHub Actions workflows now cache `external/vcpkg-bincache/`, bootstrap
  `external/vcpkg/`, and export `VCPKG_BINARY_SOURCES` before configure. The
  cache key covers `vcpkg.json`, `vcpkg-configuration.json`, `tools/vcpkg/**`,
  and `tools/setup/bootstrap_vcpkg.sh`.
- Local timing evidence so far: vcpkg cold install/configure without a populated
  binary cache took 103.2 s for `ci` and 63.3 s for `ci-vulkan` on this host;
  subsequent warm configure with the installed tree present took 1.9 s. CI
  warm-cache timing remains to be observed on GitHub Actions.
- Vulkan smoke passed locally under the `ci-vulkan` preset. Final
  FetchContent-helper deletion remains deferred to Slice D.
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
- Slice B verification 2026-06-11:
    - `external/vcpkg/vcpkg format-manifest vcpkg.json
      tools/vcpkg/overlay-ports/imgui/vcpkg.json` — passed.
    - `external/vcpkg/vcpkg install --dry-run --no-print-usage` — passed;
      the default plan uses the repository ImGui overlay and includes the
      `windowing` feature dependencies.
    - `external/vcpkg/vcpkg install --dry-run --x-no-default-features
      --no-print-usage` — passed; the headless plan excludes `glfw3`,
      `imguizmo`, `volk`, and `vulkan-memory-allocator`.
    - `cmake --preset ci` — passed with the vcpkg toolchain and Clang 23
      chainload.
    - `cmake --build --preset ci --target IntrinsicTests` — passed; the
      aggregate now builds `IntrinsicBenchmarkSmoke` before CTest.
    - `ctest --test-dir build/ci --output-on-failure -LE
      'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — passed,
      2967/2967.
    - `ctest --test-dir build/ci --output-on-failure -R
      '^IntrinsicBenchmarkSmoke\.' --timeout 60` — passed, including result
      validation.
    - `python3 tools/benchmark/validate_benchmark_manifests.py` — passed.
    - `python3 tools/benchmark/validate_benchmark_results.py --root
      build/ci/benchmark/IntrinsicBenchmarkSmokeTest --strict` — passed.
    - `cmake -S . -B build/ci-fetchcontent-fallback -G Ninja
      -DCMAKE_TOOLCHAIN_FILE=cmake/IntrinsicClangToolchain.cmake
      -DCMAKE_BUILD_TYPE=Debug -DINTRINSIC_BUILD_SANDBOX=OFF
      -DINTRINSIC_BUILD_TESTS=ON -DINTRINSIC_ENABLE_CUDA=OFF
      -DINTRINSIC_ENABLE_SANITIZERS=ON -DINTRINSIC_USE_VCPKG_DEPS=OFF` —
      passed; FetchContent fallback remains configurable for the deprecation
      window.
    - Structural/documentation checks passed:
      `check_task_policy.py --strict`, `check_task_state_links.py --strict`,
      `generate_session_brief.py --check`, `check_doc_links.py`,
      `check_docs_sync.py --diff-mode --base-ref origin/main`,
      `sync_skills.py --check`, `check_layering.py --strict`,
      `check_test_layout.py --strict`, `check_root_hygiene.py --root .`, and
      `git diff --check`. Root hygiene still reports only the pre-existing
      `.agents/` and `imgui.ini` warning-mode entries.
- Slice C verification 2026-06-11:
    - `VCPKG_BINARY_SOURCES="clear;files,$PWD/external/vcpkg-bincache,readwrite"
      cmake --preset ci-vulkan` — passed from a clean `build/ci-vulkan` and
      `external/vcpkg-installed/ci-vulkan`; empty binary cache restored 0
      packages and configure completed in 63.3 s.
    - `VCPKG_BINARY_SOURCES="clear;files,$PWD/external/vcpkg-bincache,readwrite"
      cmake --build --preset ci-vulkan --target ExtrinsicSandbox
      IntrinsicTests` — passed in 9:27.05.
    - `ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu'
      -L 'vulkan' -LE 'slow|flaky-quarantine' --no-tests=ignore
      --timeout 120 -j$(nproc)` — passed; 38 selected, 0 failed, 1 skipped,
      elapsed 21.42 s.

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
