---
id: METHOD-005
theme: I
depends_on: [GEOM-007]
maturity_target: CPUContracted
---
# METHOD-005 — Robust mesh boolean reference backend

## Goal
- Add an opt-in, numerically robust CPU-reference mesh-boolean kernel for
  union, intersection, difference, and symmetric difference, with explicit
  degeneracy/provenance diagnostics and comparison evidence against the
  existing kernel. Default replacement and legacy retirement are not implied.

## Non-goals
- No GPU backend.
- No in-place rewrite of `Geometry.HalfedgeMesh.Boolean` during this task; both kernels must coexist until parity tests pass.
- No general arrangement / BSP toolkit beyond what the chosen variant requires.
- No CSG scripting layer / DSL.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` and `methods/geometry`.
- Method package: `methods/geometry/robust_boolean/`.
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 2 #5.
- **Hard prerequisite:** retired
  [`GEOM-007`](../../archive/GEOM-007-robust-predicates-intersection-classification.md)
  supplies the filtered-sign/uncertainty vocabulary and intersection result
  records. It did **not** land exact/adaptive escalation or a complete mesh
  arrangement kernel. This task owns only the selected Boolean method's
  formula-specific exact/indirect construction predicates when GEOM-007
  reports an uncertain sign; it must not misrepresent the prerequisite as an
  exact kernel.
- The survey identified near-coincident and degenerate inputs as the existing
  Boolean kernel's robustness risk. This task must reproduce, minimize, and
  freeze at least one actual legacy failure before using that risk as a
  replacement or capability claim.

## Variants and default selection

- **Selected — Interactive and Robust Mesh Booleans** (Cherchi, Pellacini,
  Attene, Livesu; ACM TOG 41(6), 2022, arXiv:2205.14151). This is the only
  production algorithm in this task.
- **Prior-art alternatives — EMBER and the 2016 generalized-winding
  formulation.** Capture them in `paper.md` as comparison/threats; do not add
  dormant strategy tokens or dependencies.

## Slice plan

- **Slice A — intake and fixture contract.** Freeze supported manifold/input
  assumptions, exact/filtered predicate boundaries, attribute/provenance
  semantics, topology/volume metrics, fixture split, tolerances, and explicit
  failure states.
- **Slice B — analytic reference.** Implement the smallest selected-algorithm
  path for closed triangulated inputs and analytic cube/degeneracy cases.
- **Slice C — provenance and bounded regression set.** Add attribute transfer
  and the checked-in adversarial fixture subset only after topology/volume
  truth is established.
- **Slice D — comparison evidence.** Add the deterministic smoke and compare
  with the existing kernel without changing the public default.

## Right-sizing

- Keep one robust-boolean module beside the existing implementation. Do not
  introduce a boolean backend registry, general arrangement toolkit, or CSG
  service.
- A future default switch/legacy retirement requires its own parity-gated task
  with broader evidence; this task cannot make that policy change implicitly.

## Backends

- Backend axis: `cpu_reference` for the selected robust method. The existing
  Boolean kernel is a comparison/legacy implementation, not an alternative
  backend token; optimized CPU or GPU work requires a later parity-gated task.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/robust_boolean/`.
- [ ] Fill `method.yaml` (`id: geometry.robust_boolean`; metrics:
      `fixture_pass_count`, `fixture_case_count`, `volume_error`,
      `topology_error_count`, `output_triangle_count`, `runtime_ms`). Report
      tiny checked-in fixture outcomes per case/count, not as a percentage or
      broad Thingi10k success metric.
- [ ] Fill `paper.md` with the selected formulation, assumptions, objective,
      topology/attribute contract, scale-relative units, exact/filtered
      predicate policy, and failure taxonomy.

### Public API in `src/geometry`
- [ ] Add module `Geometry.RobustBoolean` in
      `src/geometry/Geometry.RobustBoolean.cppm` + `.cpp`, with matching
      `Geometry::RobustBoolean` namespace per the new-API naming policy.
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry::RobustBoolean {
    enum class Op { Union, Intersection, Difference, SymmetricDifference };
    struct Input {
      const HalfedgeMesh::Mesh& a;
      const HalfedgeMesh::Mesh& b;
      Op op;
    };
    struct Params {
      bool transfer_attributes = true;
      std::uint64_t max_intersection_events = 1'000'000;
      std::uint64_t max_output_faces = 4'000'000;
    };
    struct Result {
      HalfedgeMesh::Mesh out;
      ArrangementProvenance provenance; // method-local source/barycentric map
      Diagnostics diagnostics;          // escalation, degeneracy, caps, etc.
    };
    Core::Expected<Result> Compute(const Input&, const Params&);
  }
  ```
- [ ] Do **not** delete or rename `Geometry.HalfedgeMesh.Boolean` in this task; mark the legacy module as `[legacy]` in its module-level doc comment.

### Implementation steps (selected reference path)
- [ ] Step 1: build per-mesh BVH using existing `Geometry.BVH`.
- [ ] Step 2: detect candidate intersecting triangle pairs via BVH-vs-BVH overlap.
- [ ] Step 3: compute arrangement intersections with GEOM-007's filtered
      signs/diagnostic records as the fast stage and the selected method's
      formula-specific exact/indirect predicates as the mandatory escalation
      for uncertain construction signs; never convert uncertainty into an
      epsilon guess.
- [ ] Step 4: local arrangement re-triangulation of each affected triangle.
- [ ] Step 5: classify arrangement patches with the selected paper's
      classification rule frozen during intake; do not substitute an
      epsilon-offset point-sampling heuristic.
- [ ] Step 6: select sub-arrangement per boolean op; stitch into output mesh.
- [ ] Step 7: provenance — record source mesh + source triangle id per output triangle for attribute transfer.

### Attribute transfer
- [ ] Transfer per-vertex and per-face properties via `Geometry.Properties` interpolation rules (barycentric for vertex, copy-from-source for face) using the provenance record.

### Benchmark
- [ ] Add an executable deterministic smoke with stable ID
      `geometry.robust_boolean.reference.smoke`, a built-in analytic/adversarial
      dataset, `intent: correctness`, explicit warmup/measured counts, and
      allowed metrics `runtime_ms`, `quality_error_l2`, and
      `quality_error_linf`. Encode topology/volume regressions in the quality
      metrics and put per-operation counts/degeneracy diagnostics in result
      JSON.
- [ ] Keep the external/broad Thingi10k study out of PR-fast. The smoke uses
      only the small checked-in fixtures and makes no throughput claim.

## Tests
- [ ] `tests/unit/geometry/Test.RobustBoolean.cpp`.
- [ ] Smoke: two unit cubes (axis-aligned) — verify analytic volumes for union / intersection / difference.
- [ ] Coincident faces: two cubes sharing a face — the robust kernel returns
      the frozen analytic result; record the legacy result without presuming
      that this particular case is its motivating failure.
- [ ] Near-degenerate intersection: thin triangle pair from `tests/data/robust_boolean/` fixture set.
- [ ] Regression: at most ten minimized Thingi10k-derived pairs under
      `tests/data/robust_boolean/`, each with source URL, model/license
      provenance, transformation/minimization notes, and explicit expected
      status; report per-case outcomes in
      `methods/geometry/robust_boolean/reports/`.
- [ ] Parity comparison test that runs both kernels on simple inputs and asserts agreement up to combinatorial reordering.
- [ ] Freeze at least one minimized input on which the current legacy kernel
      demonstrably returns the wrong topology/volume or fails, then require the
      selected robust kernel to return the analytic expected result.
- [ ] Freeze scale-relative geometric, volume, and topology tolerances before
      implementing assertions; fail closed on open/non-manifold/non-finite
      inputs and unsupported attribute interpolation.

## Docs
- [ ] `methods/geometry/robust_boolean/README.md` — backend identity, selected
      formulation, exact/indirect escalation policy, and known limitations.
- [ ] Update `methods/geometry/README.md` index.
- [ ] Add a note in `docs/architecture/geometry.md` explaining the dual-kernel coexistence and the planned legacy retirement task.
- [ ] Register/document the stable correctness-smoke ID and distinguish it
      from any later heavy Thingi10k corpus.
- [ ] Regenerate module inventory.

## Acceptance criteria
- [ ] The Cherchi et al. formulation is the sole public robust-Boolean
      strategy; prior-art alternatives remain documentation-only.
- [ ] Robust kernel passes all unit cases the legacy kernel also passes (no regression).
- [ ] Robust kernel passes at least one case the legacy kernel fails on (motivation justified).
- [ ] Every declared checked-in regression fixture has an explicit expected
      success or expected failure status; the smoke result reports all cases
      without converting a tiny fixture count into a percentage claim.
- [ ] Smoke manifest/result validate and report topology/volume quality, not
      runtime alone.
- [ ] `GEOM-007` is closed (or the prerequisite predicate API is already merged) before this task starts.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'RobustBoolean|Boolean|RobustPredicates|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
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
- No deletion or rename of `Geometry.HalfedgeMesh.Boolean` in this task (separate retirement task once parity proven).
- No CGAL / libigl dependency in the production code path. Reference implementations may be used as **out-of-build** comparison tools in `methods/.../reports/` only.
- No reliance on `assert` for degeneracy handling — diagnostics must surface degeneracy as data.
- No GPU backend before reference parity.

## Maturity
- Target: `CPUContracted`. The CPU reference backend is the correctness oracle for any later optimized/GPU backend; legacy `Geometry.HalfedgeMesh.Boolean` retirement is a separate parity-gated follow-up.
- No `Operational` follow-up is owed by this task; optimized CPU and GPU backends open as separate method tasks per `AGENTS.md` §6 once reference parity exists.
