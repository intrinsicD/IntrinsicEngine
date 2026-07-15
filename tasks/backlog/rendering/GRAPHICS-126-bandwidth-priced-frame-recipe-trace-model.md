---
id: GRAPHICS-126
theme: B
depends_on:
  - REVIEW-003
maturity_target: CPUContracted
---
# GRAPHICS-126 — Bandwidth-priced frame-recipe trace model

## Goal

- Test whether a trace-only byte/cache-line cost model predicts measured
  frame-recipe pass timings well enough to justify any later scheduling or
  layout experiment.

## Non-goals

- No pass reordering, scheduler heuristic, frame-graph compiler policy,
  resource aliasing decision, async-queue policy, barrier change, or main-loop
  behavior change.
- No runtime profiler framework, telemetry service, database, persistent trace
  registry, or new GPU timestamp RHI API.
- No engine instrumentation, render-pass timestamp wiring, capture injection,
  or modification of a renderer/frame-graph diagnostic surface to manufacture
  the task's input data.
- No fitting on the same traces used to report predictive quality.
- No performance claim from predicted bytes alone; held-out correlation with
  measured timings and declared hardware conditions is required.
- No assumption that a CPU cache-line model transfers unchanged to GPU cache
  hierarchies.

## Context

- Owner/layer: offline tools and rendering benchmarks consume two already
  captured, immutable, provenance-bound frame-recipe/resource trace plus timing
  datasets. The live frame graph remains unchanged, and this task assumes no
  engine per-pass timing surface exists.
- No deliverable from `GRAPHICS-125` or `CORE-008` is required. Their conceptual
  byte-accounting or CPU-plan-noise rationale does not create a task dependency
  for this trace-only experiment.
- The transfer hypothesis comes from the Issue 445 merged-node BVH work: a
  cost model that prices data movement may explain performance better than an
  operation/pass count. Frame-recipe scheduling has broader compiler/database
  prior art, so this task is an evidence probe, not a novelty claim.
- Sources: [Memory-Efficient BVHs with Merged Nodes](https://graphics.cs.utah.edu/research/projects/bvh-merged-nodes/),
  [MLIR data-layout rationale](https://mlir.llvm.org/docs/Rationale/Rationale/),
  [Graphics Programming Weekly Issue 445](https://www.jendrikillner.com/post/graphics-programming-weekly-issue-445/),
  and the local `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`.

## Right-sizing

- Element under evaluation: one pure function/tool that maps a frozen trace and
  declared hardware assumptions to per-pass predicted costs.
- Simpler alternative: operate on exported JSON/records and emit result JSON;
  do not add a live service or scheduler integration.
- Blast radius: offline tool, trace schema/fixtures, benchmark manifest/runner,
  and evidence report only.
- Start/kill rule: before model/schema implementation, two externally captured
  Vulkan datasets from distinct declared devices must already be accessible and
  must bind every timing sample to a frozen trace, capture tool/version,
  device/driver, scene/recipe revision, resolution, warmup, and content hash.
  Missing, unalignable, or provenance-insufficient data closes the task without
  adding instrumentation or model code.
- Reintroduction trigger: a scheduler/layout task may open only if the held-out
  predictive threshold passes on multiple hardware captures.

## Required changes

- [ ] Before implementation, inventory and freeze two externally captured
      provenance-bound trace/timing datasets from distinct Vulkan devices. Each
      must contain repeated cold/warm samples, stable pass identity, declared
      capture tool/version and export procedure, device/driver/preset,
      scene/recipe commit, resolution, and hashes for the raw trace and timing
      export. If either dataset is missing or cannot be aligned pass-for-pass,
      record the negative start-gate verdict and close without code.
- [ ] On the passing branch, define a versioned immutable trace input containing
      pass identity,
      resource kind/format/extents, declared read/write ranges, transfer class,
      reuse distance where observable, queue, barriers, and existing measured
      pass timing. Consume only the frozen external captures; do not add or
      modify an engine timing/trace surface.
- [ ] Implement a deterministic trace-only model that estimates bytes moved,
      cache lines touched, read/write amplification, and a scalar predicted
      cost per pass and frame from explicit configurable hardware assumptions.
- [ ] Establish operation/pass-count and resource-byte-count baselines. Fit any
      model coefficients on training captures only, freeze them, then evaluate
      held-out scenes/resolutions/camera traces.
- [ ] Preserve the two source datasets' raw provenance and pass-alignment map
      through normalized inputs and results. Keep vendor/device/driver/preset,
      capture tool/version, scene/recipe revision, resolution, hashes, and
      warmup conditions in result diagnostics.
- [ ] Emit per-pass/frame predicted bytes/cache lines, measured timing median
      and spread, Pearson and Spearman correlation, rank error, and normalized
      absolute error for the proposed and baseline models.
- [ ] Add stable workloads `rendering.frame_recipe.bandwidth_model.smoke` for
      schema/math determinism over checked trace fixtures and
      `rendering.frame_recipe.bandwidth_model.nightly` for multi-device
      predictive evidence.
- [ ] Preregister the promotion threshold: on held-out captures from both
      devices, Spearman rank correlation must be at least 0.70 and exceed the
      better baseline by at least 0.10, with lower normalized error. Failure is
      a valid result and forbids scheduler/reordering follow-up claims.
- [ ] Record sensitivity to cache-line size, bandwidth, compression/tiling
      assumptions, resolution, and warm/cold state; flag parameters whose
      modest change reverses the conclusion.

## Tests

- [ ] Unit tests cover trace schema validation, overflow-safe byte arithmetic,
      format/extents calculations, partial ranges, read-modify-write, empty
      traces, unsupported resources, and deterministic predictions.
- [ ] Regression fixtures pin known hand-computed byte/cache-line estimates and
      train/held-out separation.
- [ ] Dataset-contract tests reject missing provenance, duplicate or unstable
      pass identities, mismatched trace/timing hashes, incomplete cold/warm
      samples, cross-device identity collisions, and train/held-out leakage.
- [ ] Smoke is CPU-only and thresholded for schema/math correctness, not host
      timing; multi-device timing/correlation is nightly evidence.
- [ ] Manifests/results validate and name datasets, warmups, parameters,
      baselines, devices, metrics, and diagnostics.

## Docs

- [ ] Document trace schema, model equations, hardware assumptions, fitting
      protocol, capture provenance/export procedure, pass-alignment rules,
      held-out split, benchmark IDs, and sensitivity limits.
- [ ] Add an evidence report with per-device baseline/model correlations,
      error distributions, promotion-threshold verdict, and explicit warning
      that correlation does not establish causal scheduling benefit.
- [ ] If the start gate fails, record the missing/unusable dataset condition and
      state that no instrumentation, trace surface, or model implementation
      landed.

## Acceptance criteria

- [ ] Exactly one closure branch is recorded: either two qualifying external
      datasets pass the start gate, or the task closes with a negative data
      verdict and no instrumentation/model code.
- [ ] On the implementation branch, the model deterministically reproduces
      hand-computed trace fixtures and emits schema-valid result JSON.
- [ ] On the implementation branch, training and held-out traces are disjoint;
      raw/normalized hashes, pass alignment, capture procedure, timing
      conditions, and hardware identities are explicit.
- [ ] On the implementation branch, the report applies the preregistered
      predictive threshold against both baselines on both device capture sets,
      including negative or sensitivity-invalidated results.
- [ ] No scheduler, frame graph, RHI, Vulkan, recipe ordering, or runtime
      behavior is changed.
- [ ] Default CPU and structural gates pass.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'BandwidthModel|FrameRecipe' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes

- Pass reordering, scheduler scoring, queue-affinity changes, aliasing policy,
  barrier changes, or new runtime/RHI profiling surfaces.
- Adding renderer/frame-graph instrumentation, wiring the dormant profiler
  surface, collecting replacement data inside this task, or continuing with
  fewer than two provenance-complete external datasets.
- Fitting and evaluation on the same traces or omitting negative results.
- Treating correlation or estimated cache lines as proof of causal speedup.
- Opening a scheduling/layout follow-up when the predictive threshold fails.
- New manager/service/registry/database infrastructure for traces.

## Maturity

- Target: `CPUContracted`; the offline evidence endpoint includes a valid
  negative closure when the external-data start gate fails. No `Operational`
  follow-up is owed.
