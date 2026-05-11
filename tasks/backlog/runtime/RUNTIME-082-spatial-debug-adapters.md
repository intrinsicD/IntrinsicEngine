# RUNTIME-082 — `Extrinsic.Runtime.SpatialDebugAdapters` umbrella

## Goal
- Open the runtime-side adapter umbrella declared by `GRAPHICS-011Q`: a new module `Extrinsic.Runtime.SpatialDebugAdapters` (planned home: `src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cppm`) hosting concrete BVH/KD-tree/octree/convex-hull adapters that translate geometry-tree implementations into the data-only bounds, hierarchy-node, split-plane, convex-hull edge, and point-marker snapshot records consumed by `Graphics.SpatialDebugVisualizers`.

## Non-goals
- No graphics-side ownership of geometry tree implementations.
- No mutation of the existing `SpatialDebugVisualizerOptions` / `Diagnostics` graphics-visible budget/diagnostics surfaces.
- No editor UI for spatial debug toggles (lives elsewhere, consumes adapter pre-filter inputs).

## Context
- Status: not started.
- Owner/layer: `runtime`.
- Planning anchor: `tasks/done/GRAPHICS-011Q-spatial-debug-adapter-clarifications.md` ("concrete BVH/KD-tree/octree/convex-hull adapters live in **runtime extraction** … `Extrinsic.Runtime.SpatialDebugAdapters` — not in `src/geometry` and not in `src/graphics` — because runtime is the only layer permitted to import both geometry tree implementations and the graphics packet types").
- Adapters may apply CPU-side pre-filters (leaf-only, occupancy-only, capped depth) and surface adapter-side statistics through `RuntimeRenderExtractionStats`.
- Test placement: `tests/integration/runtime/` next to `Test.RuntimeRenderExtraction.cpp`.

## Required changes
- [ ] Add `src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cppm` exporting `Extrinsic.Runtime.SpatialDebugAdapters` with:
  - `class ISpatialDebugAdapter { virtual void Append(SpatialDebugSnapshotBatch& out, const SpatialDebugAdapterOptions& filters) = 0; }`,
  - concrete `BvhAdapter`, `KdTreeAdapter`, `OctreeAdapter`, `ConvexHullAdapter`,
  - registry surface for selecting an active adapter per renderable.
- [ ] Wire `RenderExtractionCache::ExtractAndSubmit` to invoke the active adapter for entities that carry the relevant geometry-tree component, accumulate snapshots into the existing `RuntimeRenderSnapshotBatch::SpatialDebug*` spans, and report adapter-side stats in `RuntimeRenderExtractionStats` (truncations, depth-cap hits, leaf counts).
- [ ] Pre-filter inputs (leaf-only, occupancy-only, depth-cap) are runtime-owned configuration, settable through editor/runtime API; defaults are recorded in the parent task.

## Tests
- [ ] `contract;runtime` integration test: each adapter produces deterministic snapshot counts for a small fixture tree.
- [ ] `contract;runtime` test: pre-filter (leaf-only) reduces output count by the expected factor for a balanced tree fixture.
- [ ] `contract;runtime` test: depth-cap truncation increments the recorded `RuntimeRenderExtractionStats::SpatialDebugTruncations` counter.
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/runtime/README.md` to flip the planned `SpatialDebugAdapters` umbrella row to current state.
- [ ] Refresh `docs/api/generated/module_inventory.md` after adding the module.

## Acceptance criteria
- [ ] All four adapter kinds compile, register, and produce deterministic outputs.
- [ ] No new graphics imports; runtime imports the geometry tree modules + `Graphics.SpatialDebugVisualizers` (already an existing edge).
- [ ] `SpatialDebugVisualizerOptions` / `Diagnostics` surfaces remain unchanged.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract|integration' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Importing geometry-tree implementations from `src/graphics/*`.
- Adding graphics-visible adapter-specific knowledge to `SpatialDebugVisualizerOptions`.

## Next verification step
- Land the umbrella + at least the BVH adapter, wire extraction, exercise the contract tests above.
