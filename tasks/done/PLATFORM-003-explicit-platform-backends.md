# PLATFORM-003 — Explicit platform backend modules

## Goal
Add explicit platform backend modules for null/headless and GLFW window ownership so runtime and renderer tests can use deterministic platform seams without requiring a real Vulkan window.

## Non-goals
- Do not retire or shrink `src/legacy`.
- Do not migrate legacy modules.
- Do not introduce unrelated engine features.
- Do not refactor graphics, runtime, ECS, or asset behavior beyond required platform integration seams.

## Context
- Status: completed.
- Owner/agent: Copilot.
- Completion date: 2026-04-30.
- Commit reference: pending local commit.
- PR: not assigned in this workspace.
- Follow-up tasks from deferred scope: none created in this task.
- Owning subsystem/layer: `src/platform`.
- Architecture rule: `platform` may depend on `core` only and must not import `graphics` or `runtime`.
- Existing promoted platform surface is `Extrinsic.Platform.Window` and `Extrinsic.Platform.Input`; concrete behavior is currently hidden in `LinuxGlfwVulkan` implementation files or absent in headless builds.

## Required changes
- Add explicit backend module files under `src/platform/backends/null/` and `src/platform/backends/glfw/`.
- Preserve backend-neutral public modules `Platform.IWindow.cppm` and `Platform.Input.cppm`.
- Provide a deterministic null/headless window backend selected for headless builds.
- Keep GLFW backend ownership explicit and isolate Vulkan surface creation policy from graphics/runtime imports.
- Update CMake wiring to select the null or GLFW backend explicitly.
- Add or update structural checks/tests for forbidden Platform imports.

## Tests
- Add headless/null platform unit tests.
- Add a GLFW backend smoke test behind opt-in labels.
- Ensure Platform-to-Graphics and Platform-to-Runtime imports are rejected by verification.
- New tests in this task use the `Test.<Name>.cpp` naming format documented in `tests/README.md`.

## Docs
- Update `src/platform/README.md` with current backend layout and selection policy.
- Add `tests/README.md` guidance that new C++ tests should use `Test.<Name>.cpp`, not `Test_<Name>.cpp`.
- Regenerate `docs/api/generated/module_inventory.md` after adding modules.

## Acceptance criteria
- `src/platform/backends/null/Platform.Backend.Null.cppm` exists and implements deterministic headless behavior.
- `src/platform/backends/glfw/Platform.Backend.Glfw.cppm` exists and owns GLFW window/input behavior.
- `src/platform/backends/glfw/Platform.Backend.GlfwVulkanSurface.cppm` exists and contains GLFW Vulkan surface policy without graphics/runtime imports.
- `tests/unit/platform/Test.NullPlatform.cpp`, `tests/contract/platform/Test.PlatformLayering.cpp`, and `tests/integration/platform/Test.GlfwPlatformSmoke.cpp` use the `Test.<Name>.cpp` naming format.
- Headless/null tests build and pass in a null backend configuration.
- GLFW smoke coverage is registered with opt-in labels and is excluded from the default CPU gate.
- Documentation and task verification records are synchronized.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
cmake --preset ci -DCMAKE_C_COMPILER=/usr/bin/clang-22 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-22 -DINTRINSIC_PLATFORM_BACKEND=Null -DINTRINSIC_HEADLESS_NO_GLFW=ON -DINTRINSIC_OFFLINE_DEPS=ON
cmake --build --preset ci --target ExtrinsicPlatform IntrinsicPlatformTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine|glfw' --timeout 60
ctest --test-dir build/ci --output-on-failure -L 'platform' --timeout 60

cmake --preset ci -DCMAKE_C_COMPILER=/usr/bin/clang-22 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-22 -DINTRINSIC_PLATFORM_BACKEND=Glfw -DINTRINSIC_HEADLESS_NO_GLFW=OFF -DINTRINSIC_OFFLINE_DEPS=ON
cmake --build --preset ci --target ExtrinsicPlatform IntrinsicPlatformGlfwSmokeTests
ctest --test-dir build/ci --output-on-failure -L 'glfw' --timeout 60
```

### Verification results — 2026-04-30

- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed; 22 task files, 0 findings.
- `python3 tools/repo/check_layering.py --root src --strict` — passed; 672 source files scanned, no unallowlisted violations.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed; 0 findings.
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md && python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check` — passed; inventory regenerated and check reported up-to-date.
- Null/headless configure: `cmake --preset ci -DCMAKE_C_COMPILER=/usr/bin/clang-22 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-22 -DINTRINSIC_PLATFORM_BACKEND=Null -DINTRINSIC_HEADLESS_NO_GLFW=ON -DINTRINSIC_OFFLINE_DEPS=ON` — passed. Initial attempt with preset compiler names failed because `clang-20`/`clang++-20` were not on PATH; explicit `/usr/bin/clang-22` and `/usr/bin/clang++-22` were used.
- Null/headless focused build: `cmake --build --preset ci --target ExtrinsicPlatform IntrinsicPlatformTests` — passed.
- Null/headless platform tests: `ctest --test-dir build/ci --output-on-failure -L 'platform' --timeout 60` — passed; 3/3 tests.
- Optional GLFW configure: `cmake --preset ci -DCMAKE_C_COMPILER=/usr/bin/clang-22 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-22 -DINTRINSIC_PLATFORM_BACKEND=Glfw -DINTRINSIC_HEADLESS_NO_GLFW=OFF -DINTRINSIC_OFFLINE_DEPS=ON` — passed.
- Optional GLFW focused build: `cmake --build --preset ci --target ExtrinsicPlatform IntrinsicPlatformGlfwSmokeTests` — passed.
- Optional GLFW smoke: `ctest --test-dir build/ci --output-on-failure -L 'glfw' --timeout 60` — passed; 1/1 tests.
- Broader headless aggregate build: `cmake --build --preset ci --target ExtrinsicPlatform IntrinsicPlatformTests IntrinsicTests` — failed outside this task while scanning existing runtime/graphics tests that include missing `RHI.Vulkan.hpp` in headless mode (`Test_RuntimeRHI.cpp`, `Test_AssetPipeline.cpp`, `Test_GraphicsBackend.cpp`, `Test_RenderOrchestrator.cpp`, `Test_RuntimeGraphics.cpp`, `Test_SceneManager.cpp`).
- Broader CPU CTest gate: `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine|glfw' --timeout 60` — completed with existing non-platform failures recorded in `build/ci/Testing/Temporary/LastTestsFailed.log`: `RenderExtraction.FrameContext_DeferredDeletions_SurviveSlotReuse`, render graph packet merge tests, maintenance lane GPU tests, runtime engine layering tests, and `GraphicsRenderer.NullRendererDebugDumpContainsCanonicalPassesAndDataflowOrder`. The new platform tests passed.

### Follow-up verification — 2026-04-30 test naming

- Renamed the new platform tests to `Test.<Name>.cpp` and documented that convention in `tests/README.md`.
- `cmake --preset ci -DCMAKE_C_COMPILER=/usr/bin/clang-22 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-22 -DINTRINSIC_PLATFORM_BACKEND=Null -DINTRINSIC_HEADLESS_NO_GLFW=ON -DINTRINSIC_OFFLINE_DEPS=ON && cmake --build --preset ci --target ExtrinsicPlatform IntrinsicPlatformTests && ctest --test-dir build/ci --output-on-failure -L 'platform' --timeout 60` — passed; 3/3 platform tests.
- `python3 tools/agents/validate_tasks.py --root tasks --strict && python3 tools/repo/check_test_layout.py --root . --strict && python3 tools/repo/check_layering.py --root src --strict` — passed.
- `cmake --preset ci -DCMAKE_C_COMPILER=/usr/bin/clang-22 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-22 -DINTRINSIC_PLATFORM_BACKEND=Glfw -DINTRINSIC_HEADLESS_NO_GLFW=OFF -DINTRINSIC_OFFLINE_DEPS=ON && cmake --build --preset ci --target ExtrinsicPlatform IntrinsicPlatformGlfwSmokeTests && ctest --test-dir build/ci --output-on-failure -L 'glfw' --timeout 60` — passed; 1/1 GLFW smoke tests.

### Compliance self-review — 2026-04-30

- Re-read `/AGENTS.md`, `tasks/active/README.md`, and this task file before making the task-record update.
- `python3 tools/agents/validate_tasks.py --root tasks --strict && python3 tools/repo/check_layering.py --root src --strict && python3 tools/repo/check_test_layout.py --root . --strict && python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check` — passed; task validation 0 findings, layering no unallowlisted violations, test layout 0 findings, inventory up-to-date.
- `cmake --preset ci -DCMAKE_C_COMPILER=/usr/bin/clang-22 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-22 -DINTRINSIC_PLATFORM_BACKEND=Null -DINTRINSIC_HEADLESS_NO_GLFW=ON -DINTRINSIC_OFFLINE_DEPS=ON && cmake --build --preset ci --target ExtrinsicPlatform IntrinsicPlatformTests && ctest --test-dir build/ci --output-on-failure -L 'platform' --timeout 60` — passed; 3/3 platform tests.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Starting legacy retirement or shrinking `src/legacy`.
- Migrating legacy modules.
- Adding Platform imports of Graphics or Runtime.

