---
id: RUNTIME-107
theme: F
depends_on: [BUG-030]
maturity_target: Operational
---
# RUNTIME-107 — Headless-capable Engine::Run loop coverage

## Goal
- Make the `Engine::Run()` regression tests that currently need a live GLFW
  window execute on headless hosts through an explicit runtime/platform
  backend choice, so BUG-017/BUG-019/BUG-024 coverage is not display-host-only.

## Non-goals
- No xvfb or virtual-display CI workaround.
- No weakening of the existing sandbox regression assertions.
- No platform backend auto-selection that hides backend choice from tests.

## Context
- BUG-030 restored the default CPU gate by adding the established
  `ShouldClose() -> GTEST_SKIP()` guard around `Engine::Run()` tests whose
  GLFW window is born closed on headless hosts.
- That is a valid `CPUContracted` gate-restoration stop state, but it leaves
  the affected regression coverage skipped in headless automation.
- `EngineConfig` does not currently expose a runtime-selectable platform
  backend, and the GLFW/null backend choice is still effectively a build-time
  platform decision.
- Owner/layer: `runtime` owns engine composition and config consumption;
  `platform` owns explicit window/input backend implementations. Keep
  platform independent of runtime and graphics.

## Required changes
- [ ] Add an explicit test-facing way to request a headless-capable window loop
  for `Engine::Run()` without requiring a display.
- [ ] Keep backend selection explicit and architecture-reviewed; do not silently
  fall back from a failed GLFW window to Null without a diagnostic.
- [ ] Un-skip the BUG-030 guarded regression tests on headless hosts by routing
  them through the headless-capable loop.
- [ ] Add a contract test proving a born-closed live window yields an explicit
  skip/diagnostic instead of silent no-tick assertions.

## Tests
- [ ] `RuntimeSandboxAcceptance.ViewportLeftClickSubmitsSelectionPick` passes on
  a headless host.
- [ ] `RuntimeSandboxAcceptance.InspectorTransformEditFlushedToRenderStateSameFrame`
  passes on a headless host.
- [ ] The Sandbox editor drop/import `Engine::Run()` regressions guarded by
  BUG-030 pass on a headless host.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update `tests/README.md` and any platform/runtime backend-selection docs
  to describe how tests request a headless-capable engine loop.

## Acceptance criteria
- [ ] Headless CI executes the affected `Engine::Run()` regression assertions
  instead of skipping them.
- [ ] Backend selection remains explicit and layer-compliant.
- [ ] No GLFW/display requirement is introduced into the default CPU gate.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ViewportLeftClickSubmitsSelectionPick|InspectorTransformEditFlushedToRenderStateSameFrame|DroppedFilePathsRouteAmbiguousPly|PlatformDropEventImportsObjMesh|PlatformDropEventImportsOffMesh|PlatformCloseEventStopsEngineRunState' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding xvfb/virtual-display dependencies to make GLFW tests pass.
- Silently falling back from a requested live backend to Null without recording
  the backend mismatch.
- Moving platform backend selection ownership into graphics or app code.

## Maturity
- Target: `Operational` for headless automation. This task is the follow-up
  split from BUG-030's `CPUContracted` gate-restoration slice.
