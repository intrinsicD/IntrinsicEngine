---
id: PHYSICS-006
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
# PHYSICS-006 — Vulkan particle-spring reference integration

## Goal

Implement GPU force accumulation and integration for the existing particle-spring reference and bind it through runtime.

## Current state and scope

The method package provides a CPU pre-step spring-force accumulation and semi-implicit Euler integrator; no Vulkan spring backend exists.

Operator requested this candidate on 2026-09-17 after a source inspection.
This records authorized backlog planning outside the standing Framework24
selection preference; it is not a claim of GPU speedup or automatic adoption.
Use the existing CPU algorithm as the oracle, review its original formulation
and relevant later work, and complete the reference/tests/benchmark/optimized-CPU
evaluation sequence before a selectable GPU implementation. Keep algorithms,
precision requirements and default backend choices unchanged unless a separate
reviewed decision explicitly adopts a change.

Existing owners and evidence:

- [`methods/physics/particle_spring_reference/src/ParticleSpringReference.cpp`](../../../methods/physics/particle_spring_reference/src/ParticleSpringReference.cpp)
- [`methods/physics/particle_spring_reference/method.yaml`](../../../methods/physics/particle_spring_reference/method.yaml)
- [`docs/architecture/physics.md`](../../../docs/architecture/physics.md)

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Particle state, spring endpoint/rest/stiffness/damping records, pins, time step and reference forces. |
| Compatible entity sources | Explicit particle/spring simulation topology, which can be authored from a compatible graph without making geometry provenance the solver contract. |
| RuntimeModule | Physics owns CPU simulation descriptors/state; runtime owns Vulkan scheduling/resources and ECS synchronization. Do not import graphics/RHI/runtime or method packages into src/physics. |
| Config/agent | This task owns the missing method-specific simulation authoring/config and validated backend apply path. Reuse existing config-validation and PhysicsModule lifecycle patterns; the current rigid-body config does not already provide this method. File/agent/UI controls must agree. |
| UI | This task owns bounded method-specific controls using that same validated runtime operation, readiness and requested/actual/fallback diagnostics. |
| Publication | Atomic next simulation state and runtime writeback; preserve fixed particles and existing rigid bodies. |
| End-to-end tests | This task owns CPU-oracle comparison, actual GPU readback, every supported input family, failed/stale/cancelled work and applicable config/UI/history coverage. |

## Acceptance criteria

- [ ] Compute spring forces from the same pre-step state, reduce contributions per particle and integrate with the reference ordering.
- [ ] Choose deterministic gather/segmented reduction before floating atomic accumulation; pin tolerances and conservation/energy diagnostics.
- [ ] This task owns bounded physics state promotion and shared config/agent/UI controls; test single springs, chains, branching graphs, pins and multi-step cancellation/reset.
- [ ] Freeze parity tolerances, precision/device capabilities and representative
      fixtures before GPU tuning; do not weaken reference failure semantics.
- [ ] Reuse RHI, `ComputeParallelPrimitives`, framed JobService GPU work and
      `Graphics.GpuTransfer` where their contracts fit. Geometry/physics stay
      RHI-free; no parallel service/queue/registry, device-wide waits or borrowed
      ECS references survive asynchronous work.
- [ ] Prove requested/actual/fallback reporting and source revalidation through
      the declared control/publication path. Preserve canonical typed property
      eligibility, unrelated fields and topology; count changes are explicit.
- [ ] Register focused CPU cases and `PHYSICS006Vulkan` GPU cases (labels `gpu;vulkan`)
      and prove actual compute results against the independent CPU oracle.
      A fallback, skipped test or seeded reference-shaped GPU buffer is not proof.
- [ ] Reuse/extend manifest-backed benchmark harnesses with stable IDs and v2
      results under `build/ci-vulkan/benchmark-ctest/PHYSICS-006`. Measure cold/warm
      end-to-end time, transfers, compute, readback and memory against the current
      CPU/hybrid baseline. Benchmark fixtures run under the same `PHYSICS006Vulkan` selector.
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
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'PHYSICS006Vulkan' --no-tests=error --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/PHYSICS-006 --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
