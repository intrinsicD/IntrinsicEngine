---
id: GRAPHICS-125
theme: B
depends_on:
  - REVIEW-003
  - GEOM-066
  - GEOM-067
maturity_target: CPUContracted
---
# GRAPHICS-125 — Memory-priced cluster hierarchy evidence

## Goal

- Determine whether a meshoptimizer cluster hierarchy combined with
  memory-priced packing produces a deterministic, exact, and favorable
  error-per-byte frontier under fixed camera traces before any GPU meshlet or
  virtualized-geometry surface is opened.

## Non-goals

- No `GpuGeometryRecord` fields, graphics upload/residency, frame-recipe pass,
  GPU LOD selector, HZB integration, mesh shader, visibility buffer, RHI
  capability, Vulkan code, cluster streaming, or software rasterization.
- No Nanite-parity claim and no reopening all implementation children of
  archived `GRAPHICS-044` or `GRAPHICS-056`.
- No runtime meshletization/LOD generation and no change to imported assets.
- No new hierarchy interface, builder registry, asset service, or backend
  selector.
- No adoption based solely on triangle reduction or node count; exact leaf
  reconstruction, projected error, memory, and traversal cost are required.

## Context

- Owner/layer: an offline evidence tool consumes public geometry data and the
  validated `GEOM-066`/`GEOM-067` evidence primitives. It emits plain hierarchy
  records and benchmark results; no production engine layer consumes them.
- Archived `GRAPHICS-044` planned meshlet records and archived `GRAPHICS-056`
  planned a bounded cluster DAG. Their GPU-facing implementation children stay
  closed; this task reopens only the CPU/evidence questions with current
  dependency and right-sizing policy.
- The current `assets -> core` invariant forbids placing a geometry-dependent
  authoring builder in `src/assets`. Evidence therefore lives under `tools/`
  and does not establish a new engine dependency edge.
- Sources: [meshoptimizer v1.2](https://github.com/zeux/meshoptimizer/releases/tag/v1.2),
  [Memory-Efficient BVHs with Merged Nodes](https://graphics.cs.utah.edu/research/projects/bvh-merged-nodes/),
  and [Graphics Programming Weekly Issue 445](https://www.jendrikillner.com/post/graphics-programming-weekly-issue-445/).
- Any meshoptimizer use comes through `GEOM-066`'s vcpkg-pinned evidence path;
  this task adds no second dependency route.
- meshoptimizer v1.2's `demo/clusterlod.h` is example code, not an installed
  public header of the normal vcpkg package. This task must not assume
  `clodBuildHierarchy` is available, copy that header, or widen the overlay to
  vendor demo code implicitly.

## Right-sizing

- Element under evaluation: one offline hierarchy record and one deterministic
  cut evaluator, confined to evidence tooling.
- Simpler alternative: flat vectors and free functions over meshlet/DAG
  records; no production asset format, interface, loader, or runtime owner.
- Blast radius: one tool, fixed camera traces/fixtures, benchmark manifest and
  report. `src/`, RHI, shaders, recipes, and runtime remain untouched.
- Start/kill rule: implementation starts only when `GEOM-066` records an
  operation-specific positive meshlet-oracle verdict and the pinned package
  exposes the required public meshoptimizer APIs. A general library adoption
  verdict based on tangents or simplification is insufficient. If that gate
  fails, record the reason and close this task without a hierarchy tool.
- Reintroduction trigger: GPU-facing implementation children may open only if
  exactness and preregistered error-per-byte thresholds pass.

## Required changes

- [ ] Before implementation, inspect the `GEOM-066` report and installed
      package surface. Continue only if its meshlet operation specifically
      passed deterministic exact round-trip, remained usable through the
      pinned dependency, and exposes the public construction/simplification/
      partitioning primitives this task needs. Otherwise write a short negative
      start-gate record and close without adding code, fixtures, or benchmarks.
- [ ] When the start gate passes, build deterministic leaf meshlets through the
      validated `GEOM-066` oracle. Construct the hierarchy with pinned public
      meshoptimizer APIs plus task-local plain bottom-up grouping; record source
      triangle provenance at every leaf. Do not consume `demo/clusterlod.h` or
      claim `clodBuildHierarchy` is part of the installed library API.
- [ ] Define plain experimental cluster records containing only parent/child
      indices, bounds, simplification error, source range/provenance, and the
      fields required to evaluate a cut; do not declare a production asset or
      GPU layout.
- [ ] Produce a conventional fixed-stride layout and a memory-priced packed
      layout using `GEOM-067`'s explicit byte/cache-line assumptions. Encode,
      decode, and validate both without pointer-based ownership.
- [ ] Evaluate deterministic monotone cuts over fixed orbit, dolly, fly-through,
      near-plane crossing, and rapid camera-transition traces at declared pixel
      error budgets.
- [ ] Verify leaf-level exact source-triangle reconstruction, parent/child
      acyclicity/reachability, finite conservative bounds, monotone error, and
      no duplicate or missing selected source regions in every cut.
- [ ] Emit build time, hierarchy/packed bytes, estimated cache lines, selected
      cluster/triangle counts, projected error distribution, cut-evaluation
      time, and overflow/invalid diagnostics for both layouts.
- [ ] Add stable benchmarks
      `rendering.cluster_hierarchy.memory_priced.smoke` and
      `rendering.cluster_hierarchy.memory_priced.nightly`. The smoke uses fixed
      checked fixtures/traces/seeds, declared warmups, a baseline layout, and
      machine-readable quality/runtime/memory metrics; the nightly ID owns
      larger meshes and long traces and never gates PR-fast.
- [ ] Preregister the promotion gate: exact structural/triangle validation is
      mandatory; a GPU-facing follow-up requires at least 15% hierarchy-byte or
      estimated-cache-line reduction and no greater than 5% cut-evaluation
      regression at equal projected-error budgets on at least two nontrivial
      fixtures.

## Tests

- [ ] Unit tests cover deterministic hierarchy construction, acyclicity,
      encode/decode, leaf round-trip, monotone error, invalid bounds, empty
      input, and malformed/cyclic record rejection.
- [ ] A start-gate contract check binds the consumed meshlet-oracle result and
      public API identity to the exact `GEOM-066` report/dependency revision;
      missing, rejected, or operation-ambiguous evidence fails before build
      work begins.
- [ ] Trace tests pin selected cuts and projected-error bounds across all
      camera scenarios and repeat runs.
- [ ] Benchmark smoke uses builtin/checked fixtures only and validates both
      manifest and result JSON; nightly data cannot gate PR-fast.
- [ ] The report compares layouts at equal error budgets and cites named
      baseline/host/toolchain conditions for any runtime statement.

## Docs

- [ ] Add an evidence report under `docs/reports/` with hierarchy invariants,
      trace definitions, memory assumptions, error-per-byte curves, promotion
      verdict, and limitations.
- [ ] Document explicitly that archived `GRAPHICS-044`/`GRAPHICS-056` remain
      planning history and that no GPU/runtime capability landed.
- [ ] Document the stable smoke/nightly benchmark IDs and dataset routing.
- [ ] If the start gate fails, document the exact `GEOM-066` verdict or missing
      public API and state that no hierarchy implementation or benchmark landed.

## Acceptance criteria

- [ ] Exactly one closure branch is recorded: either the start gate fails and
      the task closes with no hierarchy code/benchmark, or the gate passes with
      an operation-specific `GEOM-066` meshlet verdict and public API inventory.
- [ ] On the implementation branch, every hierarchy is deterministic, acyclic,
      decodable, and exactly reconstructs source triangles at its leaves.
- [ ] On the implementation branch, camera-trace cuts satisfy the declared
      monotone projected-error and no-missing/no-duplicate-region invariants.
- [ ] On the implementation branch, machine-readable results compare
      conventional and packed layouts at equal error budgets and apply the
      preregistered promotion gate under both named benchmark IDs.
- [ ] No production asset, graphics, RHI, Vulkan, shader, recipe, or runtime
      surface is added or modified.
- [ ] Default CPU and structural gates pass.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'ClusterHierarchy|Meshoptimizer|BVH' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes

- Any `src/graphics`, `src/runtime`, RHI, Vulkan, shader, recipe, or production
  asset-format change.
- Runtime hierarchy construction, GPU LOD selection, mesh shaders, streaming,
  or Nanite-scope expansion.
- Introducing a second meshoptimizer dependency route or historical
  FetchContent wiring.
- Copying, vendoring, or overlay-installing meshoptimizer's demo-only
  `clusterlod.h`, or treating `clodBuildHierarchy` as a public installed API.
- Opening GPU-facing follow-ups without satisfying the recorded evidence gate.
- Performance claims at unequal projected-error budgets.

## Maturity

- Target: `CPUContracted`; this is an offline evidence endpoint, including a
  valid negative closure at the start gate. No `Operational` follow-up is owed
  unless both the start gate and explicit promotion gate pass and a new task is
  opened.
