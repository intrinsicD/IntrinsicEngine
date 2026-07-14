---
id: GRAPHICS-117
theme: B
depends_on: []
maturity_target: Operational
completed: 2026-07-04
---
# GRAPHICS-117 — Render-graph compile caching and gated debug dump

## Completion
- Completed: 2026-07-04. Commit/PR: this retirement change.
- Maturity: `Operational` for the default renderer compile-cache contract and
  debug-dump request seam. The renderer recompiles on key-relevant recipe,
  sizing, import-shape, and contribution changes, reuses a cached
  `CompiledRenderGraph` for steady-state frames, and still rebinds current
  imported handles per frame.
- Summary: `Graphics.Renderer` now owns a structural default-frame recipe
  compile key, records per-frame compile/cache-hit counters, reuses cached
  compiled graphs when topology is unchanged, invalidates on rebuild/shutdown/
  resize, and builds render-graph debug dumps only when explicitly requested.
  A PR-fast smoke benchmark records rebuild declare+compile cost against the
  steady-state cached compile-attempt contract.
- Evidence:
  `cmake --preset ci`;
  `cmake --build --preset ci --target IntrinsicTests`;
  explicit `ctest --test-dir build/ci --output-on-failure -R 'GraphicsRenderer' --timeout 60`
  (10/10 passed; this mock/null renderer suite currently inherits
  `gpu|vulkan|slow` labels);
  focused `RendererFrameLifecycle|FrameRecipeContract|RenderGraphDebugDump|SandboxEditorUi`
  CPU-supported CTest (219/219 passed);
  full CPU-supported CTest (3493/3493 passed);
  `python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict`;
  `cmake --build --preset ci --target IntrinsicBenchmarkSmoke`;
  `ctest --test-dir build/ci --output-on-failure -R 'IntrinsicBenchmarkSmoke' --timeout 60`
  (2/2 passed);
  `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  (3493/3493 passed);
  `build/ci/bin/IntrinsicBenchmarkSmoke build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeSliceB`;
  `python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeSliceB --strict`;
  `python3 tools/agents/check_task_policy.py --root . --strict`;
  `python3 tools/docs/check_doc_links.py --root .`;
  `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`;
  `python3 tools/repo/check_layering.py --root src --strict`;
  `python3 tools/repo/check_test_layout.py --root . --strict`;
  `python3 tools/repo/check_pr_contract.py`;
  `python3 tools/repo/check_root_hygiene.py --root .`
  (warning-mode only for pre-existing root entries `ara/` and `imgui.ini`).
  Local debug+sanitizer benchmark result for
  `rendering.frame_recipe_compile_cache.smoke`: `runtime_ms=1.122644`,
  `baseline_rebuild_declare_compile_ms=1.122644`,
  `cached_steady_state_declare_compile_ms=0.0`,
  `baseline_compile_attempts_per_frame=1`,
  `cached_compile_attempts_per_frame=0`, `pass_count=10`,
  `resource_count=26`, `barrier_count=10`,
  `validation_error_count=0`, `quality_error_l2=0.0`.

## Status
- Retired: 2026-07-04.
- Slice A complete: renderer compile-cache contract and lazy debug dump gate.
- Slice B complete: PR-fast declare/compile benchmark evidence added and CPU
  gate passed.

## Slice plan
- **Slice A.** Add the renderer-owned default-frame recipe cache
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
- [x] PR-fast benchmark: CPU ms/frame of the declare+compile stage before/
      after for the default sandbox recipe.

## Docs
- [x] Update `docs/architecture/frame-graph.md` frame-lifecycle wording
      (rebuild-on-change; cache key contract).
- [x] Update `src/graphics/renderer/README.md`.

## Acceptance criteria
- [x] Zero steady-state compiles proven by counters in a contract test.
- [x] Benchmark evidence recorded in this file.
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
