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

## Acceptance criteria
- [ ] gpu;vulkan smoke:
  - observed pixels move before Accept and return on Discard;
  - a moved preview is not culled.
- [ ] Contract tests:
  - pick refinement is off while uncommitted positions are shown;
  - a Discard after a concurrent CPU edit shows the edit;
  - compaction does not replay a stale shadow.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
