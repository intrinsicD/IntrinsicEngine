---
id: GRAPHICS-117
theme: B
depends_on: []
maturity_target: Operational
---
# GRAPHICS-117 — Render-graph compile caching and gated debug dump

## Status
- Active: 2026-07-04. Owner: Codex on `main`.
- Slice A complete: renderer compile-cache contract and lazy debug dump gate.
- Next slice: Slice B — PR-fast declare/compile benchmark evidence and task
  retirement if the cache contract remains stable.
- Slice A evidence:
  `cmake --preset ci`;
  `cmake --build --preset ci --target IntrinsicTests`;
  explicit `ctest --test-dir build/ci --output-on-failure -R 'GraphicsRenderer' --timeout 60`
  (10/10 passed; this mock/null renderer suite currently inherits
  `gpu|vulkan|slow` labels);
  focused `RendererFrameLifecycle|FrameRecipeContract|RenderGraphDebugDump|SandboxEditorUi`
  CPU-supported CTest (219/219 passed);
  full CPU-supported CTest (3493/3493 passed).

## Slice plan
- **Slice A (this slice).** Add the renderer-owned default-frame recipe cache
  key, cache/reuse the compiled graph while the key is unchanged, publish
  per-frame compile-attempt/cache-hit counters, and make the compiler debug
  dump opt-in through an explicit renderer request seam. Update CPU/null
  renderer contracts and docs for rebuild-on-change behavior. Defers
  benchmark evidence and final task retirement to Slice B.
- **Slice B.** Add PR-fast benchmark evidence for declare/compile CPU time,
  tighten any remaining cache-key diagnostics, run the full CPU gate, and
  retire the task if the cache contract is stable.

## Goal
- Stop recompiling the render graph from scratch every frame when its
  topology is unchanged: cache the `CompiledRenderGraph` keyed on the
  inputs that actually shape it (recipe features, sizing, import
  availability/shape, contribution descriptors), rebind current imported
  handles on reuse, and stop building the multi-KB debug dump string
  unconditionally per frame.

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
- Before Slice A every frame ran `m_RenderGraph.Reset()` →
  `BuildDefaultFrameRecipe(...)` → `Compile()`
  (`src/graphics/renderer/Graphics.Renderer.cpp:2180, 2388, 2406`) although
  topology only changes when `FrameRecipeFeatures` flips, the swapchain
  resizes, or imports change identity. `Compile()` re-derives edges, culls,
  topo-sorts, synthesizes cross-queue timelines, and walks per-resource
  states every time (`Graphics.RenderGraph.Compiler.cpp:1301+`). The
  documented contract now says the default recipe is rebuilt on
  key-relevant change. Slice A keeps this in renderer-owned state without
  changing the framegraph compiler.
- Separately, Slice A gates `m_LastRenderGraphStats.DebugDump =
  BuildRenderGraphDebugDump(*compiled)` behind
  `IRenderer::SetRenderGraphDebugDumpEnabled(...)`; default frames leave the
  dump string empty.
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  findings R16 (render half), R17.

## Required changes
- [x] Define the compile key: `FrameRecipeFeatures` (post-override), render
      target sizing, imported-resource availability/shape, and any recipe
      state that reaches declaration; prove by construction (single funnel)
      that nothing else influences `BuildDefaultFrameRecipe`.
- [x] Cache and reuse the compiled graph across frames when the key is
      unchanged; recompile transparently on change; fail-closed on any
      doubt. Slice A uses structural key equality instead of hashing, so
      there is no hash-collision reuse path.
- [x] Keep per-frame work that is genuinely per-frame (current imported handle
      rebinding, transient device resource binding, barrier submission,
      recording) working against the cached compiled graph across
      frame-in-flight slots.
- [x] Build the debug dump lazily: on stats-panel request or an explicit
      debug flag; default frames build no dump string.
- [x] Expose compile-count/cache-hit counters in `RenderGraphFrameStats`.

## Tests
- [x] Contract: steady-state frames after the first perform zero compiles
      (counter probe); flipping a feature or resizing recompiles exactly once,
      and changing an imported handle reuses the compiled graph while rebinding
      the current handle.
- [x] Contract: cached-path frame output identical to recompiled-path
      (golden compiled-description comparison after N reused frames).
- [x] Existing recipe validation/renderer lifecycle suites stay green.
- [ ] PR-fast benchmark: CPU ms/frame of the declare+compile stage before/
      after for the default sandbox recipe.

## Docs
- [x] Update `docs/architecture/frame-graph.md` frame-lifecycle wording
      (rebuild-on-change; cache key contract).
- [x] Update `src/graphics/renderer/README.md`.

## Acceptance criteria
- [x] Zero steady-state compiles proven by counters in a contract test.
- [ ] Benchmark evidence recorded in this file.
- [x] Recipe-change/resize behavior unchanged; CPU gate green.

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
