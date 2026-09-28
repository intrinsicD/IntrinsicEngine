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
- [ ] Shared runtime config struct + validator + panel helper; the runtime enum mirrors `PointSamplingMethod` with a static_assert.
- [ ] CPD: `subsample_method`, target subsample, landmark method; consolidation: initial-sample method (CPU and GPU paths identical).
- [ ] Agent fields and contract tests per consumer.

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
