---
id: GRAPHICS-118
theme: B
depends_on: []
---
# GRAPHICS-118 — Placed transient resource allocation with real memory aliasing

## Goal
- Make transient render-graph resources actually alias GPU memory: placed
  allocation (heap + offset) driven by the lifetime-interval analysis the
  compile path already performs, so peak transient memory approaches the
  max-live working set instead of today's
  `sum(all transients) × framesInFlight`.

## Non-goals
- No aliasing of imported/external resources — transients only.
- No change to barrier semantics beyond the aliasing hazards the plan
  requires (alias-reuse barriers are in scope; general barrier quality is
  `GRAPHICS-120`).
- No compile caching (`GRAPHICS-117`) — but the placement plan must be
  cache-friendly (deterministic for an unchanged graph).

## Context
- Owner/layer: `graphics/framegraph` (placement planning),
  `graphics/rhi` + `graphics/vulkan` (placed-resource creation),
  `graphics/renderer` (adoption).
- Today "transient aliasing" never reaches GPU memory: `TransientAllocator`
  pools virtual handles by exact desc match with no heap/offset placement
  (`src/graphics/framegraph/Graphics.RenderGraph.TransientAllocator.cpp:27-63`),
  and `AllocateFrameTransientResources` then creates/caches one real device
  texture/buffer per resource index per frame-in-flight slot
  (`src/graphics/renderer/Graphics.Renderer.cpp:6126-6219`), discarding the
  compiled handle aliasing. `SetTransientAliasingEnabled` only changes a
  CPU-side stat. The lifetime-interval sweep that placement needs already
  exists (`Graphics.RenderGraph.cpp:492-549`), and
  `TransientMemoryEstimateBytes` already reports an estimate (with a wrong
  BC-format size — fixed properly in `GRAPHICS-120`; placement must use RHI
  format sizing, not the local table).
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R15.

## Backends
- Backend axis: Vulkan placed allocation (VMA-backed memory blocks +
  `vkBindImageMemory`/`vkBindBufferMemory` at offsets) behind an RHI
  contract; Null device implements the contract as bookkeeping so the CPU
  gate can prove planning. No `Vk*` types cross RHI/renderer APIs.

## Required changes
- [ ] Slice A (planning, CPU-provable): compute a placed layout from the
      existing lifetime intervals — per-resource {block, offset, size,
      alignment} with non-overlapping live ranges, deterministic for a
      fixed graph; emit alias-reuse hazards (which pass first writes a
      reused range after its prior occupant's last read) into the barrier
      plan; expose planned peak bytes vs naive sum in stats.
- [ ] Slice B: RHI contract for memory blocks + placed texture/buffer
      creation with alignment/`memoryTypeBits` compatibility queries; Null
      backend bookkeeping implementation.
- [ ] Slice C: Vulkan implementation (VMA block allocation, placed binds,
      required aliasing barriers/initial-layout handling per reuse).
- [ ] Slice D: renderer adoption — `AllocateFrameTransientResources` binds
      compiled placements per frame-in-flight slot instead of per-resource
      allocations; `SetTransientAliasingEnabled` gates real behavior with
      the non-aliased path kept as the fallback/debug lane.

## Tests
- [ ] CPU/null contract: placement never overlaps live ranges (property
      test over randomized graphs); deterministic layout for a fixed graph;
      alias hazards emitted exactly where reuse occurs.
- [ ] CPU/null contract: planned peak bytes ≤ naive sum, and equals naive
      sum when aliasing is disabled.
- [ ] Opt-in `gpu;vulkan` smoke: default sandbox recipe renders correctly
      with aliasing on (image compare vs aliasing off), reported transient
      memory drops, and validation layers are clean (no hazard errors).

## Docs
- [ ] Update `src/graphics/framegraph/README.md` and
      `docs/architecture/frame-graph.md` (what "transient aliasing" now
      means, fallback lane, stats).

## Acceptance criteria
- [ ] Real measured transient memory reduction on the default sandbox
      recipe recorded in this file (before/after bytes).
- [ ] Validation-layer-clean Vulkan smoke cited as actually run.
- [ ] Aliasing-off fallback preserved and selectable; CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Operational (Vulkan-capable host only):
ctest --test-dir build/ci --output-on-failure -L 'gpu' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Passing `Vk*` types through RHI/renderer/framegraph public APIs.
- Enabling aliasing by default before the Vulkan smoke is cited.
- Aliasing imported resources.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for the
  placement planner everywhere. Slices A–B close `CPUContracted`; Slice C–D
  close `Operational` with the cited `gpu;vulkan` smoke. If sliced across
  PRs, the closing slice owns the smoke.
