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
- `WindowConfig` now exposes `WindowBackend::Configured` (default, preserving
  the CMake-selected backend) and `WindowBackend::Null` (explicit test-facing
  deterministic headless backend).
- Owner/layer: `runtime` owns engine composition and config consumption;
  `platform` owns explicit window/input backend implementations. Keep
  platform independent of runtime and graphics.

## Required changes
- [x] Add an explicit test-facing way to request a headless-capable window loop
  for `Engine::Run()` without requiring a display.
- [x] Keep backend selection explicit and architecture-reviewed; do not silently
  fall back from a failed GLFW window to Null without a diagnostic.
- [x] Un-skip the BUG-030 guarded regression tests on headless hosts by routing
  them through the headless-capable loop.
- [x] Add a contract test proving a born-closed live window yields an explicit
  skip/diagnostic instead of silent no-tick assertions.

## Tests
- [x] `RuntimeSandboxAcceptance.ViewportLeftClickSubmitsSelectionPick` passes on
  a headless host.
- [x] `RuntimeSandboxAcceptance.InspectorTransformEditFlushedToRenderStateSameFrame`
  passes on a headless host.
- [x] The Sandbox editor drop/import `Engine::Run()` regressions guarded by
  BUG-030 pass on a headless host.
- [x] Default CPU gate stays green.

## Docs
- [x] Update `tests/README.md` and any platform/runtime backend-selection docs
  to describe how tests request a headless-capable engine loop.

## Acceptance criteria
- [x] Headless CI executes the affected `Engine::Run()` regression assertions
  instead of skipping them.
- [x] Backend selection remains explicit and layer-compliant.
- [x] No GLFW/display requirement is introduced into the default CPU gate.

## Status
- Completed 2026-06-15 at maturity `Operational`.
- PR/commit: this retirement commit.
- Decision: `Core::Config::WindowConfig::Backend` defaults to
  `WindowBackend::Configured`, preserving the CMake-selected platform backend.
  Tests that must drive `Engine::Run()` on displayless hosts set
  `WindowBackend::Null`, which routes `Platform::CreateWindow` to the
  always-compiled deterministic Null window. A configured GLFW window that is
  born closed logs a runtime zero-frame warning; the runtime does not silently
  fall back to Null.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ViewportLeftClickSubmitsSelectionPick|InspectorTransformEditFlushedToRenderStateSameFrame|DroppedFilePathsRouteAmbiguousPly|PlatformDropEventImportsObjMesh|PlatformDropEventImportsOffMesh|PlatformCloseEventStopsEngineRunState' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Verification results
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests
bash -lc "set -o pipefail; ctest --test-dir build/ci --output-on-failure -R 'ViewportLeftClickSubmitsSelectionPick|InspectorTransformEditFlushedToRenderStateSameFrame|DroppedFilePathsRouteAmbiguousPly|PlatformDropEventImportsObjMesh|PlatformDropEventImportsOffMesh|PlatformCloseEventStopsEngineRunState|ConfiguredBackendBornClosedLogsZeroFrameRunDiagnostic' --timeout 60 | tee /tmp/runtime-107-focused-ctest.log"
bash -lc "set -o pipefail; cmake --build --preset ci --target IntrinsicTests 2>&1 | tee /tmp/runtime-107-intrinsic-tests-build.log"
bash -lc "set -o pipefail; ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 2>&1 | tee /tmp/runtime-107-default-cpu-ctest.log"
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
```

Result: runtime contract/integration targets built; focused CTest passed 7/7
with no skips; `IntrinsicTests` built; the default CPU-supported CTest gate
passed 2973/2973; module inventory regeneration produced no committed delta;
task/docs/layering/test-layout/diff hygiene checks passed.

## Forbidden changes
- Adding xvfb/virtual-display dependencies to make GLFW tests pass.
- Silently falling back from a requested live backend to Null without recording
  the backend mismatch.
- Moving platform backend selection ownership into graphics or app code.

## Maturity
- Target reached: `Operational` for headless automation. The affected
  `Engine::Run()` regression assertions execute under the explicit Null window
  backend on headless hosts.
