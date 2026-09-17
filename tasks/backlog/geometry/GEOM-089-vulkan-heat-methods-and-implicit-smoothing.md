---
id: GEOM-089
theme: I
depends_on: [RUNTIME-269]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Operator-requested interactive GPU candidate backlog; implementation uses reviewed diffs, tests and benchmark/ARA evidence for any resulting claims.
contract_schema: 1
contracts: [repo.source-documentation, method.engine-integration, geometry.element-domain-sources, geometry.property-coherence]
---
# GEOM-089 — Vulkan heat methods and implicit smoothing

## Goal

Bind shared Vulkan sparse operations to existing Heat, Signed Heat, Vector Heat and implicit Laplacian smoothing in independent slices.

## Current state and scope

These CPU methods assemble DEC/sparse operators and solve coupled systems. Sparse kernels are parallel work; solver convergence and factorization reuse remain separate correctness/performance concerns.

Operator requested this candidate on 2026-09-17 after a source inspection.
This records authorized backlog planning outside the standing Framework24
selection preference; it is not a claim of GPU speedup or automatic adoption.
Use the existing CPU algorithm as the oracle, review its original formulation
and relevant later work, and complete the reference/tests/benchmark/optimized-CPU
evaluation sequence before a selectable GPU implementation. Keep algorithms,
precision requirements and default backend choices unchanged unless a separate
reviewed decision explicitly adopts a change.

Existing owners and evidence:

- [`src/geometry/Geometry.HalfedgeMesh.Geodesic.cpp`](../../../src/geometry/Geometry.HalfedgeMesh.Geodesic.cpp)
- [`src/geometry/Geometry.HalfedgeMesh.SignedHeatMethod.cpp`](../../../src/geometry/Geometry.HalfedgeMesh.SignedHeatMethod.cpp)
- [`src/geometry/Geometry.HalfedgeMesh.VectorHeatMethod.cpp`](../../../src/geometry/Geometry.HalfedgeMesh.VectorHeatMethod.cpp)
- [`src/geometry/Geometry.HalfedgeMesh.Smoothing.cpp`](../../../src/geometry/Geometry.HalfedgeMesh.Smoothing.cpp)

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Canonical mesh positions/connectivity, source points or oriented curves, tangent data and boundary constraints required by the selected method. |
| Compatible entity sources | Existing compatible triangle-mesh sources; preserve connectivity, component and oriented-source semantics. |
| RuntimeModule | Existing geodesic/mesh-field and smoothing operations; Signed Heat binds through RUNTIME-210/UI-042 when those CPU surfaces are available. |
| Config/agent | This task owns backend selection through the existing family preview/validate/apply path; add only genuinely missing typed authoring/config and UI surfaces, with file/agent/UI parity. |
| UI | Same validated operation/readiness and requested/actual/fallback diagnostics; internal-only kernels use the named consumer tasks above. |
| Publication | Existing distance/vector fields or topology-preserving position updates with source guards and history. |
| End-to-end tests | This task owns CPU-oracle comparison, actual GPU readback, every supported input family, failed/stale/cancelled work and applicable config/UI/history coverage. |

## Acceptance criteria

- [ ] Freeze matrix signs, mass/connection operators, nullspace gauges and constraints; distinguish real and complex systems instead of forcing every system through an SPD token.
- [ ] Compare PDE residuals and geometric output on disconnected, boundary, degenerate and ill-conditioned fixtures; pin required precision before backend admission.
- [ ] Reuse RUNTIME-269 for supported systems; explicit unsupported diagnostics remain until the corresponding operator is implemented. Signed Heat UI completion waits for RUNTIME-210/UI-042; this does not block other slices.
- [ ] Freeze parity tolerances, precision/device capabilities and representative
      fixtures before GPU tuning; do not weaken reference failure semantics.
- [ ] Reuse RHI, `ComputeParallelPrimitives`, framed JobService GPU work and
      `Graphics.GpuTransfer` where their contracts fit. Geometry/physics stay
      RHI-free; no parallel service/queue/registry, device-wide waits or borrowed
      ECS references survive asynchronous work.
- [ ] Prove requested/actual/fallback reporting and source revalidation through
      the declared control/publication path. Preserve canonical typed property
      eligibility, unrelated fields and topology; count changes are explicit.
- [ ] Register focused CPU cases and `GEOM089Vulkan` GPU cases (labels `gpu;vulkan`)
      and prove actual compute results against the independent CPU oracle.
      A fallback, skipped test or seeded reference-shaped GPU buffer is not proof.
- [ ] Reuse/extend manifest-backed benchmark harnesses with stable IDs and v2
      results under `build/ci-vulkan/benchmark-ctest/GEOM-089`. Measure cold/warm
      end-to-end time, transfers, compute, readback and memory against the current
      CPU/hybrid baseline. Benchmark fixtures run under the same `GEOM089Vulkan` selector.
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
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'GEOM089Vulkan' --no-tests=error --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/GEOM-089 --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
