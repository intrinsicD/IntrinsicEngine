---
id: RUNTIME-139
theme: F
depends_on:
  - RUNTIME-125
  - RUNTIME-197
  - RUNTIME-202
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
maturity_target: Retired
---
# RUNTIME-139 — Remove speculative AoS storage planning

## Goal

- Delete the public planning/hint/promotion surface for a
  `StaticInterleavedAoS` geometry lane that has no allocator, shader, live
  residency, or qualifying adoption evidence, leaving the implemented uniform
  SoA path as the only truthful contract.

## Non-goals

- No AoS allocation, vertex packing, shader variant, pipeline variant, or
  promote-on-edit implementation.
- No change to the bytes, alignment, partial-update behavior, or rendering
  semantics of the current uniform SoA path.
- No deletion of the non-adoption vertex-fetch probe or invention of a
  performance conclusion from its smoke data.
- No broad `GpuWorld`, geometry-residency, or render-extraction redesign.

## Context

- Owner/layers: `graphics/renderer` owns `GpuWorld` allocation and the
  geometry-residency coordinator; `runtime` owns typed upload-plan assembly.
- ADR-0022 selects uniform SoA with per-channel streaming. The retained
  `rendering.vertex_fetch_layout.smoke` probe explicitly records
  `adoption_claim=false`; no claim-eligible result justifies another storage
  lane or shader family.
- Current `GpuWorld::UploadGeometry(...)` always allocates and advertises
  `UniformSoA`. In contrast, `PlanGeometryStorage(...)` can report
  `StaticInterleavedAoS`, and runtime/residency records propagate that proposal
  even though it is never applied. The promotion planner is exercised only by
  its own speculative contract tests.
- A plan that names non-existent residency is not a useful seam. It makes
  downstream code defend a state that production cannot create and makes the
  RUNTIME-129 bake path carry an `UnsupportedStorageLane` branch for a dormant
  promise.
- Reintroduction trigger: a claim-eligible GPU profile must identify vertex
  fetch/layout as a material bottleneck and a matched SoA/AoS probe must cross
  a frozen adoption threshold. That evidence opens a new task; this ID does
  not reserve the future design.

## Right-sizing

- One implemented physical layout needs one direct upload/residency contract,
  not an enum, hint, plan, status family, and conversion planner for an absent
  second layout.
- Keep the existing per-channel descriptors and update masks that describe
  real SoA bytes and edits. Remove only state whose sole purpose is the absent
  AoS branch.

## Required changes

- [ ] Re-run the production/test consumer census and pin that no allocation,
      shader, pipeline, or residency record implements interleaved AoS.
- [ ] Remove `GeometryStorageLane`, `GeometryStorageHint`,
      `GeometryStoragePlan*`, `GeometryStoragePromotion*`,
      `PlanGeometryStorage(...)`, and `PlanGeometryStoragePromotion(...)` from
      the public `GpuWorld` surface when the census confirms their only
      alternative is dormant AoS planning.
- [ ] Remove storage hints/plans from `GeometryResidency` requests, results,
      cache entries, and every runtime geometry-plan builder/extraction caller;
      upload directly through the uniform SoA contract.
- [ ] Remove fixed `StorageLane` fields and unsupported-AoS branches from
      copied residency views where channel descriptors already truthfully
      describe the only live layout.
- [ ] Delete the planning-only AoS/promotion contract tests and replace them
      with source/behavior ratchets proving the dormant symbols are absent and
      the existing SoA upload/update/residency contracts remain covered.

## Tests

- [ ] Focused `GpuWorld` and geometry-residency contracts pass with uniform
      SoA upload, partial channel updates, exact channel descriptors, and
      normal-bake residency unchanged.
- [ ] Runtime mesh, mesh-primitive, graph, point-cloud, and procedural
      extraction contracts pass without storage-hint propagation.
- [ ] Structural coverage rejects reintroduction of the deleted AoS planning
      tokens without a new evidence-gated task.
- [ ] The retained vertex-fetch benchmark manifest still validates and still
      carries no adoption claim.
- [ ] Default CPU-supported correctness gate passes.

## Docs

- [ ] Update ADR-0022 and renderer/runtime READMEs to describe uniform SoA as
      the sole implemented and exposed geometry-storage contract.
- [ ] Keep the benchmark README explicit that the layout probe is negative /
      non-adoption evidence, not a promised implementation lane.
- [ ] Regenerate the module inventory after public module-surface deletion.
- [ ] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [ ] No production or test source names `StaticInterleavedAoS` or exposes a
      storage plan/promotion contract for an unimplemented lane.
- [ ] All live geometry residency is described directly by its uniform SoA
      channel descriptors, with existing rendering and partial-update behavior
      preserved.
- [ ] The non-adoption benchmark remains reproducible and no performance or
      adoption claim is added.
- [ ] A future alternate layout requires a new, evidence-backed task rather
      than reopening dormant public planning state.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GpuWorld|GeometryResidency|GeometryExtraction|ObjectSpaceNormalBake' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- Implementing the optional AoS lane while deleting its speculative contract.
- Changing the uniform SoA byte layout or shader inputs without separate
  behavior and Vulkan evidence.
- Treating the smoke probe as an adoption or speedup claim.
- Replacing the removed surface with a generic storage-policy interface,
  registry, strategy object, or sidecar.

## Maturity

- Target: `Retired` for the speculative planning surface. The surviving
  uniform SoA path retains its existing CPU and promoted-Vulkan maturity; this
  cleanup makes no new rendering capability claim.
