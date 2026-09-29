---
id: GRAPHICS-154
theme: I
depends_on: [GRAPHICS-153]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: planned from the operator's GPU residency decision and two independent design reviews (2026-09-29, ADR 0030); implementation owes the contract tests and gpu;vulkan smokes listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources]
---
# GRAPHICS-154 — Completion-tracked GPU lifetimes and the resident position resolver

## Goal
- ADR 0030 decisions 3-4, first slice.
- Borrowed GPU ranges and residency slots are retired only after their producer and consumer
  completions: frame fences, immediate/transfer timeline values, and later the preview copy.
- Row-aligned `v:position` of a live point cloud or graph resolves to its `GpuWorld` block
  instead of an upload:
  - `PositionByteOffset` in `GpuGeometryResidencyView`;
  - a `RenderExtractionCache` query exposed through an `EditorFeatureBindings` binding.
- Borrowed blocks are pinned, and compaction treats pinned ranges as occupied destinations.
- First consumer: the `SpatialIndexCache` GPU build (Property space) reads positions from the
  block and skips its `SpatialIndex.Source` upload.
- `geometry.property-coherence` is amended here (resident reuse).

## Acceptance criteria
- [ ] Lifetime records cover frame, immediate and transfer completions; a contract test shows a
      range is not released while an immediate submit that reads it is pending.
- [ ] The resolver refuses stale revisions, seam-split meshes, dead geometry and blocks carrying a
      preview (the preview part is exercised in GRAPHICS-156).
- [ ] IO counter: a resident entity's GPU index build uploads zero position bytes.
- [ ] gpu;vulkan smoke: an LBVH built from the pool is bitwise equal to one built from an upload;
      its queries match.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
