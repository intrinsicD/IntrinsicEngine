---
id: BUG-082
theme: G
depends_on: []
---
# BUG-082 — GLFW X11 input-method initialization leaks under LeakSanitizer

## Status

- In progress on 2026-07-16; owner: Codex; branch:
  `codex/arch-006-completion`.
- A prior completed implementation was audited against this head. Only its
  four-file technical slice will be integrated; task/index history and all
  verification evidence will be regenerated locally.
- Current-head verification on 2026-07-16 is green: the dedicated live X11
  sanitizer contract passed `1/1` without a capability skip, the GLFW platform
  intersection passed `2/2`, and the default CPU-supported gate passed
  `3,788/3,788` in 398.51 seconds.

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
- A controlled clean rebuild changed no GLFW source but removed the report.
  Historical debugger evidence on the unchanged GLFW 3.4 / libX11 1.8.7 path
  observed `GLFWLifetime::~GLFWLifetime()` -> `glfwTerminate()` ->
  `XUnregisterIMInstantiateCallback()` -> `XCloseIM()` before normal exit, the
  complete owner teardown for the synchronously registered input-method
  handle. Current-head leak-on verification reproduced the clean result. The
  explicitly Null-backed UI-capture process passed once. The GLFW-backed
  close-before-first-frame process passed 10/10 with `detect_leaks=1` and no
  environment-provided suppression file. Because
  `IntrinsicRuntimeContractTests` links `TestSupportObjs` and its compiled-in
  LeakSanitizer defaults, the standalone helper below is the strictly
  unsuppressed control. No production lifetime change or XIM suppression is
  therefore justified.
- The dedicated standalone process does not link shared GTest/TestSupport
  sanitizer defaults. On the live X11 display it executed without a skip,
  proved the process-static lifetime calls wrapped `glfwTerminate()` exactly
  once before LeakSanitizer's sweep, and first required the unsuppressed named
  4,096-byte synthetic leak to exit 86 with a direct-leak report.

## Required changes
- [x] Reduce the failure to a focused GLFW initialize/terminate sanitizer
      contract and record whether `GLFWLifetime::~GLFWLifetime` and
      `glfwTerminate()` execute before LeakSanitizer's sweep.
- [x] Determine allocation ownership against the pinned GLFW 3.4/Xlib path and
      distinguish an engine lifetime defect from an upstream retained global.
- [x] Preserve the proven-clean engine ordering and add no suppression: the
      exact path reaches unregister/close and exits cleanly. Pair it with an
      unsuppressed synthetic-leak control so later regressions fail closed.

## Tests
- [x] Add a deterministic sanitizer regression that initializes and terminates
      the GLFW backend in one process and exits zero without hiding unrelated
      leaks.
- [x] Keep the default CPU-supported gate and GLFW platform contracts green.

## Docs
- [x] Record the ownership diagnosis and no-suppression decision in
      the platform testing notes.
- [ ] Update this index and the retirement log when verified.

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
cmake --build --preset ci --target IntrinsicGlfwLifecycleLsanProcess IntrinsicRuntimeContractTests
ctest --test-dir build/ci -V \
  -R '^GlfwLifecycleLsan\.EngineStaticTeardownAndLeakControl$' --timeout 60
env ASAN_OPTIONS='detect_leaks=1:symbolize=0:fast_unwind_on_malloc=0:halt_on_error=1' \
  LSAN_OPTIONS='detect_leaks=1:fast_unwind_on_malloc=0:exitcode=86' \
  build/ci/bin/IntrinsicRuntimeContractTests \
  --gtest_filter='ImGuiAdapterEngineWiring.UiCaptureSuppressesRuntimeInputConsumers'
for iteration in $(seq 1 10); do
  env ASAN_OPTIONS='detect_leaks=1:symbolize=0:fast_unwind_on_malloc=0:halt_on_error=1' \
    LSAN_OPTIONS='detect_leaks=1:fast_unwind_on_malloc=0:exitcode=86' \
    build/ci/bin/IntrinsicRuntimeContractTests \
    --gtest_filter='ImGuiAdapterEngineWiring.RunNormalizesNativeCloseBeforeFirstFrame'
done
ctest --test-dir build/ci --output-on-failure -L platform -L glfw --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Disabling LeakSanitizer globally or for the complete runtime contract binary.
- Treating passing assertions before the sanitizer report as a fixed gate.
- Mixing the fix into editor interaction or rendering work.
