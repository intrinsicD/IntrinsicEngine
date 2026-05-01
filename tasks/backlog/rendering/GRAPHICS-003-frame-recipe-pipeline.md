# GRAPHICS-003 — Frame recipe and default pipeline
## Goal
- Add a reusable default frame recipe that declares canonical resources and schedules renderer passes without hard-coded one-off graph construction.
## Non-goals
- No shader implementation.
- No Vulkan-only behavior.
- No new visual effects beyond the documented canonical pass chain.
## Context
- Owner: `src/graphics/renderer` and `src/graphics/framegraph`.
- `docs/architecture/rendering-three-pass.md` defines canonical resources, pass order, and backbuffer ownership.
- Current renderer entry points need a reusable recipe path before individual passes can be filled in safely.
## Required changes
- Define a typed `FrameRecipe` or equivalent policy object for forward, deferred, hybrid, debug, selection, and post-process feature gates.
- Build the default render graph from pass modules and canonical resource names.
- Add introspection hooks for pass order, resource producers/consumers, and imported-resource write policy.
## Tests
- Add contract tests for default pass order, optional-resource gating, imported backbuffer policy, and missing-resource diagnostics.
- Use null/mock backend seams for CPU-supported tests.
## Docs
- Update the frame recipe and pass table in `docs/architecture/rendering-three-pass.md`.
## Acceptance criteria
- The null renderer can report or execute the canonical graph through reusable pipeline code.
- Optional resources are omitted when their features are disabled.
- Backbuffer writes are restricted to the present/finalization step.
## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Creating hidden pass dependencies outside the frame graph.
