# PLATFORM-005 — Platform module implementation splits

## Goal
- Move worthy non-trivial, non-template implementations out of promoted platform `.cppm` interfaces into matching `.cpp` implementation units, and clean up includes/imports that become implementation-only.

## Non-goals
- No platform behavior changes.
- No backend selection policy changes.
- No graphics, ECS, runtime, app, RHI, or live renderer ownership in platform.
- No broad core, geometry, graphics, runtime, physics, or legacy cleanup in this task.
- No attempt to move templates or bodies that must remain visible to importers.

## Context
- Status: done (retired 2026-06-07; implementation split landed in `bfcd2751`).
- Owner/agent: Codex.
- Branch / PR: current branch / TBD.
- Next verification step: none; retired after a clean no-cache rebuild, explicit benchmark-smoke target build, and default CPU gate on 2026-06-07.
- Owning subsystem/layer: `platform` (`platform -> core` only).
- Static audit on 2026-06-06 identified these promoted platform cleanup targets:
  `src/platform/Platform.Input.cppm`,
  `src/platform/backends/glfw/Platform.Backend.Glfw.cppm`, and
  `src/platform/backends/null/Platform.Backend.Null.cppm`.
- GLFW and ImGui backend headers should stay implementation-local where possible; public platform interfaces should not expose backend details.

## Required changes
- [x] Add or reuse matching `.cpp` implementation units for `Platform.Input`, `Platform.Backend.Glfw`, and `Platform.Backend.Null`.
- [x] Move non-template input-state mutation, null-window event handling, GLFW lifetime/window event handling, clipboard/cursor helpers, and backend factory bodies out of `.cppm` where importer visibility is not required.
- [x] Register any newly added `.cpp` files as private sources in the relevant `src/platform/**/CMakeLists.txt` files.
- [x] Keep platform public interface types and simple accessors in `.cppm`.
- [x] Clean up global-module-fragment includes in touched `.cppm` files so GLFW/ImGui/backend-only headers are confined to implementation units where possible.
- [x] Audit imports in touched `.cppm` files and keep only public-surface imports in interfaces.

## Tests
- [x] Existing platform tests for input and null backend remain green.
- [x] Existing GLFW-labeled tests remain green where they are available and explicitly run on a GLFW-capable host.
- [x] Add no new behavior tests unless the split exposes an untested contract seam or a bug.

## Docs
- [x] Update `src/platform/README.md` or platform architecture docs only if backend layout or public policy wording changes.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module imports/surfaces change.
- [x] Update this task with completion notes before retirement.

## Acceptance criteria
- [x] Touched platform `.cppm` files expose public declarations/value types and small inline accessors only, or record justified retained exceptions.
- [x] Any new `.cpp` implementation units are registered in CMake and compile under the `ci` preset.
- [x] Touched `.cppm` files no longer include/import implementation-only backend headers where avoidable.
- [x] Platform tests pass without changed expectations.
- [x] The change preserves `platform -> core` layering and explicit backend selection.

## Progress notes
- 2026-06-07: Current implementation-split slice completed locally: moved non-trivial non-template bodies into matching `.cpp` implementation units, registered new private sources in CMake, and retained importer-visible templates, constexpr helpers, ABI structs, and small accessors in module interfaces where required.
- Focused target builds passed for the touched subsystem, and `docs/api/generated/module_inventory.md` was regenerated with `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- 2026-06-07 retirement verification: the previous rendergraph ASan blocker was reproduced as stale C++23 module layout state from ccache/incremental module artifacts, not a source defect in the implementation split. A `CCACHE_DISABLE=1` `IntrinsicTests` rebuild plus an explicit `IntrinsicBenchmarkSmoke` build restored a clean default CPU gate; final `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed 2816/2816.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Platform|Input|Window|Null' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# On hosts with GLFW coverage available, also run the focused GLFW-labeled platform tests.
ctest --test-dir build/ci --output-on-failure -L 'glfw' -R 'Platform|Window|Input' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not change backend selection, event semantics, cursor/clipboard behavior, minimized-window handling, or input-state transition semantics.
- Do not add dependencies from platform to graphics, RHI, ECS, runtime, app, or live renderer code.
- Do not move templates or importer-required definitions into `.cpp`.
- Do not mix this mechanical implementation split with platform behavior refactors.
