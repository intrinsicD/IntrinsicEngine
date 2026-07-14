# GRAPHICS-013B — Debug view and render-target inspection

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-013A` completion.
- Completed: 2026-05-03.
- PR/commit: 1606637.
- Completed slice: CPU/null debug-view inspection table, sampled-resource selection/fallback diagnostics, `DebugViewPass` command shim, frame-recipe preview scheduling tests, docs, and regenerated module inventory.
- Follow-up questions: `tasks/backlog/rendering/GRAPHICS-013BQ-debug-view-backend-clarifications.md`.

## Goal
- Define and implement debug-view sampled-resource selection and render-target inspection contracts with deterministic diagnostics.
## Non-goals
- No postprocess chain ownership (bloom/AA/tone-map/histogram).
- No ImGui overlay/present finalization ownership.
- No Vulkan-only mandatory tests.
## Context
- Owner: renderer debug visualization seams and framegraph-readable resource metadata.
- Focus is inspection and diagnostics, not effect execution.
## Required changes
- [x] Own and specify:
  - [x] debug-view sampled-resource selection
  - [x] render-target inspection hooks
  - [x] debug preview output
  - [x] diagnostics for missing sampled resources
- [x] Define deterministic behavior for unavailable, invalid, or incompatible debug-view resource selections.
## Tests
- [x] Add `contract;graphics` tests for debug-view selection semantics and preview-output pass contract behavior.
- [x] Add `unit;graphics` tests for deterministic diagnostic and inspection dump formatting.
- [x] Keep GPU/Vulkan debug visualization smoke checks optional.
## Docs
- [x] Update debug-view and inspection contract docs where applicable.
## Acceptance criteria
- [x] Debug-view resource selection and fallback behavior are deterministic and testable.
- [x] Missing sampled-resource cases emit structured diagnostics.
- [x] Inspection hooks avoid coupling debug features to platform/window ownership.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
- Verified locally with targeted graphics debug-view/frame-recipe tests and the default CPU-supported correctness gate on 2026-05-03.
## Forbidden changes
- Expanding into postprocess effect ownership.
- Expanding into ImGui/present/backbuffer finalization policy ownership.
