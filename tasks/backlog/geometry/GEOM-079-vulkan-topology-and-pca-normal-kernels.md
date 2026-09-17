---
id: GEOM-079
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
# GEOM-079 — Vulkan topology-normal and local PCA kernels

## Goal

Move mesh/graph topology-normal arithmetic and local point-PCA fitting onto Vulkan while preserving each existing normal method and its input contract.

## Current state and scope

Topology normals run on CPU. PCA already has framed Vulkan neighborhood queries; its covariance/eigensolve and MST orientation remain CPU work. ISS keypoints already have separate GPU PCA kernels and are a reuse candidate, not proof of a complete normal backend.

Operator requested this candidate on 2026-09-17 after a source inspection.
This records authorized backlog planning outside the standing Framework24
selection preference; it is not a claim of GPU speedup or automatic adoption.
Use the existing CPU algorithm as the oracle, review its original formulation
and relevant later work, and complete the reference/tests/benchmark/optimized-CPU
evaluation sequence before a selectable GPU implementation. Keep algorithms,
precision requirements and default backend choices unchanged unless a separate
reviewed decision explicitly adopts a change.

Existing owners and evidence:

- [`src/geometry/Geometry.HalfedgeMesh.Vertices.Normals.cpp`](../../../src/geometry/Geometry.HalfedgeMesh.Vertices.Normals.cpp)
- [`src/geometry/Geometry.Graph.Vertex.Normals.cpp`](../../../src/geometry/Geometry.Graph.Vertex.Normals.cpp)
- [`src/geometry/Geometry.PointCloud.Normals.cpp`](../../../src/geometry/Geometry.PointCloud.Normals.cpp)
- [`src/graphics/renderer/Graphics.PointKeypoints.cpp`](../../../src/graphics/renderer/Graphics.PointKeypoints.cpp)
- [`docs/architecture/normal-estimation.md`](../../../docs/architecture/normal-estimation.md)

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Topology modes need their existing face/edge adjacency and positions; PCA needs finite vec3 positions and the exact resolved neighborhoods. |
| Compatible entity sources | PCA: all compatible canonical point-valued domains; topology modes: only sources supplying the actual adjacency they require. |
| RuntimeModule | Extend Runtime.NormalOperations and its prepared frame; reuse SpatialIndexCache GPU computation for PCA. |
| Config/agent | This task owns backend selection through the existing family preview/validate/apply path; add only genuinely missing typed authoring/config and UI surfaces, with file/agent/UI parity. |
| UI | Same validated operation/readiness and requested/actual/fallback diagnostics; internal-only kernels use the named consumer tasks above. |
| Publication | Named count-matched normals on the selected source domain; preserve unrelated properties and normal orientation policy. |
| End-to-end tests | This task owns CPU-oracle comparison, actual GPU readback, every supported input family, failed/stale/cancelled work and applicable config/UI/history coverage. |

## Spatial acceleration consideration

Reuse canonical SpatialIndexCache leases and framed kNN/radius support. Preserve original source IDs, complete support, deletion mapping, overflow rejection and source-revision invalidation. Topology modes use adjacency, not proximity.

Consult the [consumer inventory](../../../docs/architecture/spatial-index-consumers.md) and update its owner/reuse notes when implementation changes the path.

## Acceptance criteria

- [ ] Compare all topology averaging modes, graph degeneracy and local PCA eigenvectors up to the existing sign/orientation contract; include coincident, planar and ill-conditioned neighborhoods.
- [ ] Reuse the existing GPU covariance/eigensolve only after checking centered covariance, precision and tie semantics against normal estimation. Keep MST orientation explicitly CPU in this slice; report the execution as hybrid when it runs.
- [ ] Retain method-specific complete-radius and k+1/self-exclusion rules; reject incomplete neighborhoods and preserve finite-output diagnostics.
- [ ] Freeze parity tolerances, precision/device capabilities and representative
      fixtures before GPU tuning; do not weaken reference failure semantics.
- [ ] Reuse RHI, `ComputeParallelPrimitives`, framed JobService GPU work and
      `Graphics.GpuTransfer` where their contracts fit. Geometry/physics stay
      RHI-free; no parallel service/queue/registry, device-wide waits or borrowed
      ECS references survive asynchronous work.
- [ ] Prove requested/actual/fallback reporting and source revalidation through
      the declared control/publication path. Preserve canonical typed property
      eligibility, unrelated fields and topology; count changes are explicit.
- [ ] Register focused CPU cases and `GEOM079Vulkan` GPU cases (labels `gpu;vulkan`)
      and prove actual compute results against the independent CPU oracle.
      A fallback, skipped test or seeded reference-shaped GPU buffer is not proof.
- [ ] Reuse/extend manifest-backed benchmark harnesses with stable IDs and v2
      results under `build/ci-vulkan/benchmark-ctest/GEOM-079`. Measure cold/warm
      end-to-end time, transfers, compute, readback and memory against the current
      CPU/hybrid baseline. Benchmark fixtures run under the same `GEOM079Vulkan` selector.
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
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'GEOM079Vulkan' --no-tests=error --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/GEOM-079 --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
