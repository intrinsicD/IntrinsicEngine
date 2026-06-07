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
- Status: blocked (current implementation-split slice complete locally; retirement blocked by default CPU CTest failure on 2026-06-07).
- Owner/agent: Codex.
- Branch / PR: current branch / TBD.
- Next verification step: resolve the `src/graphics/framegraph/Graphics.RenderGraph.Compiler.cpp:1565` ASan heap-buffer-overflow default-gate blocker, then rerun the default CPU gate and retire this task if clean.
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
- [ ] Existing platform tests for input and null backend remain green.
- [ ] Existing GLFW-labeled tests remain green where they are available and explicitly run on a GLFW-capable host.
- [ ] Add no new behavior tests unless the split exposes an untested contract seam or a bug.

## Docs
- [x] Update `src/platform/README.md` or platform architecture docs only if backend layout or public policy wording changes.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module imports/surfaces change.
- [x] Update this task with completion notes before retirement.

## Acceptance criteria
- [ ] Touched platform `.cppm` files expose public declarations/value types and small inline accessors only, or record justified retained exceptions.
- [ ] Any new `.cpp` implementation units are registered in CMake and compile under the `ci` preset.
- [ ] Touched `.cppm` files no longer include/import implementation-only backend headers where avoidable.
- [ ] Platform tests pass without changed expectations.
- [ ] The change preserves `platform -> core` layering and explicit backend selection.

## Progress notes
- 2026-06-07: Current implementation-split slice completed locally: moved non-trivial non-template bodies into matching `.cpp` implementation units, registered new private sources in CMake, and retained importer-visible templates, constexpr helpers, ABI structs, and small accessors in module interfaces where required.
- Focused target builds passed for the touched subsystem, and `docs/api/generated/module_inventory.md` was regenerated with `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- Retirement blocker: current-session verification on 2026-06-07 passed configure, `IntrinsicTests` build, generated-inventory, task-policy, layering, test-layout, doc-link, and diff-whitespace checks. The default CPU CTest gate failed with 159 failed tests out of 2816; the failures reproduce the existing `src/graphics/framegraph/Graphics.RenderGraph.Compiler.cpp:1565` ASan heap-buffer-overflow during render-graph compile, reached through `src/graphics/framegraph/Graphics.RenderGraph.cpp:405`, and cascade through graphics/runtime tests.

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
