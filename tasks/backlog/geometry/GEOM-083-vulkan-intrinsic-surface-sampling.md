---
id: GEOM-083
theme: I
depends_on: [GEOM-078]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Operator-requested interactive GPU candidate backlog; implementation uses reviewed diffs, tests and benchmark/ARA evidence for any resulting claims.
contract_schema: 1
contracts: [repo.source-documentation, method.engine-integration, geometry.element-domain-sources, geometry.property-coherence]
---
# GEOM-083 — Vulkan intrinsic surface sampling

## Goal

Generate surface samples on Vulkan while preserving the GEOM-078 local-coordinate/Face-ID representation and contiguous per-face ranges.

## Current state and scope

The CPU surface sampler exists. GEOM-078 separately owns the missing intrinsic sample component and face-range representation.

Operator requested this candidate on 2026-09-17 after a source inspection.
This records authorized backlog planning outside the standing Framework24
selection preference; it is not a claim of GPU speedup or automatic adoption.
Use the existing CPU algorithm as the oracle, review its original formulation
and relevant later work, and complete the reference/tests/benchmark/optimized-CPU
evaluation sequence before a selectable GPU implementation. Keep algorithms,
precision requirements and default backend choices unchanged unless a separate
reviewed decision explicitly adopts a change.

Existing owners and evidence:

- [`src/geometry/Geometry.Mesh.SurfaceSampling.cpp`](../../../src/geometry/Geometry.Mesh.SurfaceSampling.cpp)
- [`tests/unit/geometry/Test.SurfaceSampling.cpp`](../../../tests/unit/geometry/Test.SurfaceSampling.cpp)
- [`tasks/backlog/geometry/GEOM-078-intrinsic-surface-sample-point-cloud.md`](../../../tasks/backlog/geometry/GEOM-078-intrinsic-surface-sample-point-cloud.md)

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Triangle positions/connectivity, sample count, seed and existing sampling/normal-interpolation parameters. |
| Compatible entity sources | Triangle mesh sources accepted by the CPU sampler; samples form their own domain, independent of face/vertex cardinality. |
| RuntimeModule | Extend the GEOM-078 runtime attachment/publication owner; this task owns sampling config/agent controls and its editor action. |
| Config/agent | This task owns backend selection through the existing family preview/validate/apply path; add only genuinely missing typed authoring/config and UI surfaces, with file/agent/UI parity. |
| UI | Same validated operation/readiness and requested/actual/fallback diagnostics; internal-only kernels use the named consumer tasks above. |
| Publication | Atomically publish local float2 coordinates, Face-IDs, custom sample rows and contiguous face ranges on the mesh entity. |
| End-to-end tests | This task owns CPU-oracle comparison, actual GPU readback, every supported input family, failed/stale/cancelled work and applicable config/UI/history coverage. |

## Acceptance criteria

- [ ] Preserve area-weighted triangle selection, invalid-face rejection and barycentric interpolation; use shared scan/compaction for counts and ranges.
- [ ] Freeze an explicit seeded CPU/GPU random-stream contract before implementation; identical samples are required where the contract promises identity, not merely a similar histogram.
- [ ] Verify zero/many samples per face, highly unequal areas, regrouped property alignment and mesh replacement during GPU work. This task does not decide geodesic K-Means.
- [ ] Freeze parity tolerances, precision/device capabilities and representative
      fixtures before GPU tuning; do not weaken reference failure semantics.
- [ ] Reuse RHI, `ComputeParallelPrimitives`, framed JobService GPU work and
      `Graphics.GpuTransfer` where their contracts fit. Geometry/physics stay
      RHI-free; no parallel service/queue/registry, device-wide waits or borrowed
      ECS references survive asynchronous work.
- [ ] Prove requested/actual/fallback reporting and source revalidation through
      the declared control/publication path. Preserve canonical typed property
      eligibility, unrelated fields and topology; count changes are explicit.
- [ ] Register focused CPU cases and `GEOM083Vulkan` GPU cases (labels `gpu;vulkan`)
      and prove actual compute results against the independent CPU oracle.
      A fallback, skipped test or seeded reference-shaped GPU buffer is not proof.
- [ ] Reuse/extend manifest-backed benchmark harnesses with stable IDs and v2
      results under `build/ci-vulkan/benchmark-ctest/GEOM-083`. Measure cold/warm
      end-to-end time, transfers, compute, readback and memory against the current
      CPU/hybrid baseline. Benchmark fixtures run under the same `GEOM083Vulkan` selector.
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
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'GEOM083Vulkan' --no-tests=error --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/GEOM-083 --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
