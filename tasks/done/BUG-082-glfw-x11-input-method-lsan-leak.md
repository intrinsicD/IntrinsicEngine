---
id: BUG-082
theme: G
depends_on: []
---
# BUG-082 — GLFW X11 input-method initialization leaks under LeakSanitizer

## Status
- Completed 2026-07-14 at `CPUContracted` on branch
  `codex/arch-006-finish`. Commit: this local completion commit.
- No production GLFW lifetime or sanitizer suppression changed. The clean
  exact repro and the dedicated unsuppressed process contract both prove the
  existing process-static teardown is leak-clean, while the paired synthetic
  leak exits nonzero.

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
- The controlled clean rebuild changed no GLFW source but removed the reported
  allocation: ten consecutive exact-process repetitions passed with
  `detect_leaks=1` and used no suppression. A debugger trace then observed
  `glfwTerminate()` -> `XUnregisterIMInstantiateCallback()` -> `XCloseIM()`
  before normal process exit. Against pinned GLFW 3.4 / libX11 1.8.7, that is
  the complete owner teardown for the synchronously registered input-method
  handle. The original report is not reproducible at exact head, so neither a
  production shutdown change nor an XIM suppression is justified.

## Required changes
- [x] Reduce the failure to a focused GLFW initialize/terminate sanitizer
      contract and record whether `GLFWLifetime::~GLFWLifetime` and
      `glfwTerminate()` execute before LeakSanitizer's sweep.
- [x] Determine allocation ownership against the pinned GLFW 3.4/Xlib path and
      distinguish an engine lifetime defect from an upstream retained global.
- [x] Preserve the proven-clean engine ordering and add no suppression: the
      exact-head path reaches unregister/close and exits cleanly. Pair it with
      an unsuppressed synthetic-leak control so later regressions fail closed.

## Tests
- [x] Add a deterministic sanitizer regression that initializes and terminates
      the GLFW backend in one process and exits zero without hiding unrelated
      leaks.
- [x] Keep the default CPU-supported gate and GLFW platform contracts green.

## Docs
- [x] Record the ownership diagnosis and the no-suppression decision in
      the platform testing notes.
- [x] Update this index and the retirement log when verified.

## Acceptance criteria
- [x] The exact focused process exits zero under the sanitizer-enabled `ci`
      preset after exercising GLFW/X11 initialization.
- [x] A synthetic engine-owned leak is still detected by the same sanitizer
      configuration.
- [x] No backend-selection or production runtime semantics change unless the
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

Verification evidence (2026-07-14):
- Clang 23 configured `ci` with ASan/UBSan and the GLFW platform backend. The
  standalone helper deliberately links neither GTest nor `TestSupportObjs` and
  runs with `detect_leaks=1`, `symbolize=0`, and no suppression file.
- `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` passed. Its clean mode
  exited zero after the linker-wrapped `glfwTerminate` was called exactly once;
  its negative-control mode allocated a named 4,096-byte engine object and
  LeakSanitizer exited 86 with an exact direct-leak report. The no-display path
  reports a capability skip only after the synthetic control has run.
- Both required `ImGuiAdapterEngineWiring` processes passed with leak detection
  enabled, and the close-before-first-frame reproducer passed ten additional
  consecutive unsuppressed repetitions. The focused platform selection passed
  4/4. The exact-head default CPU gate passed 3,704/3,704 in 365.24 seconds;
  strict layering, test layout, task policy, documentation links, and diff
  checks passed.

## Forbidden changes
- Disabling LeakSanitizer globally or for the complete runtime contract binary.
- Treating passing assertions before the sanitizer report as a fixed gate.
- Mixing the fix into editor interaction or rendering work.
