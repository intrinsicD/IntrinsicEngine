---
id: PHYSICS-005
theme: C
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Operator-requested interactive GPU candidate backlog; implementation uses reviewed diffs, tests and benchmark/ARA evidence for any resulting claims.
contract_schema: 1
contracts: [repo.source-documentation, method.engine-integration, geometry.element-domain-sources, geometry.property-coherence]
---
# PHYSICS-005 — Vulkan SPH fluid reference integration

## Goal

Implement a bounded Vulkan backend for the existing weakly compressible SPH reference, with runtime integration and CPU comparison.

## Current state and scope

The SPH method package implements a deterministic CPU all-pairs reference; there is no GPU fluid backend or existing fluid world integration to assume.

Operator requested this candidate on 2026-09-17 after a source inspection.
This records authorized backlog planning outside the standing Framework24
selection preference; it is not a claim of GPU speedup or automatic adoption.
Use the existing CPU algorithm as the oracle, review its original formulation
and relevant later work, and complete the reference/tests/benchmark/optimized-CPU
evaluation sequence before a selectable GPU implementation. Keep algorithms,
precision requirements and default backend choices unchanged unless a separate
reviewed decision explicitly adopts a change.

Existing owners and evidence:

- [`methods/physics/sph_fluid_reference/src/SphFluidReference.cpp`](../../../methods/physics/sph_fluid_reference/src/SphFluidReference.cpp)
- [`methods/physics/sph_fluid_reference/method.yaml`](../../../methods/physics/sph_fluid_reference/method.yaml)
- [`docs/architecture/physics.md`](../../../docs/architecture/physics.md)

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | SPH particle positions/velocities/masses, boundary planes, time step and existing fluid parameters. |
| Compatible entity sources | Explicit fluid particle state; arbitrary geometry properties alone are not a simulation state. |
| RuntimeModule | Physics owns CPU simulation descriptors/state; runtime owns Vulkan scheduling/resources and ECS synchronization. Do not import graphics/RHI/runtime or method packages into src/physics. |
| Config/agent | This task owns the missing method-specific simulation authoring/config and validated backend apply path. Reuse existing config-validation and PhysicsModule lifecycle patterns; the current rigid-body config does not already provide this method. File/agent/UI controls must agree. |
| UI | This task owns bounded method-specific controls using that same validated runtime operation, readiness and requested/actual/fallback diagnostics. |
| Publication | Validated next fluid state at a simulation-step boundary; keep rigid-body world behavior unchanged. |
| End-to-end tests | This task owns CPU-oracle comparison, actual GPU readback, every supported input family, failed/stale/cancelled work and applicable config/UI/history coverage. |

## Spatial acceleration consideration

Compare uniform-grid versus complete radius candidates for fixed support; moving particles require rebuild/update per contracted step. Preserve self terms and all support contributions. Runtime may reuse graphics workspace machinery; physics cannot import it.

Consult the [consumer inventory](../../../docs/architecture/spatial-index-consumers.md) and update its owner/reuse notes when implementation changes the path.

## Acceptance criteria

- [ ] Preserve the reference density/pressure/viscosity, pressure clamp, optional terms, integration and boundary formulas in separate GPU passes.
- [ ] Retain CPU reference as independent test oracle; promote only necessary production state/CPU semantics into physics and own fluid config/agent/UI authoring in this task.
- [ ] Compare multi-step density, mass, momentum, energy and boundary behavior; no truncated neighbor physics, unstable-step publication or silent precision downgrade.
- [ ] Freeze parity tolerances, precision/device capabilities and representative
      fixtures before GPU tuning; do not weaken reference failure semantics.
- [ ] Reuse RHI, `ComputeParallelPrimitives`, framed JobService GPU work and
      `Graphics.GpuTransfer` where their contracts fit. Geometry/physics stay
      RHI-free; no parallel service/queue/registry, device-wide waits or borrowed
      ECS references survive asynchronous work.
- [ ] Prove requested/actual/fallback reporting and source revalidation through
      the declared control/publication path. Preserve canonical typed property
      eligibility, unrelated fields and topology; count changes are explicit.
- [ ] Register focused CPU cases and `PHYSICS005Vulkan` GPU cases (labels `gpu;vulkan`)
      and prove actual compute results against the independent CPU oracle.
      A fallback, skipped test or seeded reference-shaped GPU buffer is not proof.
- [ ] Reuse/extend manifest-backed benchmark harnesses with stable IDs and v2
      results under `build/ci-vulkan/benchmark-ctest/PHYSICS-005`. Measure cold/warm
      end-to-end time, transfers, compute, readback and memory against the current
      CPU/hybrid baseline. Benchmark fixtures run under the same `PHYSICS005Vulkan` selector.
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
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'PHYSICS005Vulkan' --no-tests=error --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/PHYSICS-005 --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
