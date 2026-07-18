---
id: CORE-008
theme: F
depends_on:
  - CORE-005
  - CORE-007
maturity_target: Operational
---
# CORE-008 — Compiled task-graph plan reuse across executions

## Goal
- Stop recompiling structurally-unchanged CPU task graphs every execution:
  `Core.Dag.TaskGraph` exposes an explicit registration-replay reset that
  retains one compiled topology, rebinds current callbacks, and reuses the
  plan only after exact structural equality. The fixed-step ECS `FrameGraph`
  in `Engine` and `Graphics.RenderPrepPipeline` adopt that path.

## Non-goals
- Render-graph (framegraph) compile caching — owned by `GRAPHICS-117`.
- New execution APIs (`CORE-005`) or scheduler changes (`CORE-007`).
- Caching across *changed* pass sets (a structural change recompiles; no
  partial/incremental recompile in this task).
- No semantic change to destructive `Reset()` or the 2,000-node cold-compile
  Architecture SLO; callers opt into reuse with `ResetForReplay()`.
- No hash-only validity decision, global pass-name interner, or pooled
  `ExecutionState`. Exact equality is required, dynamic-name interning is
  unbounded machinery, and copied `TaskGraphCompletion` handles may outlive a
  later submission.

## Context
- Owner/layer: `core` for the cache mechanism; `runtime` and
  `graphics/renderer` for adoption.
- Before CORE-008, every fixed sim tick re-registered the same three ECS system passes
  and runs `Compile → Execute → Reset`
  (`src/runtime/Runtime.Engine.FrameLoop.Internal.hpp:326-370`,
  `src/runtime/Runtime.EcsSystemBundle.cpp:15-34`), and
  `RenderPrepPipeline` constructs a
  brand-new 9-pass `TaskGraph` per `Run()`
  (`src/graphics/renderer/Graphics.RenderPrepPipeline.cpp:235`). Each
  `Compile()` rebuilds successor/predecessor vectors, edge sets/reason
  metadata, and the priority queue. The graph already has a `CanUsePlan()`
  notion, but destructive `Reset()` discards everything each tick.
- The three-pass benchmark shape models the baseline ECS bundle. The live
  fixed-step graph may be larger because app and boot-finalized runtime-module
  passes register every tick; unchanged shapes reuse and any add/remove/access
  change must miss transparently while installing fresh per-tick closures.
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R16 (CPU half), scheduler finding 7.
- ARCH-013 re-review (2026-07-08): Decision unchanged. Plan reuse remains a
  core DAG optimization; the runtime adoption point must respect the retired
  `RunFrame()` seam and avoid adding new command drains, event pumps, or
  module-owned scheduling paths.

## Status

- Completed on 2026-07-18 at `Operational`; owner: Codex; implementation
  branch: `codex/core-008-compiled-plan-reuse`. Activation/right-sizing commit:
  `88b1edb4`; frozen-harness and baseline commits: `e8df606f` and `78382f1b`;
  production implementation commit: `8a50b725`; owner-thread regression
  commit: `28ffc72e`; benchmark report/evidence commit: `49678b53`.
- Right-sizing decision: use the existing `AddPass` registration surface plus
  explicit `ResetForReplay()`. Retain two bounded pass-node banks so names and
  declaration storage survive replay; compare ordered descriptors exactly
  before reusing topology and move only fresh callbacks on a hit.
- Resource equality records declaration origin and identity (explicit handle,
  type token, or StringID), not only normalized `ResourceId`; otherwise
  `Write<A>` and `Write<B>` can both normalize to `{0,1}` after a reset and
  falsely reuse a plan. Raw wait/signal StringIDs are likewise structural.
- The allocation work deferred by `CORE-007` is narrowed to eliminating
  transient ready-list vectors/fixed callback trampolines where this can be
  done without reusing completion state. Whole-state pooling is dropped:
  copied completions and retiring worker closures make exclusive reuse a
  separate lifetime problem without present evidence.
- Benchmark sequence: land the replay API/stats scaffold with a full-rebuild
  fallback and freeze two stable-ID `ci-release` workloads; capture that exact
  scaffold revision, then implement reuse without modifying the harness and
  compare repeated matched-host captures.
- Frozen baseline commit: `e8df606f794dc8a77aa03e962fa8f834705effb0`.
  Five sequential, strict-validated `ci-release` runs recorded 6,146 plan
  builds and zero reuse hits per workload with zero quality error. ECS-like
  runtime was 0.001020–0.001335 ms/epoch (median 0.001036); render-prep-like
  runtime was 0.002532–0.002630 ms/epoch (median 0.002624). The preserved
  baseline binary SHA-256 is
  `15e536b748d8b99679fe51329e42dc7bdd9b2a6f205ce97170ea4b28a49f539a`.
- The preserved production-candidate binary at `8a50b725` has SHA-256
  `5fd26220e157100d777e6ecbef3df72e52554fab61402ebe51e5fa5644b0b498`;
  every candidate capture recorded one plan build plus 6,145 exact reuses,
  zero quality error, zero failures, and the same plan-order/callback-rebind
  checksums as baseline.
- Five order-balanced matched pairs reduced median paired
  registration/compile/replay runtime by 84.562% for the three-pass ECS-like
  shape and 84.938% for the nine-pass render-prep-like shape. All 250 emitted
  payloads validated strictly. Full distributions, exact effect sizes,
  provenance, limitations, and reproduction live in
  `benchmarks/reports/core_taskgraph_plan_reuse_CORE-008.md`.
- Required verification passed with Clang 23: the default CPU selector
  completed 4,096 tests with zero failures and one expected GLFW/LSan skip;
  fresh serial grouped ASan and UBSan selectors each completed 2,750 tests
  with zero failures (UBSan skipped the one ASan-only lifecycle case); the
  destructive-reset 2,000-node SLO reported 126,887 ns compile p99 against
  350,000 ns and 793,500 ns execute p95 against 8,333,334 ns.
- The canonical Null `Engine::Run()` integration path observed one fixed-step
  plan build followed by reuse, and the persistent render-prep regression
  proved owner-thread callbacks across initial and replay runs with two
  scheduler workers. Independent completion and final code reviews found no
  actionable issue. The generated module inventory remained byte-identical.

## Required changes
- [x] Add fail-closed `TaskGraph::ResetForReplay()` and `FrameGraph` forwarding
      while preserving destructive `Reset()`. Replay must expose a logically
      empty registration surface, destroy stale callbacks, and retain only the
      last successful topology plus reusable declaration storage.
- [x] Compare pass count/order, owned names, compilation-affecting options,
      resource origin/identity/mode, explicit dependencies/reasons, and raw
      wait/signal labels exactly. Callback identity is excluded and callbacks
      are rebound every epoch; mismatch or compile failure can never execute
      the previous plan.
- [x] Expose plain plan-build/reuse counters and a last-reuse flag; a reuse hit
      reports zero topology compile time. Fix empty compiled graphs so they are
      valid plans.
- [x] Reuse the two retained pass-node banks instead of adding a global name
      interner. Eliminate eager per-successful-compile edge-reason metadata and
      per-submission ready-list/callback-wrapper churn only where the change is
      local and preserves escaped completion lifetime.
- [x] Adopt in `Engine::RunFixedStepSimulationTicks`: unchanged
      pass set (the common `OnSimTick`-adds-nothing case) executes with zero
      recompiles; app-added passes trigger recompile transparently.
- [x] Adopt in `Graphics.RenderPrepPipeline`: reuse the pipeline graph
      across `Run()` calls and clear all stack-borrowing callbacks on every
      success/fault exit.

## Tests
- [x] Contract: unchanged replay executes without recompiling (compile-count
      counter exposed via stats), uses freshly rebound callbacks, and preserves
      dependency order. An old completed handle remains ready during a later
      replay submission.
- [x] Contract: adding/removing a pass or changing options, explicit edges,
      declared access identity/mode, or labels after reuse invalidates the plan
      and recompiles; failed changed replay cannot fall back to the old plan.
- [x] Runtime/graphics contracts: two ECS-bundle epochs bind the current scene,
      app/module shape transitions recompile, repeated RenderPrep runs use
      current inputs, and forced failures leave the next run valid.
- [x] Null-runtime integration: at least two real fixed ticks observe one plan
      build plus a reuse hit; existing frame-graph, ECS-system, runtime-module,
      render-prep, and cold-compile Architecture SLO suites stay green.
- [x] Matched optimized smoke benchmarks: separate stable IDs for the
      3-pass ECS-like and 9-pass render-prep-like shapes. PR-fast coverage
      remains deterministic contract tests; performance runs in `ci-release`.

## Docs
- [x] Update `docs/architecture/task-graphs.md` (plan reuse contract,
      invalidation rules).
- [x] Update `src/core/README.md`, runtime fixed-step documentation, renderer
      prep documentation, and benchmark package/report indexes.

## Acceptance criteria
- [x] After the first fixed step, the steady-state sandbox path performs zero
      additional task-graph topology builds (proven by plan counters on the
      Null runtime path; each replay still makes an accepted `Compile()` call).
- [x] Both matched workloads record baseline/candidate distributions,
      structure/callback quality error zero, and explicit effect sizes.
- [x] Default CPU, serial ASan/UBSan, and cold-compile Architecture SLO gates
      green; no layering changes.

## Maturity

- Reached: `Operational`. Deterministic core contracts, the canonical Null
  `Engine::Run()` fixed-step path, and the persistent renderer-prep owner all
  exercise exact replay. No Vulkan-specific follow-up is owed for this CPU
  scheduling optimization. Render-graph caching remains separately owned by
  `GRAPHICS-117`.

## Verification

- Operational proof:
  `RuntimeSandboxAcceptance.FixedStepTaskGraphBuildsOnceThenReusesPlan`
  (`integration;runtime;graphics`) passed 1/1 through the Null
  `Engine::Run()` path and observed one topology build followed by reuse.
- Renderer ownership proof: the two-worker
  `RenderPrepPipeline.TaskGraphReusesPlanAndRebindsCallbacks` contract passed
  for both initial build and replay and asserted every callback ran on the
  owner thread.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error \
  --timeout 60 --parallel 1
cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error \
  --timeout 60 --parallel 1
cmake --preset ci-release
cmake --build --preset ci-release \
  --target IntrinsicBenchmarkSmoke IntrinsicBenchmarkTests
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Stale-plan execution after structural mutation (invalidation must be
  fail-closed, never best-effort).
- Perf claims without the benchmark numbers.
- Touching the render graph (framegraph) — that is `GRAPHICS-117`.
- Reusing an `ExecutionState`, counter event, or callback capture while any
  prior completion/worker may still own it.
