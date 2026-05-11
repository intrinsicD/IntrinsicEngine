# GRAPHICS-039 — Clustered light binning (planning)

## Goal
Lock down the contract for a clustered (froxel-grid) light culling pass that bins extracted lights into view-space cells and exposes the resulting per-cell light list to surface shading, so that many-light scenes shade in O(lights-per-cell) rather than O(total-lights). Planning only — no shader bodies or pipeline changes land here.

## Non-goals
- No many-lights ReSTIR sampling (covered by `GRAPHICS-046`).
- No tiled-deferred or visibility-buffer materialization (`GRAPHICS-043`).
- No new light types beyond the existing `LightSnapshot` shape.
- No shadow caster changes; shadow assignment remains owned by `ShadowSystem`.
- No CPU-side light culling; entirely GPU-resident.

## Context
- Owner layer: `graphics/renderer` (compute pass + bind layout), `graphics/rhi` (existing storage-buffer surface).
- The current `LightSystem` extracts lights into a flat GPU buffer; surface shaders iterate the full list. This is fine for handful-of-lights scenes; modern AAA scenes carry hundreds of analytic lights.
- Clustered shading is the canonical pattern (Olsson/Persson 2012, Filament implementation, UE clustered+forward, Frostbite). Cells are typically 16×16×24 (XY tiles × log-Z slices).
- Cross-links: `GRAPHICS-009` (deferred lighting), `GRAPHICS-042` (PBR completeness wants per-cell IBL probe binning too), `GRAPHICS-046` (ReSTIR DI samples the same cell list).

## Design decisions to record
1. **Cluster grid shape.** Lock cell count XYZ (suggested 16×9×24 at 16:9, scalable; record the formula). Z slicing is logarithmic in view-Z; record the near/far policy.
2. **Cluster build pass.** Compute pass that produces an axis-aligned bounding box per cell. Decide between rebuild-every-frame (simpler, robust to camera changes) and reuse-when-camera-static (cheaper). Default: rebuild every frame.
3. **Light-to-cluster assignment pass.** Compute pass that tests each extracted light's bounding volume against each cell's AABB, emits per-cell light-index lists. Storage shape: a single packed index buffer + a per-cell offset/count header.
4. **Light bounding shapes.** Point: sphere. Spot: cone (decide cone-vs-AABB test). Directional: skip (always affects all cells). Record exact intersection routines.
5. **Per-cell capacity.** Decide hard limit (suggested 256 lights/cell) and overflow policy (clamp + diagnostic counter `LightClusterOverflowCount`). Record memory budget at the chosen cell count.
6. **Surface-shader integration.** Decide bind set + binding for the cluster index buffer + offset header. Surface shaders compute the cluster from `gl_FragCoord` + view-Z and iterate only the assigned indices.
7. **IBL probe extension.** The same clustering structure can hold IBL probe indices for `GRAPHICS-042`. Record the rule for sharing the index storage vs. parallel buffers (default: parallel buffer, same cell layout).
8. **Snapshot interaction.** `LightSystem` reads from the existing extracted `LightSnapshot` records; no new extraction fields. Record the rule.
9. **Async-compute affinity.** Cluster build and assignment passes are tagged with `QueueAffinity::AsyncCompute` per `GRAPHICS-037` once that lands. Default: graphics queue.
10. **Diagnostics.** `LightClusterOverflowCount`, `LightsCulledCount`, `EmptyClusterCount`. Atomic increments.
11. **Test split.** `contract;graphics` for cluster build, light assignment, overflow handling, all under null RHI; opt-in `gpu;vulkan` smoke for shader correctness.
12. **Layering audit.** No live ECS access. No new RHI surfaces.

## Required changes
- [ ] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [ ] Cross-link upstream and downstream tasks enumerated in Context.
- [ ] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-039-Impl-A** — Cluster grid resource + build pass + null-RHI shape tests.
- **GRAPHICS-039-Impl-B** — Light-assignment pass + overflow diagnostic + `contract;graphics` tests.
- **GRAPHICS-039-Impl-C** — Surface-shader integration + per-bucket recipe wiring + integration tests.
- **GRAPHICS-039-Impl-D** — Async-compute affinity wiring (gated by `GRAPHICS-037`).

## Tests
- [ ] Planning slice: validators only.
- [ ] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [ ] Update `docs/architecture/rendering-three-pass.md` with the clustered-shading section.
- [ ] Update `src/graphics/renderer/README.md` light-system section.

## Acceptance criteria
- [ ] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [ ] Implementation child slices are identified but not opened.
- [ ] No new RHI surfaces.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No new light types in this slice.
- No CPU-side light culling.
- No bypass of `LightSystem` extraction.
- No mixing of mechanical file moves with semantic refactors.
