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
# GRAPHICS-156 — Position previews through GpuWorld

## Goal
- ADR 0030 decision 5.
- A published position front is copied into its `GpuWorld` block at the head of the culling pass
  (where `SubmitPendingUploadBarriers` runs), reusing `GpuTransferInCommandUploadDesc`, with
  compute-write->transfer-read and transfer-write->shader-read barriers.
- Seam-split meshes get a gather through a device copy of `MeshSourceVertexForGpuVertex`, keyed
  by its remap revision.
- During a preview:
  - the block is pinned and marked as holding a preview (the GRAPHICS-154 resolver refuses it as
    canonical input);
  - bounds are widened conservatively or culling is bypassed;
  - primitive pick refinement is disabled for the entity;
  - dependent normals keep their CPU state.
- Abort restores the block from the current CPU state through a forced extraction re-upload.

## Acceptance criteria
- [ ] gpu;vulkan smoke: preview pixels move before commit and return on abort; a moved preview
      is not culled.
- [ ] Contract tests:
  - the resolver refuses a preview block;
  - pick refinement is off during a preview;
  - abort after a concurrent CPU edit shows the edit, not the old shadow.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
