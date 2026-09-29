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

## Completion — 2026-09-29
Commit: on `claude/cpd-nystrom` (see RETIREMENT-LOG). `Graphics.GpuPropertyResidency`: canonical
slots keyed ECS-blind (scope, owner, domain, kind, name). A slot is uploaded once per CPU
revision and shared through leases; a new revision replaces it, and a still-held old slot is
retired until released. `SpatialIndexCache` owns the instance and exposes it; a property-space
index over every row reads its positions from the slot. Transformed or compacted indices keep a
private upload, and `Prune` releases slots of dead entities.

Lifetime finding: Vulkan fence signals cover every earlier submission on the queue, so the
device's frame-deferred destruction already protects buffers read by immediate submits.
Canonical slots are never rewritten in place (new revision = new buffer), so no extra
completion records are needed here. In-place rewrites are the rings' problem (GRAPHICS-155).

Evidence:
- Mock-device contract tests (one upload per revision, hits, replacement with held leases,
  prune, refusals).
- gpu;vulkan smoke: kNN over the canonical slot is bitwise equal to the same queries over a
  private own-upload workspace; the same revision uploads nothing; a new revision uploads once.
- Full GPU suite 123/124 (known Xephyr LSan environment failure); CPU gate 5258/5258.
- property-coherence.md amended; catalog proofs added. Operational.

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
- [x] A slot is not released while it is held (lease), and GPU readers are protected by the
      device's frame-deferred destruction (fences cover earlier immediate submits); contract test
      on leases, finding recorded above.
- [x] IO counters:
  - two GPU methods in a row on the same revision upload the positions once;
  - a revision bump uploads once more.
- [x] gpu;vulkan smoke: an LBVH built from the residency is bitwise equal to one built from
      today's upload; its queries match.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
