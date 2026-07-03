---
id: GRAPHICS-120
theme: B
depends_on: []
---
# GRAPHICS-120 — Framegraph compiler/executor efficiency and hygiene polish

## Goal
- Remove the per-compile/per-execute waste and small contract hazards inside
  `src/graphics/framegraph/`: allocation churn, quadratic scans, the
  `thread_local` validation side channel, the read-treated-as-write barrier
  state, and the duplicated/incorrect format-size table.

## Non-goals
- Compile caching (`GRAPHICS-117`), memory aliasing (`GRAPHICS-118`),
  parallel recording (`GRAPHICS-119`).
- No behavioral changes to compilation output except where a finding is a
  defect (the two hygiene items below, each pinned by a test).

## Context
- Owner/layer: `graphics/framegraph` (+ a small RHI format helper).
- Items (origin:
  `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R18):
  - Per-compile allocation churn: ~35 vectors + an `unordered_set` per
    `Compile`, every pass/resource name copied into fresh `std::string`s,
    per-`RenderPassRecord` access vectors reallocated on every rebuild
    (`Graphics.RenderGraph.Compiler.cpp:1363-1392, 1421, 2045-2079`;
    `Graphics.RenderGraph.cpp:712-714`); executor allocates
    `passNameByIndex`, queue wait/signal vectors, and two vectors per
    barrier packet per frame.
  - Quadratic scans: `emitBarriersForPass` rescans all `BarrierPackets`
    per pass × stage (`Graphics.RenderGraph.Executor.cpp:39-71`, duplicated
    in `Graphics.Renderer.cpp:2811-2845`); `FindOrCreateBarrierPacket`
    linear-scans on every insertion (`Compiler.cpp:816-833`); packets are
    already sorted (`SortBarrierPackets`, `Compiler.cpp:847`) so emission
    can be a linear cursor walk. Also O(n²) `ValidateUniquePassIds`
    (`Compiler.cpp:402-430`).
  - Hygiene: `thread_local RenderGraphValidationResult
    g_LastCompileValidationResult` global compile state and a
    misleadingly-`const` `Compile()` that mutates `m_Impl`
    (`Compiler.cpp:32`; `Graphics.RenderGraph.cpp:358, 442`).
  - Defect: `TextureUsage::ColorAttachmentRead` maps to
    `TextureBarrierState::ColorAttachmentWrite` (`Compiler.cpp:38-39`) —
    read-only color access over-synchronizes as a write state.
  - Defect: local `BytesPerPixel` duplicates RHI format knowledge and
    returns 4 for BC-compressed formats, so
    `TransientMemoryEstimateBytes` overestimates 4–8×
    (`Graphics.RenderGraph.cpp:414-440`); a stale recipe reference lives in
    framegraph doc text (`Compiler.cppm:98-101`).

## Required changes
- [ ] Persistent compiler scratch: reuse compile-scoped containers across
      compiles (member scratch or arena); intern names as
      `string_view`s into graph-owned storage; keep `RenderPassRecord`
      access vectors' capacity across `Reset()`.
- [ ] Linear barrier emission: cursor/index over the sorted packet list in
      both the executor and the renderer's duplicated path (dedupe into one
      shared helper while there); make packet insertion amortized O(1);
      sort-or-hash `ValidateUniquePassIds`.
- [ ] Replace the `thread_local` validation side channel: return the
      validation result in the `Compile()` `Expected` payload (or a caller
      out-param); make `Compile` non-`const` or move its mutations out.
- [ ] Map `ColorAttachmentRead` to a read barrier state with read-read
      transition skipping; pin with a barrier-plan test.
- [ ] Move format sizing to an RHI helper with correct block-compressed
      math; delete the local table; fix the estimate; scrub the recipe
      reference from framegraph comments.

## Tests
- [ ] Golden: compiled output (edges, order, barriers) unchanged for a
      fixture graph across the churn/scan changes.
- [ ] Contract: `ColorAttachmentRead` no longer emits a write-state
      transition between consecutive readers.
- [ ] Contract: BC-format transient estimate matches block math.
- [ ] Existing framegraph suites stay green.
- [ ] PR-fast benchmark: compile + emission CPU time before/after on a
      pass-heavy synthetic graph.

## Docs
- [ ] Update `src/graphics/framegraph/README.md` where contracts change
      (validation result plumbing, format sizing source).

## Acceptance criteria
- [ ] Zero `thread_local` compile state; validation results flow through
      the return path.
- [ ] Benchmark evidence recorded; steady-state per-frame allocations in
      compile/emit measurably reduced (counter or allocator probe).
- [ ] Both defect fixes pinned by tests; CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Changing compiled-graph semantics beyond the two pinned defect fixes.
- Perf claims without benchmark numbers.
- Introducing renderer/recipe knowledge into the framegraph module.
