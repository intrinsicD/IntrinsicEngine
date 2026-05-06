# GRAPHICS-011Q — Spatial debug adapter clarification follow-ups

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-06 after `GRAPHICS-010Q` retirement cleared `tasks/active/`.
- Completed: 2026-05-06.
- Branch: `claude/agentic-workflow-session-pmrBn`.
- Implementation commit: `37df72b` (resolve decisions and sync architecture/renderer-README docs).
- Task-state commit: pending retirement commit (this commit moves the file from `tasks/active/` to `tasks/done/`).
- Resolution: decisions recorded below and consequential notes synced into the `Extrinsic.Graphics.SpatialDebugVisualizers` paragraph of `docs/architecture/graphics.md` and into the spatial-debug visualizer bullet of `src/graphics/renderer/README.md`. Verification: `python3 tools/agents/check_task_policy.py --root . --strict` (75 task files validated, 0 findings) and `python3 tools/docs/check_doc_links.py --root .` (186 relative links, no broken links).

## Decisions
- **Concrete adapter ownership.** Concrete adapters that translate
  `Geometry::BVH`, `Geometry::KDTree`, `Geometry::Octree`, and convex-hull
  outputs into the data-only `Extrinsic.Graphics.SpatialDebugVisualizers`
  input records (`SpatialDebugAabb`, `SpatialDebugHierarchyNode`,
  `SpatialDebugSplitPlane`, `SpatialDebugWireEdge`, plus point markers)
  live in **runtime extraction**, not in `src/geometry` and not in
  `src/graphics`. Runtime is the only layer that may import both the
  geometry tree implementations (`Geometry.BVH`/`Geometry.KDTree`/
  `Geometry.Octree`/`Geometry.ConvexHull`) and the graphics packet types
  (`Extrinsic.Graphics.SpatialDebugVisualizers`); allowing geometry to
  reference graphics packet structs would invert the
  `geometry -> core` layer rule in `AGENTS.md` and copy the legacy
  `src/legacy/Graphics/Graphics.{BVH,KDTree,Octree,ConvexHull}DebugDraw`
  pattern into the promoted graphics layer. Editor/app code may still own
  user-facing toggles (enable/disable, color choice, leaf/internal filters,
  per-depth filtering) but must funnel them into the runtime adapter as
  inputs; it must not call into `Extrinsic.Graphics.SpatialDebugVisualizers`
  directly with live geometry references.
- **Adapter naming and module placement.** Concrete adapter modules land
  next to the existing runtime extraction in `src/runtime/` under the
  `Extrinsic.Runtime.SpatialDebugAdapters` umbrella module name (initial
  expected source files: `Runtime.SpatialDebugAdapters.cppm` plus
  per-structure helpers such as
  `Runtime.SpatialDebugAdapters.BVH`/`.KDTree`/`.Octree`/`.ConvexHull` if
  they are split for build-time isolation). Public adapter functions follow
  the verb-style `Build*SpatialDebugInputs(...)` naming used by other
  runtime extraction helpers (for example
  `BuildBVHSpatialDebugInputs(const Geometry::BVH&,
  SpatialDebugBVHAdapterOptions, ...)` returning the data-only record
  spans/vectors that
  `Extrinsic.Graphics.SpatialDebugVisualizers::BuildSpatialDebugPackets`
  consumes). The `Extrinsic.Graphics.SpatialDebugVisualizers` packet
  contract itself is **frozen** by this clarification: input record types
  and the diagnostics struct must not grow new fields to accommodate
  adapter-specific knowledge; new adapter inputs route through the existing
  bounds/hierarchy-node/split-plane/wire-edge/point-marker shapes.
- **Output limit and pre-filter policy.** Adapters must not bypass or
  duplicate the canonical
  `SpatialDebugVisualizerOptions::MaxLinePackets`/`MaxPointPackets`/
  `MaxDepth` budget — the graphics packet builder remains the single place
  that enforces budget truncation, and `TruncatedLineBudget`/
  `TruncatedPointBudget`/`RejectedDepthLimitCount` continue to be reported
  by graphics. Adapters may apply CPU-side **pre-filters** (leaf-only,
  internal-only, occupancy-only, per-depth subsetting, or capped traversal
  depth) before emitting input records; pre-filtered records are simply
  not produced and never reach graphics. Editor/app-side toggles
  (`Overlay`, `ColorByDepth`, `LeafOnly`, `DrawInternal`, `OccupiedOnly`)
  from the legacy debug-draw modules map to either adapter pre-filter
  options or to the existing `SpatialDebugVisualizerOptions` color/depth
  fields — they must not introduce new graphics-side options.
- **Diagnostics handoff.** `SpatialDebugVisualizerDiagnostics` remains the
  **single graphics-side diagnostic surface** for input-record validity,
  emitted line/point counts, and rejection/truncation reasons
  (`RejectedInvalidBoundsCount`, `RejectedInvalidCoordinateCount`,
  `RejectedDepthLimitCount`, `RejectedTopologyCount`,
  `TruncatedLineBudget`, `TruncatedPointBudget`). Adapters do **not**
  introduce a parallel graphics-visible diagnostics struct; runtime-side
  adapter invocation counts, snapshot-construction CPU cost, and
  pre-filter rejection counts (e.g. nodes filtered by leaf-only or depth
  pre-filter) report through `RuntimeRenderExtractionStats` (or a
  dedicated runtime-owned spatial-debug stats sibling) and stay outside
  `Extrinsic.Graphics.SpatialDebugVisualizers`. Graphics never inspects
  runtime-side adapter diagnostics, and runtime is responsible for
  forwarding adapter-visible failure modes (missing/empty geometry tree,
  null adapter options) through its own diagnostics rather than fabricating
  invalid input records to trigger graphics-side rejection counters.
- **Adapter test placement.** Adapter tests are **runtime integration
  tests** under `tests/integration/runtime/` (matching the existing
  `Test.RuntimeRenderExtraction.cpp` placement), because the adapters
  bridge two layers (geometry source structures + the graphics packet
  contract) and verifying both ends in the same test is the canonical
  shape. Pure geometry helpers used by adapters keep their tests under
  `tests/unit/geometry/`; the data-only graphics packet contract keeps its
  tests under `tests/unit/graphics/Test.Graphics.SpatialDebugVisualizers.cpp`.
  Editor/app-only adapter wiring (UI toggles, controllers) is covered, if
  at all, by app-level tests/sandboxes; it must not be folded into the
  graphics or geometry unit suites. New runtime adapter test files use the
  `Test.<Name>.cpp` naming (`AGENTS.md` §7 "New C++ test files use
  `Test.<Name>.cpp`"). Test labels follow `integration;runtime;graphics`
  so the default CPU gate (`-LE 'gpu|vulkan|slow|flaky-quarantine'`) still
  exercises adapter integration coverage without requiring Vulkan.

## Resolution
- Decisions recorded above and consequential notes synced into the
  `Extrinsic.Graphics.SpatialDebugVisualizers` paragraph of
  `docs/architecture/graphics.md` and into the spatial-debug visualizer
  bullet in `src/graphics/renderer/README.md`.
- Verification: `python3 tools/agents/check_task_policy.py --root . --strict`
  and `python3 tools/docs/check_doc_links.py --root .`.

## Goal
- Clarify which higher-layer adapters should translate concrete geometry/runtime spatial structures into the data-only `Extrinsic.Graphics.SpatialDebugVisualizers` input records.

## Non-goals
- No C++ behavior changes.
- No new spatial data structure algorithms.
- No editor UI controller work.

## Context
- `GRAPHICS-011` introduced graphics-owned packet builders for bounds, hierarchy nodes, split planes, convex-hull wire edges, and point markers without importing live geometry trees, runtime, editor UI, or ECS ownership into graphics.
- Concrete adapters for `Geometry::BVH`, `Geometry::KDTree`, `Geometry::Octree`, convex-hull mesh outputs, and editor/debug tooling should live in the owning layer that already has access to those structures.

## Required changes
- Decide the owning layer for concrete BVH/KD-tree/octree/convex-hull adapters (geometry helper API, runtime extraction helper, or app/editor-only utility).
- Document adapter naming, output limit policy, and diagnostics handoff into `SpatialDebugVisualizerDiagnostics`.
- Clarify whether adapter tests belong under geometry unit tests, runtime integration tests, or app/editor tests.

## Tests
- Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- Update `docs/architecture/graphics.md` and any geometry/runtime architecture docs touched by the adapter ownership decision.

## Acceptance criteria
- Concrete adapter ownership is clear without adding prohibited graphics dependencies.
- Future adapter work can proceed without changing the graphics packet-builder contract.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Importing runtime/editor/ECS ownership into `src/graphics`.
- Adding `src/graphics` dependencies on geometry implementation internals.

