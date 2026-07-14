# GRAPHICS-007 — Culling and draw-bucket contracts

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-006` completion.
- Completed: 2026-05-03.
- Result: expanded culling/draw-bucket contracts to surface opaque, surface alpha-mask, line, point, shadow opaque, and selection surface/line/point lanes with CPU/mock command coverage.
- Follow-up task: `GRAPHICS-008 — Depth, surface, and G-buffer passes` promoted to `tasks/active/`.

## Completion metadata
- Implementation commit: pending local agent workflow handoff.
- Task-state commit: pending local agent workflow handoff.
- Verification: focused culling contract tests, shader validation, aggregate build, and default CPU gate passed; commands recorded below.

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
- [x] Harden `CullingPass` command sequencing: bucket reset, append, indirect-count, barrier, and dispatch/draw ordering.
- [x] Define the culling/bounds SSBO schema: instance slot, world-space bounds, visibility flags, layer mask, shadow participation, selection participation, and renderable-domain flags.
- [x] Keep cullable records indexed by canonical renderable instance slots; any compacted culling list must be an indirection over instance slots, not a second source of entity identity.
- [x] Define indirect draw output records and bucket ownership for surface, line, point, shadow, and selection passes.
- [x] Define CPU fallback/mock behavior for null backend contract tests.
- [x] Add diagnostics for unsupported bucket combinations and invalid geometry ranges.
## Tests
- [x] Add mock command-context tests for `CullingPass::Execute` covering reset, dispatch, barriers, bucket selection, and indirect draw metadata.
- [x] Add contract tests proving instance-slot alignment across renderable, transform, bounds/culling, and draw-bucket records.
- [x] Cover empty-scene, single-draw, multi-bucket, and invalid-range cases.
- [x] Label these CPU/mock-backend tests `contract;graphics` so they run in the default CPU gate.
## Docs
- [x] Update the culling and draw-bucket sections of `docs/architecture/rendering-three-pass.md`, including `CullingPass` in the Pass Contract table, the Pipeline Order, and the Pass module naming map.
- [x] Document the GPU scene SSBO layout and instance-slot identity rules.
## Acceptance criteria
- [x] `CullingPass` is documented as a real rendergraph pass owned by `Extrinsic.Graphics.Pass.Culling`, with command contracts (reset, dispatch, barriers, indirect-count) covered by tests.
- [x] Surface, line, point, alpha-mask, shadow, and selection buckets have tested command contracts.
- [x] Empty buckets do not issue unsafe commands.
- [x] Bucket behavior is independent of live ECS state.
- [x] Pass naming in this task, in `docs/architecture/rendering-three-pass.md`, and in the `Pass.Culling` source module agree.
## Verification
```bash
cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GraphicsCullingContracts' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
glslc -Iassets/shaders assets/shaders/culling/instance_cull.comp -o /tmp/intrinsic_instance_cull_canonical.spv
glslc -Iassets/shaders assets/shaders/instance_cull.comp -o /tmp/intrinsic_instance_cull_root.spv
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Expanding legacy registration APIs instead of promoted graphics contracts.
