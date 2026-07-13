---
id: BUG-082
theme: G
depends_on: []
---
# BUG-082 — GLFW X11 input-method initialization leaks under LeakSanitizer

## Goal
- Make a process that initializes and shuts down the GLFW/X11 platform backend
  exit cleanly under the sanitizer-enabled `ci` preset, or establish a precise
  upstream ownership diagnosis with a narrowly scoped repository remedy that
  preserves leak detection for engine allocations.

## Non-goals
- No global `detect_leaks=0`, sanitizer-gate weakening, or broad third-party
  suppression.
- No platform backend redesign or change to `Auto|Null|Glfw` selection policy.
- No UI-034 capture, registry, or editor behavior changes.

## Context
- Observed on 2026-07-13 while running the focused `UI-034` runtime contracts
  from `build/ci/bin/IntrinsicRuntimeContractTests`. All 22 selected assertions
  passed, then LeakSanitizer exited nonzero for a direct 408-byte allocation in
  `_XimOpenIM`, reached through `XRegisterIMInstantiateCallback` ->
  `_glfwInitX11` -> `glfwInit` -> `GLFWLifetime::Instanciate`.
- Exact focused repro:
  `build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='ImGuiAdapterEngineWiring.RunNormalizesNativeCloseBeforeFirstFrame'`.
  The test assertions pass, then LeakSanitizer reports the allocation and exits
  nonzero.
- The UI-034 capture and editor-visibility contracts explicitly select the Null
  backend and exit cleanly under the same sanitizer build. This isolates the
  defect from the editor changes while retaining a focused GLFW-backed runtime
  reproducer.
- `src/platform/backends/glfw/Platform.Backend.Glfw.cpp` owns one process-static
  `GLFWLifetime`; its destructor calls `glfwTerminate()`. The remaining work is
  to determine whether shutdown ordering prevents cleanup or Xlib/GLFW retains
  this allocation despite a completed terminate path.

## Required changes
- [ ] Reduce the failure to a focused GLFW initialize/terminate sanitizer
      contract and record whether `GLFWLifetime::~GLFWLifetime` and
      `glfwTerminate()` execute before LeakSanitizer's sweep.
- [ ] Determine allocation ownership against the pinned GLFW 3.4/Xlib path and
      distinguish an engine lifetime defect from an upstream retained global.
- [ ] Fix engine shutdown ordering when owned here; if upstream-owned, use only
      a symbol-specific, documented suppression that leaves engine leak
      detection enabled and is covered by a regression.

## Tests
- [ ] Add a deterministic sanitizer regression that initializes and terminates
      the GLFW backend in one process and exits zero without hiding unrelated
      leaks.
- [ ] Keep the default CPU-supported gate and GLFW platform contracts green.

## Docs
- [ ] Record the ownership diagnosis and any sanitizer suppression rationale in
      the platform testing notes.
- [ ] Update this index and the retirement log when verified.

## Acceptance criteria
- [ ] The exact focused process exits zero under the sanitizer-enabled `ci`
      preset after exercising GLFW/X11 initialization.
- [ ] A synthetic engine-owned leak is still detected by the same sanitizer
      configuration.
- [ ] No backend-selection or production runtime semantics change unless the
      diagnosis proves the current lifetime is incorrect.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='ImGuiAdapterEngineWiring.UiCaptureSuppressesRuntimeInputConsumers'
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='ImGuiAdapterEngineWiring.RunNormalizesNativeCloseBeforeFirstFrame'
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Disabling LeakSanitizer globally or for the complete runtime contract binary.
- Treating passing assertions before the sanitizer report as a fixed gate.
- Mixing the fix into editor interaction or rendering work.
