---
id: RUNTIME-280
theme: F
depends_on: [RUNTIME-277, GEOM-110, RUNTIME-287]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources, geometry.property-coherence, runtime.spatial-query-locality]
---
# RUNTIME-280 — Selection-by-query operations and mask publication

## Goal
- Select primitives by query (nearest to a point, within a radius, property predicate,
  boundary elements, current pick, existing selection) and optionally publish the
  result as a Bool mask property with undo, so selections feed operation inputs.

## Non-goals
- No config section (command-only operation); no new spatial index; no lasso/box screen-space selection.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Exists: `Runtime.SelectionController.cppm` `EditPrimitives(registry, entity, domain, PrimitiveSelectionEdit::{Replace,Add,Toggle,Remove,Clear,All,Invert}, indices)`, `ReadPrimitives`, `PrimitiveSelectionSnapshot`; editor command `ApplyEditorPrimitiveSelection(context.Processing, entity, domain, op, indices)` used by `Sandbox.DomainPanels.cpp`; picked element via `PrimitiveSelectionResult`/`LastRefinedPrimitive` (`Runtime.PrimitiveSelectionRefinement.cppm`); `Runtime.SpatialIndexCache.cppm` CPU `Nearest/KNearest/Radius`; boundary via `Geometry.HalfedgeMesh.Boundary`/`MeshAnalysis` on the detached mesh (`Runtime.GeometryProcessingOperations.MeshSupport.hpp`); Bool properties publishable with history via `CaptureGeometryScalarProperty`/`ApplyGeometryScalarProperty`.
- Consumers of masks: `HarmonicFieldConfig::HardMask` (Bool property), `LaplacianEigenbasisConfig::DistanceSource` (row slot).
- Spatial acceleration consideration: nearest/radius queries use the shared `SpatialIndexCache` for the entity's canonical position property when present, else a brute-force scan over live rows (reference semantics); no private index; invalidation follows the cache's existing rules. See `docs/architecture/spatial-index-consumers.md` and add this consumer there.

## Control surfaces
- Config: N/A (command-only; parameters are per call).
- UI: "Select by query" block in Selection Details and "Use selection as…" buttons (UI-061).
- Agent/CLI: `selection_get {entity, domain, offset, limit}` (read-only), `pick_get` (read-only), `selection_query {…}` (mutating: changes selection and optionally writes a mask) in `Runtime.AgentOperations`.

## Required changes
- [ ] `Runtime.SelectionQueryOperations.cppm` + implementation: `EditorSelectionQueryCommand { StableEntityId; GeometryElementDomain Domain; variant<NearestToPoint{P, K}, WithinRadius{P, R}, PropertyPredicate{GeometryPropertyRef, Comparison, Value, Component}, BoundaryElements, CurrentPick, FromSelection> Query; PrimitiveSelectionEdit Combine; std::string MaskOutput; }` → `{Status, Count, ElementCount, Indices (bounded), MaskPublished}`; `Preview` returns structured readiness (RUNTIME-277).
- [ ] Selection changes through `ApplyEditorPrimitiveSelection`; mask publication inside `CommandHistory->Execute({.Label="Select by query"})` with the stale-input guards used by the smoothing owner.
- [ ] `ExportSelectionAsMask` / `ImportMaskAsSelection` helpers; Harmonic `HardMask` and Eigenbasis `DistanceSource` can be filled from the current selection.
- [ ] Agent operations registered with bounded index paging.

## Tests
- [ ] `tests/contract/runtime/Test.SelectionQueryOperations.cpp` on `EditorFeatureTestContext`: nearest/radius/predicate/boundary determinism on mesh, graph and point-cloud fixtures; combine modes; mask publication on the originating domain; undo restores prior mask and selection; stale topology refused.

## Docs
- [ ] `docs/architecture/spatial-index-consumers.md` consumer row; `docs/architecture/sandbox-editor-feature-boundaries.md`; module inventory regenerated.

## Acceptance criteria
- [ ] All query kinds work on every compatible element domain and publish masks on the originating domain with one undoable transaction.
- [ ] Selections can feed `HardMask`/`DistanceSource` without manual index entry.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SelectionQuery|SelectionController|AgentOperations' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- A private spatial index or name-based domain eligibility.
- ARCH-019 exclusions (the mask write is this operation's own publication, not a generic property write).
