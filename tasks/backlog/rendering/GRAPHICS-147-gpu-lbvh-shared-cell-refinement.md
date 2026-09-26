---
id: GRAPHICS-147
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Backlog record of a known GPU tree-quality gap; any speed claim from its implementation needs its own benchmark evidence.
contract_schema: 1
contracts: []
contract_review: GPU build-quality change under the existing point-LBVH query contract; results, limits and integration surfaces stay unchanged.
---
# GRAPHICS-147 — Refine shared Morton cells in the GPU point LBVH

## Goal

Give the Vulkan point LBVH the same shared-cell refinement as the CPU tree, so
clustered inputs and far outliers no longer degrade GPU traversal.

## Current state

The CPU `Geometry.PointLBVH` gives points that share a 30-bit Morton code
further Morton digits over their group's own bounds and splits on the common
prefix of the digit strings (see
[spatial indices](../../../docs/architecture/spatial-indices.md#construction-and-limits)).
The GPU build (`lbvh_morton.comp`, `lbvh_sort.comp`, `lbvh_build.comp`) still
appends the source index directly after the first digit. Inside a shared cell
its splits follow the source index, which is spatially random.

On the CPU, before refinement, 90,000 points with one far outlier took
895 µs per k=16 query; after refinement 6.6 µs. Strongly clustered data went
from 114 µs to 7.4 µs. These are single local measurements in a standalone
build, not benchmark evidence. GPU query results are exact either way; only
traversal cost is affected.

## Acceptance criteria

- [ ] Choose a GPU-suitable refinement (e.g. segmented re-quantization of equal-code runs with a bounded digit count) and keep the traversal stack bound consistent with the resulting depth.
- [ ] Keep `Test.PointLBVHGpuSmoke` exact-parity checks passing, and add a clustered/outlier fixture to them.
- [ ] Measure clustered and outlier workloads against the current GPU build before claiming a speedup.

## Verification

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -R PointLBVHGpuSmoke --timeout 120
```
