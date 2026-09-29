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

## Acceptance criteria
- [ ] After Accept, extraction issues no position upload for a point cloud (IO counter); the
      render and CPU positions are equal.
- [ ] Undo/redo restores positions and uploads once each.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
