---
id: PHYSICS-007
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
# PHYSICS-007 — Vulkan XPBD cloth constraints and integration

## Goal

Implement a conflict-aware Vulkan backend for the existing XPBD cloth reference with an explicit numerical-equivalence gate.

## Current state and scope

The CPU reference updates shared particles sequentially through stretch/bend constraints; launching every constraint concurrently would introduce races and change the method.

Operator requested this candidate on 2026-09-17 after a source inspection.
This records authorized backlog planning outside the standing Framework24
selection preference; it is not a claim of GPU speedup or automatic adoption.
Use the existing CPU algorithm as the oracle, review its original formulation
and relevant later work, and complete the reference/tests/benchmark/optimized-CPU
evaluation sequence before a selectable GPU implementation. Keep algorithms,
precision requirements and default backend choices unchanged unless a separate
reviewed decision explicitly adopts a change.

Existing owners and evidence:

- [`methods/physics/xpbd_cloth_reference/src/XpbdClothReference.cpp`](../../../methods/physics/xpbd_cloth_reference/src/XpbdClothReference.cpp)
- [`methods/physics/xpbd_cloth_reference/method.yaml`](../../../methods/physics/xpbd_cloth_reference/method.yaml)
- [`docs/architecture/physics.md`](../../../docs/architecture/physics.md)

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Cloth particle state, triangle connectivity, stretch/bend constraints, compliance, pins and colliders. |
| Compatible entity sources | Cloth states satisfying the reference topology/constraint contract, with mesh authoring handled by runtime. |
| RuntimeModule | Physics owns CPU simulation descriptors/state; runtime owns Vulkan scheduling/resources and ECS synchronization. Do not import graphics/RHI/runtime or method packages into src/physics. |
| Config/agent | This task owns the missing method-specific simulation authoring/config and validated backend apply path. Reuse existing config-validation and PhysicsModule lifecycle patterns; the current rigid-body config does not already provide this method. File/agent/UI controls must agree. |
| UI | This task owns bounded method-specific controls using that same validated runtime operation, readiness and requested/actual/fallback diagnostics. |
| Publication | Next cloth state at the fixed-step boundary, then validated runtime geometry writeback. |
| End-to-end tests | This task owns CPU-oracle comparison, actual GPU readback, every supported input family, failed/stale/cancelled work and applicable config/UI/history coverage. |

## Acceptance criteria

- [ ] Compare graph-colored conflict-free solves and an explicitly formulated Jacobi candidate; freeze the chosen schedule and lambda lifecycle before implementation.
- [ ] Scheduling changes require a matched CPU formulation and reference error bounds, not just more iterations until an image looks similar.
- [ ] Own bounded cloth state/config/agent/UI integration; test pinned cloth, compliance across dt/iteration counts, degenerate constraints, collisions and multi-step stretch/energy drift.
- [ ] Freeze parity tolerances, precision/device capabilities and representative
      fixtures before GPU tuning; do not weaken reference failure semantics.
- [ ] Reuse RHI, `ComputeParallelPrimitives`, framed JobService GPU work and
      `Graphics.GpuTransfer` where their contracts fit. Geometry/physics stay
      RHI-free; no parallel service/queue/registry, device-wide waits or borrowed
      ECS references survive asynchronous work.
- [ ] Prove requested/actual/fallback reporting and source revalidation through
      the declared control/publication path. Preserve canonical typed property
      eligibility, unrelated fields and topology; count changes are explicit.
- [ ] Register focused CPU cases and `PHYSICS007Vulkan` GPU cases (labels `gpu;vulkan`)
      and prove actual compute results against the independent CPU oracle.
      A fallback, skipped test or seeded reference-shaped GPU buffer is not proof.
- [ ] Reuse/extend manifest-backed benchmark harnesses with stable IDs and v2
      results under `build/ci-vulkan/benchmark-ctest/PHYSICS-007`. Measure cold/warm
      end-to-end time, transfers, compute, readback and memory against the current
      CPU/hybrid baseline. Benchmark fixtures run under the same `PHYSICS007Vulkan` selector.
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
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'PHYSICS007Vulkan' --no-tests=error --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/PHYSICS-007 --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
