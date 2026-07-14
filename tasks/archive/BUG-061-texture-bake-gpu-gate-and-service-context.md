---
id: BUG-061
theme: G
depends_on:
  - UI-014
  - RUNTIME-115
maturity_target: CPUContracted
completed: 2026-07-06
---
# BUG-061 — Texture bake UI misses service context and GPU availability gate

## Status
- Retired on 2026-07-06 at `CPUContracted`.
- PR/commit: this retirement commit.
- Fix: live sandbox editor contexts now pass `Engine::GetAssetService()` into
  `SandboxEditorContext`; selected-mesh texture bake model availability and
  command execution now additionally require an operational `RHI::IDevice`.
- Result: valid mesh properties/UVs no longer fail only because the runtime
  bake command service is absent, while Null/fail-closed graphics backends keep
  texture baking disabled/refused so the CPU-backed compatibility baker is not
  used from the default headless path.

## Goal
- Make selected-mesh texture baking available only when the live sandbox editor has both the runtime generated-texture service context and an operational GPU backend.

## Non-goals
- Do not port selected-mesh scalar/label/vector attribute baking to a GPU implementation.
- Do not implement `RUNTIME-129` GPU object-space normal bake scheduling.
- Do not enable texture baking for graph or point-cloud domains.

## Context
- Owner/layer: `runtime` editor UI and selected-mesh bake command routing.
- Symptom: computed mesh properties appear in the editor, but the Bake action remains disabled because the real engine-built `SandboxEditorContext` does not pass `AssetService` into the UI model.
- Policy correction: selected-mesh texture baking must also stay disabled on Null or non-operational graphics backends because the current bake implementation is CPU-backed and too slow for the default headless path.

## Required changes
- [x] Wire `Engine::GetAssetService()` into the sandbox editor context built from the live engine.
- [x] Gate selected-mesh texture bake model availability and command execution on `RHI::IDevice::IsOperational()`.
- [x] Preserve existing mesh/UV/property/source compatibility gates.

## Tests
- [x] Add/update `SandboxEditorUi` coverage for operational-device bake enablement.
- [x] Add coverage proving non-operational devices disable/refuse texture baking even when properties, UVs, and `AssetService` are present.
- [x] Add source-contract coverage proving the attached engine context builder wires the texture bake service and device.

## Docs
- [x] Update runtime editor documentation for the GPU-operational bake gate.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after retiring this task.

## Acceptance criteria
- [x] Live sandbox editor contexts no longer report the bake command as unavailable solely because `AssetService` is absent from `BuildContextFromEngine`.
- [x] Texture baking is disabled/refused unless `context.Device` is operational.
- [x] Existing UV/property compatibility diagnostics remain deterministic.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Adding CPU fallback behavior for GPU-only bake availability.
- Mixing in unrelated renderer, import, or material changes.
- Passing graphics or Vulkan handles into UI state.

## Maturity
- Target: `CPUContracted` — met. This task gates the current CPU-backed
  command behind operational-device availability and proves the editor
  contract on CPU/null tests; no `Operational` follow-up is owed here because
  GPU implementation work is owned by existing rendering/runtime follow-ups.
