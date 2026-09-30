---
id: GRAPHICS-156
theme: I
depends_on: [RUNTIME-292]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: planned from the operator's GPU residency decision and two independent design reviews (2026-09-29, ADR 0030); implementation owes the contract tests and gpu;vulkan smokes listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources]
---
# GRAPHICS-156 — Renderer observes positions from the residency

## Goal
- ADR 0030 decision 5 for positions.
- The renderer observes the appearance-selected position property. While a method's ring
  exists (running, or finished but not accepted), its front is GPU-copied into the `GpuWorld`
  block at the head of the culling pass:
  - where `SubmitPendingUploadBarriers` runs, via `GpuTransferInCommandUploadDesc`;
  - with compute-write->transfer-read and transfer-write->shader-read barriers.
- Seam-split meshes get a gather through a device copy of `MeshSourceVertexForGpuVertex`, keyed
  by its remap revision.
- While uncommitted positions are shown:
  - bounds are widened conservatively or culling is bypassed;
  - primitive pick refinement is disabled for the entity;
  - dependent normals keep their CPU state;
  - the block's CPU shadow is marked stale, so compaction or replay never overwrites it.
- Discard or cancel restores the block from the current CPU state through a forced extraction
  update.

## Completion — 2026-09-30
Commit: on `claude/cpd-nystrom` (see RETIREMENT-LOG). The renderer observes positions from the
residency (ADR 0030 decisions 5 and 7):
- `GpuWorld::SetGeometryPositionPreview` / `ClearGeometryPositionPreview`: at the head of the
  culling pass, the ring front is copied (1:1 lanes) or gathered into the position block, with
  barriers on both sides.
  - The gather runs through `gpu_world_position_gather.comp` over `MeshSourceVertexForGpuVertex`;
    the map is uploaded once per remap revision and kept on the CPU for rebuilds.
  - `PositionShadowStale` keeps compaction and rebuild from replaying old positions. The shadow
    is replayed whenever no preview can rewrite the range (no map or no pipeline).
  - Stale handles are rejected.
- Extraction observes `v:position` fronts, submits unbounded preview bounds (not culled), and
  forces a CPU position upload when the front disappears. Discard therefore shows concurrent
  CPU edits, never the old shadow.
- Picking: primitive refinement is off while uncommitted positions are shown. The pick stamp
  carries the preview state of the frame being built (`ObservesUncommittedPositions`), so a
  preview transition between pick and readback discards the pick. Primitive highlights are
  suppressed while previewed. Normals keep their CPU state.
- Deviation: the copy records `CopyBuffer` plus the `UploadInCommand` barrier pair directly in
  `GpuWorld`, not `GpuTransferInCommandUploadDesc` (`GpuWorld` owns no `GpuTransfer`).

Evidence:
- Contract tests:
  - `GpuWorldPositionPreviewContract.*`: copy, recopy, compaction, rebuild, seam gather, map
    rebuild, refused map, failed pipeline, stale handle;
  - `PositionPreviewExtraction.*`;
  - `SceneInteractionModule.*` in the production hook order;
  - `PrimitiveSelection` suppression.
- gpu;vulkan `GRAPHICS156PositionPreview.*` (3, validation layers on):
  - pixels move before Accept and return bytewise on Discard;
  - a moved preview is not culled;
  - a seam-split surface gathers through its remap.
- Implemented by Fable 5.1, reviewed by Codex 6 Astra (medium) in three rounds. Findings fixed:
  - gather map lost on rebuild;
  - delayed-pick refinement;
  - stale-handle clear;
  - highlights at CPU positions;
  - preview state one frame late;
  - gather pipeline failure after rebuild.
- CPU gate 5299/5299 (1 pre-existing skip). GPU suite 128/129, only the environmental
  `VulkanShutdownLsanContract` red. Operational.

## Acceptance criteria
- [x] gpu;vulkan smoke:
  - observed pixels move before Accept and return on Discard;
  - a moved preview is not culled.
- [x] Contract tests:
  - pick refinement is off while uncommitted positions are shown;
  - a Discard after a concurrent CPU edit shows the edit;
  - compaction does not replay a stale shadow.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
