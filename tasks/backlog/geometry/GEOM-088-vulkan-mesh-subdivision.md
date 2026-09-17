---
id: GEOM-088
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
# GEOM-088 — Vulkan Loop, sqrt3 and Catmull-Clark subdivision

## Goal

Add Vulkan subdivision stages for the three existing schemes, including deterministic topology and property construction.

## Current state and scope

CPU Loop, sqrt3 and Catmull-Clark implementations exist; GEOM-072 separately owns future Catmull-Clark crease masks.

Operator requested this candidate on 2026-09-17 after a source inspection.
This records authorized backlog planning outside the standing Framework24
selection preference; it is not a claim of GPU speedup or automatic adoption.
Use the existing CPU algorithm as the oracle, review its original formulation
and relevant later work, and complete the reference/tests/benchmark/optimized-CPU
evaluation sequence before a selectable GPU implementation. Keep algorithms,
precision requirements and default backend choices unchanged unless a separate
reviewed decision explicitly adopts a change.

Existing owners and evidence:

- [`src/geometry/Geometry.HalfedgeMesh.Subdivision.cpp`](../../../src/geometry/Geometry.HalfedgeMesh.Subdivision.cpp)
- [`src/geometry/Geometry.HalfedgeMesh.SubdivisionSqrt3.cpp`](../../../src/geometry/Geometry.HalfedgeMesh.SubdivisionSqrt3.cpp)
- [`src/geometry/Geometry.HalfedgeMesh.CatmullClark.cpp`](../../../src/geometry/Geometry.HalfedgeMesh.CatmullClark.cpp)
- [`tasks/backlog/geometry/GEOM-072-catmull-clark-crease-masks.md`](../../../tasks/backlog/geometry/GEOM-072-catmull-clark-crease-masks.md)

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Positions, scheme-specific face/edge adjacency, existing crease inputs and output budgets. |
| Compatible entity sources | Triangle or polygon meshes as accepted by the selected CPU scheme, not an artificial common triangle-only restriction. |
| RuntimeModule | Existing mesh subdivision operation/config/panel with per-scheme backend admission. |
| Config/agent | This task owns backend selection through the existing family preview/validate/apply path; add only genuinely missing typed authoring/config and UI surfaces, with file/agent/UI parity. |
| UI | Same validated operation/readiness and requested/actual/fallback diagnostics; internal-only kernels use the named consumer tasks above. |
| Publication | Explicit topology-changing operation using existing property/UV outcomes and undo/history. |
| End-to-end tests | This task owns CPU-oracle comparison, actual GPU readback, every supported input family, failed/stale/cancelled work and applicable config/UI/history coverage. |

## Acceptance criteria

- [ ] Implement one scheme at a time; compare stencils, boundary/crease behavior, extraordinary vertices and deterministic connectivity.
- [ ] Separate output-size scans/allocation from parallel writes; reject exceeded face/index budgets without partial publication.
- [ ] Match existing attribute transfer and UV-loss diagnostics. GEOM-072 features become eligible only after their CPU reference lands; do not silently invent crease behavior.
- [ ] Freeze parity tolerances, precision/device capabilities and representative
      fixtures before GPU tuning; do not weaken reference failure semantics.
- [ ] Reuse RHI, `ComputeParallelPrimitives`, framed JobService GPU work and
      `Graphics.GpuTransfer` where their contracts fit. Geometry/physics stay
      RHI-free; no parallel service/queue/registry, device-wide waits or borrowed
      ECS references survive asynchronous work.
- [ ] Prove requested/actual/fallback reporting and source revalidation through
      the declared control/publication path. Preserve canonical typed property
      eligibility, unrelated fields and topology; count changes are explicit.
- [ ] Register focused CPU cases and `GEOM088Vulkan` GPU cases (labels `gpu;vulkan`)
      and prove actual compute results against the independent CPU oracle.
      A fallback, skipped test or seeded reference-shaped GPU buffer is not proof.
- [ ] Reuse/extend manifest-backed benchmark harnesses with stable IDs and v2
      results under `build/ci-vulkan/benchmark-ctest/GEOM-088`. Measure cold/warm
      end-to-end time, transfers, compute, readback and memory against the current
      CPU/hybrid baseline. Benchmark fixtures run under the same `GEOM088Vulkan` selector.
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
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'GEOM088Vulkan' --no-tests=error --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/GEOM-088 --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
