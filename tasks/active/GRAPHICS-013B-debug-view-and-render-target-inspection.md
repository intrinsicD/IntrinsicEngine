# GRAPHICS-013B — Debug view and render-target inspection

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-013A` completion.
- Current slice: promoted from backlog; implementation not started in this handoff.

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
- Own and specify:
  - debug-view sampled-resource selection
  - render-target inspection hooks
  - debug preview output
  - diagnostics for missing sampled resources
- Define deterministic behavior for unavailable, invalid, or incompatible debug-view resource selections.
## Tests
- Add `contract;graphics` tests for debug-view selection semantics and preview-output pass contract behavior.
- Add `unit;graphics` tests for deterministic diagnostic and inspection dump formatting.
- Keep GPU/Vulkan debug visualization smoke checks optional.
## Docs
- Update debug-view and inspection contract docs where applicable.
## Acceptance criteria
- Debug-view resource selection and fallback behavior are deterministic and testable.
- Missing sampled-resource cases emit structured diagnostics.
- Inspection hooks avoid coupling debug features to platform/window ownership.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Expanding into postprocess effect ownership.
- Expanding into ImGui/present/backbuffer finalization policy ownership.
