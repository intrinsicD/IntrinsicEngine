---
id: GRAPHICS-151
theme: I
depends_on: [GRAPHICS-148]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note split from GRAPHICS-148 (2026-09-29); implementation owes its own tests and gpu;vulkan smoke.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-151 — Sorted segment heads and fixed-order segment reduction

## Goal
- On radix-sorted records (GRAPHICS-148), mark segment heads (`key[i] != key[i-1]`) and
  compact them with the existing stream compaction into segment offsets, so sorted keys
  replace GPU hash tables (METHOD-014's racy hash, METHOD-059, METHOD-062).
- Reduce values per sorted segment in fixed order from those offsets; keep
  `parallel_segmented_float_reduce.comp` (one workgroup scans every element per segment)
  for small inputs.
- Reuse candidate found during GRAPHICS-148: `lop_grid_count.comp` / `lop_grid_scatter.comp`
  bucket points into cells with atomic cursors (order within a cell depends on scheduling);
  evaluate replacing them with a radix sort by cell key plus segment heads.

## Acceptance criteria
- [ ] CPU oracle and plan tests in the default gate; heads and offsets equal the CPU oracle on
      empty, single, all-equal and random keys (one and two key words).
- [ ] gpu;vulkan smoke: three runs identical; reduction in fixed order bitwise repeatable.
- [ ] LOP grid bucketing decision recorded (replace, or keep with the measured reason).

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'ComputeParallelPrimitives' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'ComputeParallelPrimitives' -L 'gpu|vulkan' --timeout 300
```
