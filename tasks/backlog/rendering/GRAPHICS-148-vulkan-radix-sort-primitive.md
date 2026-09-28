---
id: GRAPHICS-148
theme: I
depends_on: []
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note planned with Fable 5.1 and Codex (2026-09-28); implementation owes its own tests, gpu;vulkan smokes and sealed evidence.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-148 — Vulkan radix sort and sorted-segment primitives

## Goal
- A reusable, stable LSD radix sort in `Graphics.ComputeParallelPrimitives` (uint32 keys +
  uint32 payload; uint64 keys as two 32-bit passes; 4-bit digits):
  `parallel_radix_histogram.comp` -> existing `parallel_prefix_scan.comp` ->
  `parallel_radix_scatter.comp` with workgroup-local stable ranks, plus
  `parallel_segment_heads.comp` so sorted keys replace GPU hash tables, and a sorted-segment
  reduction with fixed order (the existing `parallel_segmented_float_reduce.comp` scans every
  element per segment; keep it for small inputs).
- Today only the O(n log^2 n) bitonic `lbvh_sort.comp` exists. Consumers: METHOD-014
  (replace the racy hash), GEOM-112 spatially balanced ordering on GPU, METHOD-059, METHOD-062.

## Acceptance criteria
- [ ] CPU oracle (`std::stable_sort`) parity on empty, 1, non-power-of-two, multi-workgroup, all-equal and random 64-bit keys; payload order bitwise equal including ties.
- [ ] Dispatch plan and scratch sizing tested in the default gate (Null device); three repeated GPU runs identical.
- [ ] No subgroup-size assumption (portable workgroup path first; subgroup acceleration must keep order).

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'ComputeParallelPrimitives|RadixSort' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'ComputeParallelPrimitives|RadixSort' -L 'gpu|vulkan' --timeout 300
```
