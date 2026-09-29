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
# GRAPHICS-154 — Completion-tracked GPU lifetimes and residency inputs

## Goal
- ADR 0030 decisions 3-4, first slice.
- GPU buffers are rewritten or freed only after their producer and consumer completions: frame
  fences, immediate/transfer timeline values, and later the render copy and direct render reads.
- Canonical input slots: a property is uploaded CPU -> residency once per CPU revision and reused
  while the revision holds. Render buffers are never method inputs.
- First consumer: `SpatialIndexCache` takes its GPU positions from the canonical slot instead of
  its own `SpatialIndex.Source` upload. Every index and query over the same entity, property and
  revision then shares one GPU copy.
- `geometry.property-coherence` is amended here (resident reuse, observing renderer).

## Acceptance criteria
- [ ] A contract test shows a slot is not released while an immediate submit that reads it is
      pending.
- [ ] IO counters:
  - two GPU methods in a row on the same revision upload the positions once;
  - a revision bump uploads once more.
- [ ] gpu;vulkan smoke: an LBVH built from the residency is bitwise equal to one built from
      today's upload; its queries match.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
