# GRAPHICS-007 — Culling and draw-bucket contracts
## Goal
- Complete non-legacy culling and draw-bucket contracts for surface, line, point, alpha-mask, shadow, and selection lanes.
## Non-goals
- No full LBVH or acceleration-structure rewrite.
- No pass-specific shading work.
- No legacy culling module dependency.
## Context
- Owner: `src/graphics/renderer` and pass modules under `src/graphics/renderer/Passes`.
- Draw buckets must be reusable by depth, surface, line, point, shadow, and selection passes.
## Required changes
- Harden bucket reset, append, indirect-count, barrier, and dispatch/draw sequencing.
- Define the culling/bounds SSBO schema: instance slot, world-space bounds, visibility flags, layer mask, shadow participation, selection participation, and renderable-domain flags.
- Keep cullable records indexed by canonical renderable instance slots; any compacted culling list must be an indirection over instance slots, not a second source of entity identity.
- Define indirect draw output records and bucket ownership for surface, line, point, shadow, and selection passes.
- Define CPU fallback/mock behavior for null backend contract tests.
- Add diagnostics for unsupported bucket combinations and invalid geometry ranges.
## Tests
- Add mock command-context tests for reset, dispatch, barriers, bucket selection, and indirect draw metadata.
- Add contract tests proving instance-slot alignment across renderable, transform, bounds/culling, and draw-bucket records.
- Cover empty-scene, single-draw, multi-bucket, and invalid-range cases.
- Label these CPU/mock-backend tests `contract;graphics` so they run in the default CPU gate.
## Docs
- Update the culling and draw-bucket sections of `docs/architecture/rendering-three-pass.md`.
- Document the GPU scene SSBO layout and instance-slot identity rules.
## Acceptance criteria
- Surface, line, point, alpha-mask, shadow, and selection buckets have tested command contracts.
- Empty buckets do not issue unsafe commands.
- Bucket behavior is independent of live ECS state.
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
