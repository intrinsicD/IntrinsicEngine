---
id: GRAPHICS-117
theme: B
depends_on: []
---
# GRAPHICS-117 — Render-graph compile caching and gated debug dump

## Goal
- Stop recompiling the render graph from scratch every frame when its
  topology is unchanged: cache the `CompiledRenderGraph` keyed on the
  inputs that actually shape it (recipe features, sizing, import handles),
  and stop building the multi-KB debug dump string unconditionally per
  frame.

## Non-goals
- No framegraph-module algorithmic changes beyond what caching requires;
  per-compile allocation churn and quadratic scans are `GRAPHICS-120`.
- No pass-contribution seam changes (`GRAPHICS-116`); the cache key must
  simply incorporate whatever that seam makes variable.
- No multi-threaded recording (`GRAPHICS-119`).

## Context
- Owner/layer: `graphics/renderer` (`Graphics.Renderer` frame driver) with
  minimal support in `graphics/framegraph` if a compiled-graph reuse handle
  is needed.
- Today every frame runs `m_RenderGraph.Reset()` →
  `BuildDefaultFrameRecipe(...)` → `Compile()`
  (`src/graphics/renderer/Graphics.Renderer.cpp:2180, 2388, 2406`) although
  topology only changes when `FrameRecipeFeatures` flips, the swapchain
  resizes, or imports change identity. `Compile()` re-derives edges, culls,
  topo-sorts, synthesizes cross-queue timelines, and walks per-resource
  states every time (`Graphics.RenderGraph.Compiler.cpp:1301+`). The
  documented contract ("the default recipe is still rebuilt every frame",
  `docs/architecture/frame-graph.md`) must be updated to "rebuilt on
  change" in the same PR.
- Separately, `m_LastRenderGraphStats.DebugDump =
  BuildRenderGraphDebugDump(*compiled)` runs unconditionally each frame —
  a multi-KB `ostringstream` dump for a stats panel
  (`Graphics.Renderer.cpp:2497`, `Compiler.cpp:2091-2327`).
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  findings R16 (render half), R17.

## Required changes
- [ ] Define the compile key: `FrameRecipeFeatures` (post-override), render
      target sizing, imported-resource identities/formats, and any recipe
      state that reaches declaration; prove by construction (single funnel)
      that nothing else influences `BuildDefaultFrameRecipe`.
- [ ] Cache and reuse the compiled graph across frames when the key is
      unchanged; recompile transparently on change; fail-closed on any
      doubt (a wrong reuse must be impossible, not unlikely — hash equality
      plus a debug-mode full-compare assertion path).
- [ ] Keep per-frame work that is genuinely per-frame (transient device
      resource binding, barrier submission, recording) working against the
      cached compiled graph across frame-in-flight slots.
- [ ] Build the debug dump lazily: on stats-panel request or an explicit
      debug flag; default frames build no dump string.
- [ ] Expose compile-count/cache-hit counters in `RenderGraphFrameStats`.

## Tests
- [ ] Contract: steady-state frames after the first perform zero compiles
      (counter probe); flipping a feature, resizing, or changing an import
      identity recompiles exactly once.
- [ ] Contract: cached-path frame output identical to recompiled-path
      (golden compiled-description comparison after N reused frames).
- [ ] Existing recipe validation/renderer lifecycle suites stay green.
- [ ] PR-fast benchmark: CPU ms/frame of the declare+compile stage before/
      after for the default sandbox recipe.

## Docs
- [ ] Update `docs/architecture/frame-graph.md` frame-lifecycle wording
      (rebuild-on-change; cache key contract).
- [ ] Update `src/graphics/renderer/README.md`.

## Acceptance criteria
- [ ] Zero steady-state compiles proven by counters in a contract test.
- [ ] Benchmark evidence recorded in this file.
- [ ] Recipe-change/resize behavior unchanged; CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Stale compiled-graph reuse after any key-relevant change (fail closed).
- Perf claims without benchmark numbers.
- Removing recipe validation from the recompile path.
