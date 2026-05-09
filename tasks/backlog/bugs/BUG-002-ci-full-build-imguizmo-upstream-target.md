# BUG-002 — CI full build compiles ImGuizmo upstream target without ImGui includes

## Goal
- Make the default `cmake --build --preset ci` build complete without attempting to compile the upstream `imguizmo` target that lacks the repository ImGui include path.

## Non-goals
- No ImGuizmo feature work or editor gizmo behavior changes.
- No broad dependency-system rewrite beyond the target/export fix needed for this failure.
- No unrelated FetchContent cache cleanup.

## Context
- Status: backlog.
- Owner/agent: unassigned.
- Observed: 2026-05-09 while collecting local CI failures from `build/ci-full-logs/build_ci_full.log`.
- Symptom: `cmake --build --preset ci` fails in `external/cache/imguizmo-build/CMakeFiles/imguizmo.dir/...` because upstream ImGuizmo sources include `imgui.h` but the upstream target only has `-I external/cache/imguizmo-src/src`.
- Expected behavior: the default CI build should build repository targets only, or any exposed ImGuizmo target should have a complete dependency relationship on ImGui.
- Impact: a full default CI build stops before repository tests and benchmark targets are available.

## Required changes
- Audit `cmake/Dependencies.cmake` and root/legacy CMake wiring for ImGuizmo target exposure.
- Prevent the upstream `imguizmo` target from being part of the default all target, or patch its target include directories/dependencies so `imgui.h` is available.
- Keep the repository-owned `imguizmo_lib`/consumer target behavior intact for editor/runtime code.
- If a CMake patch or wrapper is added, document why the upstream target is excluded or augmented.

## Tests
- Add or update a CMake/dependency regression check if feasible so the default `all` target does not include broken third-party sample/helper targets.
- Run the full default CI build after the fix.

## Docs
- Update dependency/build troubleshooting docs if the chosen fix changes how ImGuizmo is fetched or exposed.
- No architecture docs are expected unless target ownership changes.

## Acceptance criteria
- `cmake --build --preset ci` no longer fails in `external/cache/imguizmo-build/CMakeFiles/imguizmo.dir/*` with `fatal error: 'imgui.h' file not found`.
- Repository consumers that include ImGuizmo still receive both ImGuizmo and ImGui include directories.
- The fix does not introduce new layer dependencies or editor/runtime behavior changes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not vendor-edit generated `external/cache/imguizmo-src` content as the permanent fix.
- Do not silence the error by removing ImGuizmo usage from engine/editor code.
- Do not combine this dependency-target fix with unrelated CI or code refactors.

## Captured evidence
- `build/ci-full-logs/build_ci_full.log` shows failures in `ImGradient.cpp`, `ImSequencer.cpp`, `ImGuizmo.cpp`, `ImCurveEdit.cpp`, `GraphEditor.cpp`, and `ImVectorEditor.cpp` with `fatal error: 'imgui.h' file not found`.

