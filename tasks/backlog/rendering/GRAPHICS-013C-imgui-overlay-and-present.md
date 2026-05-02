# GRAPHICS-013C — ImGui overlay and present/finalization
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
- Own and specify:
  - ImGui draw-data import
  - overlay load/store behavior
  - final present/finalization pass
  - imported backbuffer write policy
- Enforce that imported `Backbuffer` writes happen only in present/finalization per architecture policy.
- Add structured diagnostics for invalid overlay/present resource state.
## Tests
- Add `contract;graphics` tests for ImGui import contract, overlay composition behavior, and final backbuffer write policy.
- Add `unit;graphics` tests for deterministic present/finalization diagnostics formatting where applicable.
- Keep optional `gpu;vulkan` smoke tests opt-in.
## Docs
- Update present/finalization and overlay ownership sections in architecture/renderer docs.
## Acceptance criteria
- Overlay and present/finalization ownership are explicit and testable without Vulkan.
- Imported `Backbuffer` write policy is enforced or represented in deterministic diagnostics.
- Graphics layer remains decoupled from platform window ownership.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Introducing unrelated render effects or platform ownership changes.
- Mixing mechanical file moves with semantic refactors.
