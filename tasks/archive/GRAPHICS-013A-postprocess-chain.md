# GRAPHICS-013A — Postprocess chain

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-012` completion.
- Completed: 2026-05-03.
- PR/commit: 6940767.
- Completed slice: CPU/null postprocess chain contracts, deterministic settings/stage diagnostics, explicit HDR-to-LDR frame-recipe resources, guarded `Histogram`/`Bloom`/`ToneMap`/`FXAA`/`SMAA` pass command shims, docs, and unit/contract tests.
- Follow-up questions: `tasks/backlog/rendering/GRAPHICS-013AQ-postprocess-backend-clarifications.md`.

## Goal
- Define and implement the HDR-to-LDR postprocess chain contract with explicit resource lifetime and diagnostics.
## Non-goals
- No debug-view sampled-resource inspection UX.
- No ImGui overlay integration or final present/finalization ownership.
- No Vulkan-only mandatory tests.
## Context
- Owner: `src/graphics/renderer/Passes`, framegraph resource model, and backend-agnostic pass contracts.
- Legacy postprocess modules are behavioral reference only.
## Required changes
- [x] Own and specify:
  - [x] bloom
  - [x] FXAA
  - [x] SMAA
  - [x] tone map
  - [x] histogram
  - [x] HDR to LDR chain
  - [x] postprocess resource lifetime
- [x] Declare temporary/intermediate resources for postprocess effects and histogram diagnostics.
- [x] Add deterministic diagnostics for missing required postprocess resources and unsupported effect combinations.
## Tests
- [x] Add `contract;graphics` tests for pass order, HDR-to-LDR conversion contract, and resource lifetime boundaries.
- [x] Add `unit;graphics` tests for postprocess diagnostics formatting/consistency when applicable.
- [x] Keep optional GPU smoke tests behind `gpu;vulkan` labels only.
## Docs
- [x] Update postprocess chain architecture docs and renderer docs for public contract changes.
## Acceptance criteria
- [x] `SceneColorHDR` ownership, transformation, and `SceneColorLDR` outputs are explicit and testable.
- [x] Postprocess intermediate resource ownership/lifetime is explicit and deterministic.
- [x] Postprocess diagnostics are backend-agnostic and testable without Vulkan.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
- Verified locally with targeted graphics postprocess/frame-recipe tests and the default CPU-supported correctness gate on 2026-05-03.
## Forbidden changes
- Folding debug-view or ImGui/present policy ownership into this task.
- Shader-only feature work without matching contract/test updates.
