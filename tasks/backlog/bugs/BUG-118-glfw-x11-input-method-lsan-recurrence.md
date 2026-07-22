---
id: BUG-118
theme: G
depends_on: []
---
# BUG-118 — GLFW X11 input-method LeakSanitizer recurrence

## Status
- Backlog; reproduced four times on 2026-07-21 while verifying `RUNTIME-190`.
- This is an unrelated platform-gate failure: the active texture-bake patch
  does not touch GLFW, X11, platform lifetime, or sanitizer configuration.

## Goal
- Explain why the standalone GLFW lifetime contract again retains the
  408-byte X11 input-method allocation after proving process-static
  `glfwTerminate()` ran, and restore the unsuppressed contract to green.

## Non-goals
- No global `detect_leaks=0`, broad X11/GLFW suppression, retry wrapper,
  quarantine, label exclusion, or weakening of the synthetic-leak control.
- No changes to texture baking, renderer material behavior, or `RUNTIME-190`.

## Context
- `BUG-082` closed this exact allocation path on 2026-07-16 after the
  unchanged GLFW 3.4/Xlib teardown passed cleanly and the standalone process
  proved `glfwTerminate()` executed exactly once.
- On 2026-07-21 the canonical CPU selector failed the standalone contract,
  an immediate exact rerun failed identically, and both post-fix complete CPU
  selectors reproduced it. All four runs reported
  `BUG082_GLFW_STATIC_TEARDOWN: terminate_calls=1` followed by one direct
  408-byte allocation retained through `libX11.so.6`; the synthetic control
  still behaved as intended.
- Exact reproducer:
  `ctest --test-dir build/ci --output-on-failure -R '^GlfwLifecycleLsan\.EngineStaticTeardownAndLeakControl$' --timeout 60`.
- The current executable build ID is
  `e025bf63ae31f4a96e1802638dc13dfac30f335b`. The live host/display/XIM
  configuration and whether a clean rebuild changes the result have not yet
  been compared with the retired `BUG-082` evidence.

## Required changes
- [ ] Reproduce from a fresh `ci` configure/build and capture the active X11
      display, locale, input-method, libX11, and GLFW identities.
- [ ] Compare the current unregister/close call path and process teardown
      ordering with the `BUG-082` proof, including whether `XCloseIM` executes.
- [ ] Determine whether this is environment-dependent upstream retention,
      stale build state, or a regressed engine lifetime before changing code.
- [ ] Apply only the narrowest ownership-correct remedy that preserves the
      unsuppressed synthetic engine-leak control.

## Tests
- [ ] Make the exact standalone contract pass repeatedly on the reproducing
      live-X11 host without retry or suppression.
- [ ] Run the GLFW/platform intersection and the complete CPU-supported gate.

## Docs
- [ ] Record the ownership diagnosis and current environment comparison here.
- [ ] Update the bug index and retirement log if the issue is resolved.

## Acceptance criteria
- [ ] The standalone clean process exits zero after initializing and
      terminating GLFW/X11, while its named 4,096-byte synthetic leak still
      exits with the expected LeakSanitizer failure.
- [ ] The fix or environment contract explains why `BUG-082` passed and this
      recurrence fails; no unrelated sanitizer finding is hidden.

## Verification
```bash
cmake --preset ci --fresh
cmake --build --preset ci --target IntrinsicGlfwLifecycleLsanProcess
ctest --test-dir build/ci --output-on-failure \
  -R '^GlfwLifecycleLsan\.EngineStaticTeardownAndLeakControl$' --timeout 60
ctest --test-dir build/ci --output-on-failure -L platform -L glfw --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Reopening the gate by disabling leak detection, accepting exit 86 for the
  clean process, or suppressing all X11/GLFW allocations.
- Treating `terminate_calls=1` alone as proof that every XIM-owned allocation
  was released.
- Mixing the platform diagnosis into `RUNTIME-190` production code.

## Maturity
- Target: `Operational`; this issue is a regression of the retired `BUG-082`
  operational contract.
