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
- [ ] Backend axis (`cpu_reference` / `gpu_vulkan_compute`) through the RUNTIME-290 seam with requested/actual/fallback reporting; small landmark sets (CPD) stay on the CPU.

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

