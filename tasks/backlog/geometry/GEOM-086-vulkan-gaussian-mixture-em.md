---
id: GEOM-086
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
# GEOM-086 — Vulkan Gaussian-mixture EM fitting

## Goal

Implement the existing dense Gaussian-mixture EM iterations on Vulkan without changing the fitted objective.

## Current state and scope

Geometry.GaussianMixture provides CPU EM with covariance flooring, log-likelihood diagnostics and optional safeguarded Anderson acceleration.

Operator requested this candidate on 2026-09-17 after a source inspection.
This records authorized backlog planning outside the standing Framework24
selection preference; it is not a claim of GPU speedup or automatic adoption.
Use the existing CPU algorithm as the oracle, review its original formulation
and relevant later work, and complete the reference/tests/benchmark/optimized-CPU
evaluation sequence before a selectable GPU implementation. Keep algorithms,
precision requirements and default backend choices unchanged unless a separate
reviewed decision explicitly adopts a change.

Existing owners and evidence:

- [`src/geometry/Geometry.GaussianMixture.cpp`](../../../src/geometry/Geometry.GaussianMixture.cpp)
- [`src/geometry/Geometry.FixedPoint.Anderson.cpp`](../../../src/geometry/Geometry.FixedPoint.Anderson.cpp)
- [`methods/geometry/curvature_segmentation/method.yaml`](../../../methods/geometry/curvature_segmentation/method.yaml)

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Finite vec3 sample span, initial mixture and current EM/acceleration parameters. |
| Compatible entity sources | Every canonical domain supplying the typed sample property; preserve each consumer's preprocessing and statistical interpretation. |
| RuntimeModule | Private runtime compute integration reused by GEOM-091 segmentation and GEOM-097 CLOP; these consumer tasks own their config/UI exposure. |
| Config/agent | Internal typed backend consumed by GEOM-091 and GEOM-097; those tasks own validated config/agent/UI routing. No standalone fitting panel is required. |
| UI | Same validated operation/readiness and requested/actual/fallback diagnostics; internal-only kernels use the named consumer tasks above. |
| Publication | Mixture and diagnostics to the typed caller; consumer-owned named properties only, with no fabricated geometry conversion. |
| End-to-end tests | This task owns CPU-oracle comparison, actual GPU readback, every supported input family, failed/stale/cancelled work and applicable config/UI/history coverage. |

## Spatial acceleration consideration

Keep dense Gaussian evaluation exact. Neither a radius cutoff nor nearest-component LBVH preserves this objective without a separately reviewed approximation.

Consult the [consumer inventory](../../../docs/architecture/spatial-index-consumers.md) and update its owner/reuse notes when implementation changes the path.

## Acceptance criteria

- [ ] Parallelize dense sample/component responsibilities and deterministic reductions for weights, means and covariances; do not truncate Gaussian tails.
- [ ] Preserve initialization, log-sum-exp stability, covariance floors, empty-component behavior, stopping and Anderson safeguards.
- [ ] Compare likelihood histories, mixture predictions and collapsed/ill-conditioned cases with declared label-permutation handling and precision budgets.
- [ ] Freeze parity tolerances, precision/device capabilities and representative
      fixtures before GPU tuning; do not weaken reference failure semantics.
- [ ] Reuse RHI, `ComputeParallelPrimitives`, framed JobService GPU work and
      `Graphics.GpuTransfer` where their contracts fit. Geometry/physics stay
      RHI-free; no parallel service/queue/registry, device-wide waits or borrowed
      ECS references survive asynchronous work.
- [ ] Prove requested/actual/fallback reporting and source revalidation through
      the declared control/publication path. Preserve canonical typed property
      eligibility, unrelated fields and topology; count changes are explicit.
- [ ] Register focused CPU cases and `GEOM086Vulkan` GPU cases (labels `gpu;vulkan`)
      and prove actual compute results against the independent CPU oracle.
      A fallback, skipped test or seeded reference-shaped GPU buffer is not proof.
- [ ] Reuse/extend manifest-backed benchmark harnesses with stable IDs and v2
      results under `build/ci-vulkan/benchmark-ctest/GEOM-086`. Measure cold/warm
      end-to-end time, transfers, compute, readback and memory against the current
      CPU/hybrid baseline. Benchmark fixtures run under the same `GEOM086Vulkan` selector.
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
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'GEOM086Vulkan' --no-tests=error --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/GEOM-086 --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
