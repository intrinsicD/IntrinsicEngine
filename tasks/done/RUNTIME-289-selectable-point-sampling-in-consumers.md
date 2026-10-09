---
id: RUNTIME-289
theme: I
depends_on: [GEOM-111]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note; implementation owes contract tests per consumer.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources]
---
# RUNTIME-289 — Selectable point sampling wherever points are chosen

## Goal
- Every operation that picks a subset of points exposes the sampling method of
  `Geometry.PointSampling` with one shared config block (`PointSamplingConfig`, spec-driven
  fields) and one panel widget: CPD source/target subsampling, CPD low-rank and Nystroem
  landmarks, point-cloud consolidation initial samples (geometry and the Vulkan runtime path
  together), and later consumers.

## Acceptance criteria
- [x] Shared runtime config struct + validator + panel helper; the runtime enum mirrors `PointSamplingMethod` with a static_assert.
- [x] The progressive Poisson options of GEOM-112 (cell policy, retries, budget, priority property and bands, phase order, ordering, order-only, profiles) are fields of the shared block.
- [x] CPD: `subsample_method`, target subsample, landmark method; consolidation: initial-sample method (CPU and GPU paths identical).
- [x] Agent fields and contract tests per consumer.
- [x] Backend axis (`cpu_reference` / `gpu_vulkan_compute`) through the RUNTIME-290 seam with requested/actual/fallback reporting; small landmark sets (CPD) stay on the CPU. (2026-10-09: a framed device ordering job before the solver job, see the log; Codex code review approved.)

## Completion
Completed 2026-10-09. Commit: the RUNTIME-289 backend-axis commit ("Order CPD subsamples and
consolidation initial samples on Vulkan (RUNTIME-289)"). Operational for exact farthest point
(the only method with a device kernel): gpu;vulkan smokes `RUNTIME289VulkanCpdSubsamples` and
`PointCloudConsolidationGpuParity.VulkanOrderedInitialSamplesMatchTheCpuOrder` pass on this
host. Other methods report the seam's reason and order on the CPU. Known limit: no dedicated
test cancels a consolidation run during its Vulkan projection preparation (reviewer: coverage
limitation, not blocking).

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Point positions of the consumer's operands. |
| Compatible entity sources | Every canonical point domain, as the consumers already accept. |
| RuntimeModule | Existing consumer operations (registration, consolidation). |
| Config/agent | Shared `PointSamplingConfig` block inside each consumer's section. |
| UI | Shared sampling widget in each consumer panel. |
| Publication | Unchanged per consumer. |
| End-to-end tests | Contract test per consumer with a non-default method. |

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift|Consolidation|PointSampling' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```

## Log
- 2026-09-28 slice 1: `Extrinsic.Runtime.PointSamplingConfig` (prefixed field specs, validator,
  geometry mapping with static_asserts), JSON codec in the internal codec unit, panel helper
  `Sandbox.PointSamplingControls.hpp`; CPD consumes it (`subsample_*`, `subsample_target`,
  `landmark_*` for low rank and Nystroem). Default exact farthest point keeps every earlier
  result. Next: consolidation initial samples (hand-written codec; CPU and GPU paths through
  one helper), then the backend axis with RUNTIME-290.
- 2026-09-28 slice 2: consolidation `initial_*` (hand-written codec checks the block with the
  shared specs; invalid blocks warn and keep the default); geometry `SelectInitialSamples`
  now serves the CPU reference and the runtime's Vulkan path, so both start from the same
  samples (gpu;vulkan consolidation tests 7/7). `Random` keeps the legacy seeded subsample.
  Agent access is through `config_apply` (the section fields). Remaining: the backend axis,
  which needs RUNTIME-290.
- 2026-10-09 slice 3 (backend axis): plan review by Codex (`gpt-6-astra`, xhigh, thread
  `01a12003-638d-7210-9b10-a5e2bff14786`, packet sha256 `2f895f2d…b131`, recursion protection
  instruction-only) answered `revise`: precompute instead of a blocking worker broker (both
  consumers sample once before solving), consolidation in local space with identity model,
  validate injected orders, define cancellation and per-set reporting. Implemented that way:
  a framed device job (`PointSamplingGpuRun`, resident positions) runs before the solver job,
  which depends on it (`JobDesc::DependsOn`; a cancelled or stale ordering cancels the run) and
  takes the verified order through `CPD::Params::Subsample{Target,Source}Order` /
  `Consolidation::Params::InitialOrder` (geometry rejects repeated or out-of-range indices).
  Config `subsample_backend` / `initial_backend`; results, agent fields and panels report the
  requested backend, the backend per set and the reason. Evidence: default-gate contract tests
  (deviceless fallback per consumer, mock-device publish with a correct and a swapped order),
  geometry validation tests (each mutation-checked), gpu;vulkan smokes on this host: CPD
  Bayesian with both subsamples on the device equals the CPU run (displacements identical),
  consolidation with a device initial order equals the CPU run (positions identical).
- 2026-10-09 code review (Codex `gpt-6-astra`, xhigh, thread `01a12027-b16a-7151-8ed7-487b2176fd96`,
  recursion protection instruction-only): round 1 on diff sha256 `43f09118…d9ce`: "revise required"
  (CPD cancel did not stop the ordering; reporting lost on failure/cancel paths; consolidation
  early exits; no handoff test). Round 2 on `ea448ceb…69ba`: "revise required" (projection
  finalizer read the worker-owned snapshot; step-job cancel/rejection did not stop the ordering;
  moved diagnostic; P2: a run that ends before sampling reports the planned backend). Operator
  decision on P2 (2026-10-09, verbatim: "So lassen"): keep "the backend that orders, or for a run
  ending before solving, was to order" and document it; the reviewer's position was that results
  must distinguish "not executed" from a CPU fallback. Round 3 on `64ce6fde…4c679`: "Overall
  verdict: approve." Full CPU gate 5734/5734 on the final state.
