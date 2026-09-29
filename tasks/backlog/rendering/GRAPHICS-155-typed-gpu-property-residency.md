---
id: GRAPHICS-155
theme: I
depends_on: [GRAPHICS-154]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: planned from the operator's GPU residency decision and two independent design reviews (2026-09-29, ADR 0030); implementation owes the contract tests and gpu;vulkan smokes listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources]
---
# GRAPHICS-155 — Typed GPU property residency (input side and output rings)

## Goal
- ADR 0030 decisions 1-2 and 4.
- `Graphics.GpuPropertyResidency` has these parts:
  - an ECS-blind key and a typed layout identity (scalar type, channels, stride, count, row map);
  - `AcquireInput` (one upload per CPU revision, reused while it holds);
  - `AcquireBack` / `Publish` / `Discard` / `Front` / `BindRevision`;
  - ring depth as a per-key policy;
  - slot reuse only after completion;
  - dropping or coalescing previews when the ring is exhausted;
  - eviction;
  - IO counters (upload/readback bytes and counts, reuse hits, swaps, ring waits, dropped
    previews).
- It uses device-local `BufferManager` leases and keeps the property's own type. A float
  presentation view is derived separately.
- Runtime free functions `ResolveGpuPropertyInput` / `AcquireGpuPropertyOutput` in
  `GeometryIntegration`.

## Acceptance criteria
- [ ] Null-device contract tests:
  - resolving twice uploads once;
  - a revision bump uploads once more;
  - a double property stays double;
  - a slot is not rewritten before its completions;
  - an exhausted ring drops a preview instead of blocking.
- [ ] Discard frees only after pending readbacks complete.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
