---
id: GEOM-090
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
# GEOM-090 — Assess Vulkan LSCM, harmonic and BFF parameterization

## Goal

Measure whether Vulkan solves benefit existing LSCM, harmonic and BFF workflows before adopting a selectable backend.

## Current state and scope

All three exist on CPU. METHOD-026 intentionally owns only iterative ARAP/SLIM GPU work; no existing task covers this one-shot assessment.

Operator requested this candidate on 2026-09-17 after a source inspection.
This records authorized backlog planning outside the standing Framework24
selection preference; it is not a claim of GPU speedup or automatic adoption.
Use the existing CPU algorithm as the oracle, review its original formulation
and relevant later work, and complete the reference/tests/benchmark/optimized-CPU
evaluation sequence before a selectable GPU implementation. Keep algorithms,
precision requirements and default backend choices unchanged unless a separate
reviewed decision explicitly adopts a change.

Existing owners and evidence:

- [`src/geometry/Geometry.HalfedgeMesh.Parameterization.cpp`](../../../src/geometry/Geometry.HalfedgeMesh.Parameterization.cpp)
- [`src/geometry/Geometry.Parameterization.Harmonic.cpp`](../../../src/geometry/Geometry.Parameterization.Harmonic.cpp)
- [`src/geometry/Geometry.Parameterization.Bff.cpp`](../../../src/geometry/Geometry.Parameterization.Bff.cpp)
- [`tasks/backlog/methods/METHOD-026-parameterization-family-gpu-vulkan-compute-backend.md`](../../../tasks/backlog/methods/METHOD-026-parameterization-family-gpu-vulkan-compute-backend.md)
- [`tasks/backlog/geometry/GEOM-070-sparse-lsqr-lscm-adoption.md`](../../../tasks/backlog/geometry/GEOM-070-sparse-lsqr-lscm-adoption.md)

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Existing triangle disk/topology, positions, pins and boundary prescriptions for each strategy. |
| Compatible entity sources | Exactly the canonical mesh sources accepted by each CPU strategy; no GPU-only eligibility narrowing. |
| RuntimeModule | Runtime.ParameterizationOperations and its prepared frame; preserve METHOD-026 strategy ownership. |
| Config/agent | This task owns backend selection through the existing family preview/validate/apply path; add only genuinely missing typed authoring/config and UI surfaces, with file/agent/UI parity. |
| UI | Same validated operation/readiness and requested/actual/fallback diagnostics; internal-only kernels use the named consumer tasks above. |
| Publication | Candidate UVs remain experimental until accepted; adopted results use existing validated UV/history publication. |
| End-to-end tests | This task owns CPU-oracle comparison, actual GPU readback, every supported input family, failed/stale/cancelled work and applicable config/UI/history coverage. |

## Acceptance criteria

- [ ] Predeclare adoption thresholds and compare cold end-to-end cost, repeated right-hand sides and cached CPU factorization reuse on representative sizes.
- [ ] Preserve each system's mathematical class and formulation; GEOM-070 owns any CPU LSCM-to-LSQR change, not this GPU study.
- [ ] Compare distortion, boundary constraints, residuals, failure classification and UV finiteness. Record a negative verdict if transfer/solve costs or numerical changes defeat the candidate; no backend token then.
- [ ] Freeze parity tolerances, precision/device capabilities and representative
      fixtures before GPU tuning; do not weaken reference failure semantics.
- [ ] Reuse RHI, `ComputeParallelPrimitives`, framed JobService GPU work and
      `Graphics.GpuTransfer` where their contracts fit. Geometry/physics stay
      RHI-free; no parallel service/queue/registry, device-wide waits or borrowed
      ECS references survive asynchronous work.
- [ ] Prove requested/actual/fallback reporting and source revalidation through
      the declared control/publication path. Preserve canonical typed property
      eligibility, unrelated fields and topology; count changes are explicit.
- [ ] Register focused CPU cases and `GEOM090Vulkan` GPU cases (labels `gpu;vulkan`)
      and prove actual compute results against the independent CPU oracle.
      A fallback, skipped test or seeded reference-shaped GPU buffer is not proof.
- [ ] Reuse/extend manifest-backed benchmark harnesses with stable IDs and v2
      results under `build/ci-vulkan/benchmark-ctest/GEOM-090`. Measure cold/warm
      end-to-end time, transfers, compute, readback and memory against the current
      CPU/hybrid baseline. Benchmark fixtures run under the same `GEOM090Vulkan` selector.
- [ ] Update affected method manifests/backend documentation, canonical
      architecture notes and module inventory when interfaces change. Bind any
      capability/parity/performance conclusion to ARA evidence; no present
      performance claim follows from filing this task.

## Completion boundary

This is an evidence-gated assessment: an accepted negative result closes the task with no exposed backend. Positive adoption additionally owes the integration and device tests below.

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
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'GEOM090Vulkan' --no-tests=error --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/GEOM-090 --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
