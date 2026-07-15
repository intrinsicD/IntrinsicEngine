---
id: METHOD-027
theme: I
depends_on: [REVIEW-003, GEOM-013, METHOD-007]
---
# METHOD-027 — Adaptive Delaunay/QEF implicit meshing reference

## Goal

- Determine whether feature samples produced by QEF minimization can drive a
  deterministic adaptive Delaunay implicit-meshing reference that preserves
  thin and sharp features while producing manifold output with fewer field
  evaluations than fixed-grid extraction.

## Non-goals

- No replacement or default-policy change for `GEOM-013` Manifold Dual
  Contouring or `Geometry.MarchingCubes`; this method is opt-in.
- No 3D implementation before the 2D adversarial killing slice passes its
  declared correctness and termination gates.
- No GPU backend, learned sampler, general remeshing framework, or new runtime,
  renderer, ECS, asset, or platform integration.
- No claim of algorithmic novelty or superiority without the comparative
  evidence required below.

## Context

- Owning subsystem/layer: `geometry` plus a method package under
  `methods/geometry/adaptive_delaunay_qef/`; all engine code remains
  `geometry -> core`.
- Promotion is gated by `REVIEW-003`, the post-stabilization review gate. The
  task must not start merely because its geometry prerequisites are available.
- `GEOM-013` supplies the canonical QEF/manifold-dual-contouring reference;
  `METHOD-007` supplies the constrained-Delaunay/TetMesh reference. Reuse
  concrete kernels only where these two present callers justify extraction;
  do not introduce a meshing service, backend registry, or general strategy
  framework.
- The seed is Matt Keeter's staged proposal (grid signs -> QEF feature points
  -> incremental Delaunay refinement of sign-crossing edges -> separating
  surface): [Please Steal my Meshing Algorithm Idea](https://www.mattkeeter.com/blog/2026-07-03-meshing/).
- The prior-art audit must include Adaptive Delaunay Scaffolding
  ([arXiv:2605.03235](https://arxiv.org/abs/2605.03235)), Manifold Dual
  Contouring, and restricted-Delaunay surface sampling. This task evaluates a
  recombination; it does not presume novelty.
- Retired `GEOM-007` supplies robust orientation/insphere predicates,
  `GEOM-008` supplies small dense/SVD utilities, `GEOM-009` supplies the
  manifest-driven geometry benchmark pattern, `GEOM-039` supplies exact
  nearest-face queries, and `GEOM-046` supplies topology analysis.

## Slice plan

- **Slice A — paper intake and method contract.** Before writing prototype
  code, create the method package and pin the 2D/3D objectives, assumptions,
  normalized units, inputs/outputs, diagnostics, failure states, fixture split,
  budgets, tolerances, and prior-art boundary.
- **Slice B — 2D killing test.** Implement the algorithm privately over
  analytic implicit curves, compare it with uniform-grid contouring, and
  measure termination, topology, geometric error, feature recall, and field
  evaluations. This slice may retire the task as a documented negative result.
- **Slice C — 3D CPU reference.** Reuse the landed QEF and Delaunay primitives,
  only after Slice B passes, add analytic/regression tests, and emit a manifold
  `HalfedgeMesh::Mesh` with explicit non-convergence and unsupported-field
  diagnostics.
- **Slice D — comparative evidence.** Add the smoke manifest and a bounded
  comparison against Marching Cubes and Manifold DC. Heavy adversarial-corpus
  work remains a separate benchmark task.

## Right-sizing

- Start with file-local 2D records and free functions. Extract a shared QEF or
  Delaunay primitive only after both existing prerequisite implementations and
  this method require the exact same contract.
- A failed Slice B ends the task with evidence; it does not leave scaffold code
  or create a replacement follow-up.

## Backends

- Backend axis: deterministic `cpu_reference` only. No optimized CPU or GPU
  backend is owed unless the reference survives the evidence gate and a later
  task names a concrete consumer.

## Required changes

- [ ] Create `methods/geometry/adaptive_delaunay_qef/` with `method.yaml`,
      `paper.md`, and a CPU-reference README before prototype implementation.
      Record the objective, normalized-domain/field units, 2D and 3D
      input/output contracts, assumptions, prior-art boundary, fixture split,
      stopping rules, diagnostics, and failure states.
- [ ] Create a checked-in procedural 2D adversarial fixture set covering a
      smooth curve, sharp corner, thin double wall, near-tangent components,
      disconnected components, and a continuous non-Lipschitz field.
- [ ] Implement the private 2D QEF/Delaunay refinement prototype with explicit
      iteration, sample-count, and topology-growth caps and deterministic
      tie-breaking.
- [ ] Freeze the Slice B budget before execution: scale every fixture so its
      domain diagonal is 1, allow at most 16 refinement rounds, 65,536 field
      evaluations, and 131,072 live vertices/triangles per fixture, and give
      uniform-grid contouring the same field-query budget within 1%.
- [ ] Apply the numeric Slice B killing rule to the held-out fixture set: every
      fixture terminates within all caps, has the expected component/manifold
      structure, and contains no self-intersection or non-finite vertex; across
      the frozen fixture set the method must either improve thin-feature recall
      by at least 10 percentage points or reduce worst-case symmetric Hausdorff
      error by at least 20%, while recall may regress by at most 2 percentage
      points and Hausdorff error by at most 5% relative when it is the guardrail.
- [ ] Also require the adaptive method to reach the uniform-grid baseline's
      paired recall/error target with at most 80% of its median field queries
      across the held-out fixtures and no fixture above 105%; otherwise the
      task may report quality evidence but cannot pass its evaluation-efficiency
      claim.
- [ ] If Slice B fails, record the fixture, parameters, diagnostics, and failure
      mode in the task/method evidence, delete non-reusable prototype surface,
      and retire with an explicit no-follow-up decision.
- [ ] If Slice B passes, implement the smallest geometry-owned 3D CPU reference
      using plain data records/free functions; return structured diagnostics
      for capped refinement, degenerate QEFs, duplicate samples, invalid
      Delaunay state, non-manifold output, and non-finite input.
- [ ] Add stable benchmark ID
      `geometry.adaptive_delaunay_qef.reference.smoke`. Restrict manifest
      metrics to the repository allow-list; emit component/topology counts,
      Hausdorff/error summaries, feature recall, field evaluations, and
      termination diagnostics in the result payload.

## Tests

- [ ] Add deterministic unit tests for the 2D killing fixtures, including
      repeated-run identity, the exact numeric pass/kill rule, and cap
      exhaustion that fails closed.
- [ ] After Slice B passes, add analytic 3D tests for sphere, box/sharp corner,
      thin shell, disconnected components, and a twisted field that challenges
      manifold extraction.
- [ ] Compare 3D output against the CPU references from Marching Cubes and
      `GEOM-013` at matched field-query budgets; include correctness/quality
      metrics, not runtime alone.
- [ ] Validate empty bounds, zero/overflowing resolution, constant-sign fields,
      zero/non-finite gradients, duplicate crossings, and non-convergence.
- [ ] Run the benchmark smoke and validate its manifest and result JSON.

## Docs

- [ ] Document the method contract, prior-art boundary, killing-test outcome,
      numerical limitations, and current maturity in the method package.
- [ ] If the 3D reference lands, update `docs/architecture/geometry.md` with
      the opt-in relationship among Marching Cubes, Manifold DC, and this
      method; do not describe an unimplemented default change.
- [ ] Update `benchmarks/geometry/README.md` for the stable benchmark ID and
      regenerate the module inventory if a public module surface changes.

## Acceptance criteria

- [ ] Slice A completes the paper intake and freezes the method contract before
      any prototype is implemented.
- [ ] Slice B has a reproducible pass or kill result against the fixed 16-round,
      65,536-query, and 131,072-element caps and the declared 10-point/20%
      improvement, 2-point/5% non-regression, and 80%-median/105%-worst-query
      thresholds.
- [ ] A killed method leaves durable negative evidence, no production
      scaffold, and no implementation follow-up.
- [ ] A surviving method has a deterministic 3D CPU reference, explicit
      termination/failure diagnostics, manifold/topology tests, and a validated
      comparison benchmark.
- [ ] `GEOM-013` remains the public default and no new cross-layer dependency
      or speculative abstraction is introduced.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'AdaptiveDelaunayQef|DualContouring|ConstrainedDelaunay|BenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- Starting the 2D prototype before the paper intake, method contract, fixture
  split, budgets, and tolerances are checked in.
- Starting the 3D slice before the 2D killing criteria pass.
- Replacing or silently changing the default behavior of `GEOM-013` or
  `Geometry.MarchingCubes`.
- Creating a generic meshing interface/service/registry for one new method.
- Shipping prototype-only or failed-method scaffolding in promoted modules.
- Adding optimized/GPU, runtime, renderer, ECS, asset, platform, or UI work.
- Mixing mechanical file moves with semantic changes.

## Maturity

- Target after a positive killing test: `CPUContracted`.
- The CPU reference/evidence endpoint is intentional; no `Operational`
  follow-up is owed.
