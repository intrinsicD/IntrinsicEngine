---
id: RUNTIME-293
theme: I
depends_on: [GRAPHICS-156]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: planned from the operator's GPU residency decision and two independent design reviews (2026-09-29, ADR 0030); implementation owes the contract tests and gpu;vulkan smokes listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources]
---
# RUNTIME-293 — Accept GPU-authored positions without a re-upload

## Goal
- ADR 0030 decision 6 for positions: Accept writes the ring front back to the CPU property.
- A positions publication analogue of `PublishPointScalarField`: before/after, revision watches,
  undoable.
- For 1:1 domains (point clouds, graphs):
  - the GpuWorld shadow is patched for the position channel only (the other channels are
    preserved; fingerprint and content metadata are updated);
  - extraction acknowledges the new revision;
  - `MarkGpuDirty` / `MarkVertexPositionsDirty` are not set, so nothing is uploaded again.
- Meshes commit through the ordinary revision-delta upload.

## Completion — 2026-09-30
Commit: on `claude/cpd-nystrom` (see RETIREMENT-LOG). Accept of GPU-authored positions (ADR 0030
decision 6):
- `PublishPointPositionField` is the positions analogue of `PublishPointScalarField`: every row
  before and after, revision watches, undoable. Authored culling bounds (local or world) are
  recomputed from the live accepted rows in the same command. Undo and redo derive world bounds
  from the current transform. A bounds failure refuses the Accept before anything is written.
- Point clouds and graphs:
  - `RenderExtractionCache::CommitAcceptedPositions` calls `GpuWorld::CommitGeometryPositions`,
    which patches only the shadow's position range and its fingerprint;
  - the block keeps the copied front (`Committed`), or copies it once at the next culling head
    (`CopyPending`);
  - no CPU upload and no `MarkGpuDirty`.
- Meshes: ordinary revision-delta upload.
- Identity is the residency's publication counter (`GpuPropertyView::Publication`), not the
  buffer. `BindRevision` binds exactly the accepted publication. Rings carry a generation, and
  `Discard(key, generation)` releases only its own ring; a refused slot leaves no ring behind.
- Reusable run API for RUNTIME-294: `BeginEditorGpuPositionRun` (acquires the ring),
  `EditorGpuPositionRunFirstBack`, `AcceptEditorGpuPositionRun`, `DiscardEditorGpuPositionRun`
  (abandons the run; a no-op for terminal runs). The front readback is shared with the scalar
  transaction (`GpuFront.hpp`). Seam: `SpatialIndexCache::CommitGpuPositions`.

Evidence:
- Contract tests `GpuWorldPositionCommitContract.*`, `GpuPositionsAccept.*`, the
  `PositionPreviewExtraction` additions and the `GpuPropertyResidency` publication, generation
  and refused-ring tests.
- gpu;vulkan `RUNTIME293PositionsAccept.*`:
  - after Accept, 0 position uploads over 8 frames, pixels equal the preview, and the canonical
    slot is the front, so the next `ResolveGpuPropertyInput` uploads 0 bytes;
  - undo and redo upload once each;
  - positions moved outside their authored bounds stay visible, and undo culls them again.
- Implemented by Fable 5.1, reviewed by Codex 6 Astra (medium) in four rounds. Findings fixed:
  - publication stamp;
  - retained accepted front;
  - run-level Discard;
  - bounds (world recompute under the current transform, failure, deleted rows, local-only);
  - ring ownership by generation;
  - orphan ring on a refused slot.
- CPU gate 5320/5320 (1 pre-existing skip). GPU suite 130/131, only the environmental
  `VulkanShutdownLsanContract` red. Operational.

## Acceptance criteria
- [x] After Accept, extraction issues no position upload for a point cloud (IO counter); the
      render and CPU positions are equal.
- [x] Undo/redo restores positions and uploads once each.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
