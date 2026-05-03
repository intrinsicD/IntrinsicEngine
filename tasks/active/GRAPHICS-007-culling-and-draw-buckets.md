# GRAPHICS-007 — Culling and draw-bucket contracts

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-006` completion.
- Current slice: next task selected; implementation not started in this handoff.

## Goal
- Complete non-legacy culling and draw-bucket contracts for surface, line, point, alpha-mask, shadow, and selection lanes.
## Non-goals
- No full LBVH or acceleration-structure rewrite.
- No pass-specific shading work.
- No legacy culling module dependency.
## Context
- Owner: `src/graphics/renderer` and pass modules under `src/graphics/renderer/Passes`.
- Canonical source pass module: `Extrinsic.Graphics.Pass.Culling` (class `CullingPass`, defined in `src/graphics/renderer/Passes/Pass.Culling.{cppm,cpp}`). This task owns both `CullingPass` command contracts and the GPU draw-bucket contracts it produces; downstream geometry/shadow/selection passes are consumers, not owners, of those contracts.
- `CullingPass` is a real rendergraph pass that runs before any geometry-consuming pass; see the Pass Contract table and Pass module naming section in `docs/architecture/rendering-three-pass.md`.
- Draw buckets must be reusable by depth, surface, line, point, shadow, and selection passes.
## Required changes
- Harden `CullingPass` command sequencing: bucket reset, append, indirect-count, barrier, and dispatch/draw ordering.
- Define the culling/bounds SSBO schema: instance slot, world-space bounds, visibility flags, layer mask, shadow participation, selection participation, and renderable-domain flags.
- Keep cullable records indexed by canonical renderable instance slots; any compacted culling list must be an indirection over instance slots, not a second source of entity identity.
- Define indirect draw output records and bucket ownership for surface, line, point, shadow, and selection passes.
- Define CPU fallback/mock behavior for null backend contract tests.
- Add diagnostics for unsupported bucket combinations and invalid geometry ranges.
## Tests
- Add mock command-context tests for `CullingPass::Execute` covering reset, dispatch, barriers, bucket selection, and indirect draw metadata.
- Add contract tests proving instance-slot alignment across renderable, transform, bounds/culling, and draw-bucket records.
- Cover empty-scene, single-draw, multi-bucket, and invalid-range cases.
- Label these CPU/mock-backend tests `contract;graphics` so they run in the default CPU gate.
## Docs
- Update the culling and draw-bucket sections of `docs/architecture/rendering-three-pass.md`, including `CullingPass` in the Pass Contract table, the Pipeline Order, and the Pass module naming map.
- Document the GPU scene SSBO layout and instance-slot identity rules.
## Acceptance criteria
- `CullingPass` is documented as a real rendergraph pass owned by `Extrinsic.Graphics.Pass.Culling`, with command contracts (reset, dispatch, barriers, indirect-count) covered by tests.
- Surface, line, point, alpha-mask, shadow, and selection buckets have tested command contracts.
- Empty buckets do not issue unsafe commands.
- Bucket behavior is independent of live ECS state.
- Pass naming in this task, in `docs/architecture/rendering-three-pass.md`, and in the `Pass.Culling` source module agree.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Expanding legacy registration APIs instead of promoted graphics contracts.
