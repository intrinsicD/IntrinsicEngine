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
  - eviction as an LRU cache with idle time and byte budget (ADR 0030 decision 4);
  - IO counters (upload/readback bytes and counts, reuse hits, swaps, ring waits, dropped
    previews).
- It uses device-local `BufferManager` leases and keeps the property's own type. A float
  presentation view is derived separately.
- Runtime free functions `ResolveGpuPropertyInput` / `AcquireGpuPropertyOutput` in
  `GeometryIntegration`.

## Completion — 2026-09-30
Commit: on `claude/cpd-nystrom` (see RETIREMENT-LOG). `Graphics.GpuPropertyResidency` is typed
and device-local:
- `GpuPropertyLayout` (scalar type, channels, stride, count, row map) is the slot identity; a
  mismatch is a miss, never a reinterpretation. A packed stride and stride 0 are one identity.
- Canonical slots upload once per revision through the transfer queue; a refused upload caches
  nothing and is counted (the caller defers; no synchronous fallback of unknown outcome).
- Output rings: `AcquireBack` / `Publish` / `Discard` / `Front` / `BindRevision`, depth 1..3 per
  key. An exhausted ring returns empty and counts a dropped preview; the front is never rewritten.
- Completions: frame use (reusable once `GlobalFrame - frame > FramesInFlight`, because the
  counter advances at EndFrame and the fence is waited one BeginFrame later), transfer and
  readback tokens, and leases.
- LRU cache: `Tick()` evicts idle canonical slots after T and, over budget, the size-weighted
  least recently used ones. Rings, pending, leased and observed slots stay. Config
  `render.gpu_property_idle_evict_seconds` (60) and `render.gpu_property_budget_megabytes`
  (256); injected clock.
- Counters: uploads, refusals, readbacks, hits, misses, evictions, releases, resident bytes,
  publishes, ring waits, dropped previews.
- Runtime: `Runtime.GpuPropertyBinding` (`MakeGpuPropertyKey`, `MakeGpuPropertyLayout`,
  `ResolveGpuPropertyInput`, `AcquireGpuPropertyOutput`); `SpatialIndexCache` keeps owning the
  residency, uses the shared layout constructor and ticks it from its maintenance hook.

Evidence:
- 15 `GpuPropertyResidency.*` and 2 `GpuPropertyBinding.*` mock-device contract tests cover
  every criterion above, including the frame boundary and front protection without a lease.
- Implemented by Fable 5.1; reviewed by Codex 6 Astra (medium). Four findings fixed (frame
  completion boundary, stride identity, unknown-outcome fallback upload, a vacuous front test);
  re-review clean.
- CPU gate 5272/5272. GPU suite under Xephyr: 122 passed, 1 opt-in skip, only the known
  environmental `VulkanShutdownLsanContract` red; `GRAPHICS154PropertyResidency` passes on the
  device-local transfer path.
- Maturity: canonical input path Operational (gpu;vulkan smoke). Rings are contract-tested
  only; their first GPU consumer and smoke is RUNTIME-292.

## Acceptance criteria
- [x] Null-device contract tests:
  - resolving twice uploads once;
  - a revision bump uploads once more;
  - a double property stays double;
  - a slot is not rewritten before its completions;
  - an exhausted ring drops a preview instead of blocking.
- [x] Discard frees only after pending readbacks complete.
- [x] Canonical slots form an LRU cache with an idle timeout and a byte budget (config section,
      injected clock):
  - an idle slot is evicted after T;
  - over budget, the least recently used slots go first (size-weighted);
  - rings, pending slots and currently observed slots are never evicted;
  - an evicted slot re-uploads once on its next use;
  - hit, miss, eviction and resident-byte counters are reported.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
