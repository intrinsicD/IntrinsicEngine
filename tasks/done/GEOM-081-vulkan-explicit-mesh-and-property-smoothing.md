---
id: GEOM-081
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
# GEOM-081 — Vulkan explicit mesh and property smoothing

## Completion — 2026-09-26
Commit: the enclosing `claude/geom-081-vulkan-property-smoothing` commit records
this retirement. Operational parity on the recorded RTX 3050 (C111): 84 editor-path
runs match the CPU reference bitwise for averaging, Taubin and spectral heat and
within 9.2e-17 relative for bilateral. Stage timings and memory moved to GEOM-103;
the pre-existing ARA proof-path failure is BUG-224.

## Goal

Add Vulkan execution for uniform/cotan Laplacian, Taubin and existing cotan scalar/vector property smoothing.

Scope as implemented: since the property-smoothing unification, mesh fairing is
`FilterProperty` on `v:position`, so the explicit filters (averaging, Taubin,
bilateral, spectral heat) cover positions and every floating property on all
eight canonical domains with kNN, cotangent and uniform mesh weights.

## Current state and scope

CPU implementations already use simultaneous updates with separate current/next buffers, giving a concrete per-vertex parallelization boundary.

Operator requested this candidate on 2026-09-17 after a source inspection.
This records authorized backlog planning outside the standing Framework24
selection preference; it is not a claim of GPU speedup or automatic adoption.
Use the existing CPU algorithm as the oracle, review its original formulation
and relevant later work, and complete the reference/tests/benchmark/optimized-CPU
evaluation sequence before a selectable GPU implementation. Keep algorithms,
precision requirements and default backend choices unchanged unless a separate
reviewed decision explicitly adopts a change.

Existing owners and evidence:

- [`src/geometry/Geometry.PropertySmoothing.cpp`](../../src/geometry/Geometry.PropertySmoothing.cpp) (CPU reference, `PlanPropertyFilter`, `CompletePropertyFilter`)
- [`src/runtime/Editor/Operations/Runtime.MeshFieldOperations.Smoothing.cpp`](../../src/runtime/Editor/Operations/Runtime.MeshFieldOperations.Smoothing.cpp) (backend selection, framed job, publication)
- [`src/graphics/renderer/Graphics.PropertyFilter.cpp`](../../src/graphics/renderer/Graphics.PropertyFilter.cpp) and [`assets/shaders/property_filter.comp`](../../assets/shaders/property_filter.comp) (kernels)

## Log

- 2026-09-26: GPU recording reuses `SpatialIndexCache::QueueGpuCompute` through a
  new index-free overload instead of a new service. Vulkan refuses without shader
  double precision (no hidden CPU fallback), like the keypoint backend. Bitwise
  parity comes from per-row ascending-edge accumulation with `precise` doubles;
  bilateral needs a custom double `exp`. Implicit smoothing stays GEOM-089; the
  variational fit has no GPU backend. On the locked desktop seat presents run at
  about 1 Hz, so the suite runs under Xephyr (`DISPLAY=:7`).

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Mesh adjacency/positions and, for property smoothing, the selected compatible scalar/vector property plus active mask. |
| Compatible entity sources | Mesh vertex domains satisfying each existing stencil; this topology-based operation is not generalized to arbitrary point sets. |
| RuntimeModule | Existing mesh topology/field operations and config paths, sharing only matching compiled stencil machinery. |
| Config/agent | This task owns backend selection through the existing family preview/validate/apply path; add only genuinely missing typed authoring/config and UI surfaces, with file/agent/UI parity. |
| UI | Same validated operation/readiness and requested/actual/fallback diagnostics; internal-only kernels use the named consumer tasks above. |
| Publication | Positions or the explicitly selected property; preserve topology, inactive/boundary rows and unrelated fields. |
| End-to-end tests | This task owns CPU-oracle comparison, actual GPU readback, every supported input family, failed/stale/cancelled work and applicable config/UI/history coverage. |

## Acceptance criteria

- [x] Keep each iteration on the GPU using ping-pong buffers; preserve boundary pins, deleted/isolated rows and active-neighbor filtering.
- [x] Preserve when cotan weights and geometry-dependent quantities are recomputed; do not replace one reference operator with another.
- [x] Cover all currently supported floating property kinds and Taubin pass order. Implicit smoothing belongs to GEOM-089.
- [x] Freeze parity tolerances, precision/device capabilities and representative
      fixtures before GPU tuning; do not weaken reference failure semantics.
- [x] Reuse RHI, `ComputeParallelPrimitives`, framed JobService GPU work and
      `Graphics.GpuTransfer` where their contracts fit. Geometry/physics stay
      RHI-free; no parallel service/queue/registry, device-wide waits or borrowed
      ECS references survive asynchronous work.
- [x] Prove requested/actual/fallback reporting and source revalidation through
      the declared control/publication path. Preserve canonical typed property
      eligibility, unrelated fields and topology; count changes are explicit.
- [x] Register focused CPU cases and `GEOM081Vulkan` GPU cases (labels `gpu;vulkan`)
      and prove actual compute results against the independent CPU oracle.
      A fallback, skipped test or seeded reference-shaped GPU buffer is not proof.
- [x] (Split) Reuse/extend manifest-backed benchmark harnesses — the parity benchmark `geometry.property_smoothing.vulkan_explicit_parity` seals v2 results under `build/ci-vulkan/benchmark-ctest/GEOM-081` via `GEOM081VulkanPropertySmoothingBenchmark`; cold/warm transfer, compute, readback and memory measurements moved to GEOM-103. Original wording: with stable IDs and v2
      results under `build/ci-vulkan/benchmark-ctest/GEOM-081`. Measure cold/warm
      end-to-end time, transfers, compute, readback and memory against the current
      CPU/hybrid baseline. Benchmark fixtures run under the same `GEOM081Vulkan` selector.
- [x] Update affected method manifests/backend documentation, canonical
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
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'GEOM081Vulkan' --no-tests=error --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/GEOM-081 --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
