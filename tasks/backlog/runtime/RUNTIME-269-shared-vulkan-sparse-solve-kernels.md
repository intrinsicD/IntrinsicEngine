---
id: RUNTIME-269
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Operator-requested interactive GPU candidate backlog; implementation uses reviewed diffs, tests and benchmark/ARA evidence for any resulting claims.
contract_schema: 1
contracts: [repo.source-documentation, method.engine-integration]
---
# RUNTIME-269 — Shared Vulkan sparse solve kernels for existing methods

## Goal

Provide the smallest reusable GPU sparse-operator/solve implementation required by current heat and parameterization consumers.

## Current state and scope

CPU DEC/Sparse solvers exist. METHOD-026 plans a private GPU solve; GEOM-089 and GEOM-090 add concrete consumers, so they should not grow independent solver frameworks.

Operator requested this candidate on 2026-09-17 after a source inspection.
This records authorized backlog planning outside the standing Framework24
selection preference; it is not a claim of GPU speedup or automatic adoption.
Use the existing CPU algorithm as the oracle, review its original formulation
and relevant later work, and complete the reference/tests/benchmark/optimized-CPU
evaluation sequence before a selectable GPU implementation. Keep algorithms,
precision requirements and default backend choices unchanged unless a separate
reviewed decision explicitly adopts a change.

Existing owners and evidence:

- [`src/geometry/Geometry.HalfedgeMesh.DEC.cpp`](../../../src/geometry/Geometry.HalfedgeMesh.DEC.cpp)
- [`src/geometry/Geometry.Sparse.cpp`](../../../src/geometry/Geometry.Sparse.cpp)
- [`src/geometry/Geometry.LinearSolver.cppm`](../../../src/geometry/Geometry.LinearSolver.cppm)
- [`src/graphics/renderer/Graphics.ComputeParallelPrimitives.cpp`](../../../src/graphics/renderer/Graphics.ComputeParallelPrimitives.cpp)
- [`tasks/backlog/methods/METHOD-026-parameterization-family-gpu-vulkan-compute-backend.md`](../../../tasks/backlog/methods/METHOD-026-parameterization-family-gpu-vulkan-compute-backend.md)

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Explicit CSR/diagonal/vector buffers with matrix-class, precision, constraints and residual contract; no ECS handles. |
| Compatible entity sources | Internal numerical inputs; GEOM-089, GEOM-090 and METHOD-026 own entity eligibility and publication. |
| RuntimeModule | Runtime-owned private compiled GPU implementation through existing RHI and ComputeParallelPrimitives; no geometry-to-RHI dependency or new public solver service. |
| Config/agent | Internal kernel only. METHOD-026, GEOM-089 and GEOM-090 own config/agent/UI controls; this task owns numerical/device diagnostics consumed by them. |
| UI | Same validated operation/readiness and requested/actual/fallback diagnostics; internal-only kernels use the named consumer tasks above. |
| Publication | Typed solution/status/residual to the owning operation; no independent ECS or UI publication. |
| End-to-end tests | This task owns CPU-oracle comparison, actual GPU readback, every supported input family, failed/stale/cancelled work and applicable config/UI/history coverage. |

## Acceptance criteria

- [ ] Audit actual consumer systems first. Implement bounded SpMV, vector reductions and Jacobi-preconditioned CG for proven SPD systems; do not mislabel indefinite, rectangular or complex systems as supported.
- [ ] Freeze precision/capability checks, nullspace/Dirichlet treatment, residual norms and non-convergence/breakdown results. Optional float64 must be capability-gated.
- [ ] Keep iterative work on-device and reuse buffers/operators where valid. Expose only plain descriptors/free functions justified by the named consumers; avoid a backend registry.
- [ ] Freeze parity tolerances, precision/device capabilities and representative
      fixtures before GPU tuning; do not weaken reference failure semantics.
- [ ] Reuse RHI, `ComputeParallelPrimitives`, framed JobService GPU work and
      `Graphics.GpuTransfer` where their contracts fit. Geometry/physics stay
      RHI-free; no parallel service/queue/registry, device-wide waits or borrowed
      ECS references survive asynchronous work.
- [ ] Prove requested/actual/fallback reporting and source revalidation through
      the declared control/publication path. Preserve canonical typed property
      eligibility, unrelated fields and topology; count changes are explicit.
- [ ] Register focused CPU cases and `RUNTIME269Vulkan` GPU cases (labels `gpu;vulkan`)
      and prove actual compute results against the independent CPU oracle.
      A fallback, skipped test or seeded reference-shaped GPU buffer is not proof.
- [ ] Reuse/extend manifest-backed benchmark harnesses with stable IDs and v2
      results under `build/ci-vulkan/benchmark-ctest/RUNTIME-269`. Measure cold/warm
      end-to-end time, transfers, compute, readback and memory against the current
      CPU/hybrid baseline. Benchmark fixtures run under the same `RUNTIME269Vulkan` selector.
- [ ] Update affected method manifests/backend documentation, canonical
      architecture notes and module inventory when interfaces change. Bind any
      capability/parity/performance conclusion to ARA evidence; no present
      performance claim follows from filing this task.

## Completion boundary

Target is actual Vulkan execution of the named stages with CPU-reference parity and complete declared integration. Any retained CPU stage must be named in results; shader presence or a GPU query alone does not close the task.

## Verification

Register the named suites and benchmark output as part of implementation.
For assessment-only rejection, run the CPU/candidate evidence actually needed
and record the negative gate; never fabricate successful production GPU evidence.
Required sanitizer/merge gates remain additional to this focused checklist.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'RUNTIME269Vulkan' --no-tests=error --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/RUNTIME-269 --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
