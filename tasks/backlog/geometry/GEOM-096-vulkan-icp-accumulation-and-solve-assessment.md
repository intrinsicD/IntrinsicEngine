---
id: GEOM-096
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Operator-requested interactive GPU candidate backlog; implementation uses reviewed diffs, tests and benchmark/ARA evidence for any resulting claims.
contract_schema: 1
contracts: [repo.source-documentation, method.engine-integration, geometry.element-domain-sources, geometry.property-coherence]
---
# GEOM-096 — Vulkan ICP accumulation and solve assessment

## Goal

Move existing ICP correspondence reductions onto Vulkan and measure whether retaining the small transform solve on CPU is preferable.

## Current state and scope

ICP has Vulkan LBVH correspondences but shares a CPU solve. A GPU query backend is not a full GPU iteration.

Operator requested this candidate on 2026-09-17 after a source inspection.
This records authorized backlog planning outside the standing Framework24
selection preference; it is not a claim of GPU speedup or automatic adoption.
Use the existing CPU algorithm as the oracle, review its original formulation
and relevant later work, and complete the reference/tests/benchmark/optimized-CPU
evaluation sequence before a selectable GPU implementation. Keep algorithms,
precision requirements and default backend choices unchanged unless a separate
reviewed decision explicitly adopts a change.

Existing owners and evidence:

- [`src/geometry/Geometry.Registration.cpp`](../../../src/geometry/Geometry.Registration.cpp)
- [`methods/geometry/registration/README.md`](../../../methods/geometry/registration/README.md)
- [`src/runtime/Editor/Operations/Runtime.RegistrationOperations.cpp`](../../../src/runtime/Editor/Operations/Runtime.RegistrationOperations.cpp)

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Two distinct compatible point-property sources, optional required target normals, transforms and existing ICP settings. |
| Compatible entity sources | Every source/target domain accepted by canonical registration preflight, including non-vertex samples. |
| RuntimeModule | Runtime.RegistrationOperations; retain shared target cache and existing publication/config/UI. |
| Config/agent | This task owns backend selection through the existing family preview/validate/apply path; add only genuinely missing typed authoring/config and UI surfaces, with file/agent/UI parity. |
| UI | Same validated operation/readiness and requested/actual/fallback diagnostics; internal-only kernels use the named consumer tasks above. |
| Publication | Existing validated transform result/history, never replacement of source geometry. |
| End-to-end tests | This task owns CPU-oracle comparison, actual GPU readback, every supported input family, failed/stale/cancelled work and applicable config/UI/history coverage. |

## Spatial acceleration consideration

Reuse immutable target leases and existing transformed nearest queries. Preserve source IDs, ties and transform metric; full iteration benchmarking includes correspondence rebuild/reuse and reduction readback.

Consult the [consumer inventory](../../../docs/architecture/spatial-index-consumers.md) and update its owner/reuse notes when implementation changes the path.

## Acceptance criteria

- [ ] Compute point-to-point covariance and point-to-plane normal equations/reductions on GPU without changing correspondences, rejection or robust weighting.
- [ ] Compare a bounded small-system CPU readback with a GPU solve; retain an explicitly hybrid path if it wins or is more reliable.
- [ ] Preserve transform-space metric, finite target-normal requirements, convergence and degeneracy handling; never substitute point-to-point for point-to-plane.
- [ ] Freeze parity tolerances, precision/device capabilities and representative
      fixtures before GPU tuning; do not weaken reference failure semantics.
- [ ] Reuse RHI, `ComputeParallelPrimitives`, framed JobService GPU work and
      `Graphics.GpuTransfer` where their contracts fit. Geometry/physics stay
      RHI-free; no parallel service/queue/registry, device-wide waits or borrowed
      ECS references survive asynchronous work.
- [ ] Prove requested/actual/fallback reporting and source revalidation through
      the declared control/publication path. Preserve canonical typed property
      eligibility, unrelated fields and topology; count changes are explicit.
- [ ] Register focused CPU cases and `GEOM096Vulkan` GPU cases (labels `gpu;vulkan`)
      and prove actual compute results against the independent CPU oracle.
      A fallback, skipped test or seeded reference-shaped GPU buffer is not proof.
- [ ] Reuse/extend manifest-backed benchmark harnesses with stable IDs and v2
      results under `build/ci-vulkan/benchmark-ctest/GEOM-096`. Measure cold/warm
      end-to-end time, transfers, compute, readback and memory against the current
      CPU/hybrid baseline. Benchmark fixtures run under the same `GEOM096Vulkan` selector.
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
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'GEOM096Vulkan' --no-tests=error --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/GEOM-096 --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
