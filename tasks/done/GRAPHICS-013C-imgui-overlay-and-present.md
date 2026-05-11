# GRAPHICS-013C — ImGui overlay and present/finalization

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-013B` completion.
- Completed: 2026-05-03.
- PR/commit: f199b21.
- Completed slice: CPU/null ImGui overlay draw-data summary import, deterministic overlay/present diagnostics, guarded `ImGuiPass` and `PresentPass` command contracts, render-graph rejection of non-present backbuffer writes, docs, tests, and regenerated module inventory.
- Follow-up questions: `tasks/backlog/rendering/GRAPHICS-013CQ-imgui-present-backend-clarifications.md`.

## Goal
- Define and implement ImGui draw-data import, overlay composition policy, and final present/finalization backbuffer ownership.
## Non-goals
- No postprocess effect ownership.
- No debug-view inspection ownership.
- No platform/window ownership migration into graphics.
## Context
- Owner: renderer overlay/finalization pass contract and imported backbuffer write policy.
- Runtime/platform remain responsible for window/swapchain lifecycle wiring.
## Required changes
- [x] Own and specify:
  - [x] ImGui draw-data import
  - [x] overlay load/store behavior
  - [x] final present/finalization pass
  - [x] imported backbuffer write policy
- [x] Enforce that imported `Backbuffer` writes happen only in present/finalization per architecture policy.
- [x] Add structured diagnostics for invalid overlay/present resource state.
## Tests
- [x] Add `contract;graphics` tests for ImGui import contract, overlay composition behavior, and final backbuffer write policy.
- [x] Add `unit;graphics` tests for deterministic present/finalization diagnostics formatting where applicable.
- [x] Keep optional `gpu;vulkan` smoke tests opt-in.
## Docs
- [x] Update present/finalization and overlay ownership sections in architecture/renderer docs.
## Acceptance criteria
- [x] Overlay and present/finalization ownership are explicit and testable without Vulkan.
- [x] Imported `Backbuffer` write policy is enforced or represented in deterministic diagnostics.
- [x] Graphics layer remains decoupled from platform window ownership.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
- Verified locally with targeted graphics ImGui/present/frame-recipe tests and the default CPU-supported correctness gate on 2026-05-03.
## Forbidden changes
- Introducing unrelated render effects or platform ownership changes.
- Mixing mechanical file moves with semantic refactors.
