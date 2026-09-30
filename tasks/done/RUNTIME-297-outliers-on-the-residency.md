---
id: RUNTIME-297
theme: I
depends_on: [RUNTIME-292, GRAPHICS-154]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: slice of RUNTIME-294 planned from ADR 0030 decisions 8-9 (2026-09-30); implementation owes the contract tests and the gpu;vulkan parity + IO smoke listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources, method.engine-integration]
---
# RUNTIME-297 — Outlier analysis on the GPU property residency

## Goal
- RUNTIME-294 row "Outliers (statistical, radius, LDR, remove)".
- Input: positions from the canonical residency slot through the LBVH (`SpatialIndexCache`, GRAPHICS-154).
- Output: the score as a float ring (observed by the colormap like any scalar) and the mask as a typed ring with a float presentation ring; Accept through the scalar transaction (`PublishPointScalarField`). Removal changes the cardinality and publishes atomically through the existing removal command (no ring).
- CPU stage today: reductions on downloaded neighborhoods. Port: per-point kNN / radius-count kernels, a fixed-order mean / std reduction (deterministic), stream compaction for removal.
- ADR 0030 decisions 8-9: the kernels record from `GpuPropertyView` inputs into ring outputs, never from CPU spans; `RequestedBackend` / `ActualBackend`, fallback reasons and the residency IO counters stay uniform. The CPU reference stays canonical; the GPU backend reports its identity and parity delta.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: a count-matched vec3 position property on any point domain. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds. |
| RuntimeModule | `Extrinsic.Runtime.PointAnalysisOperations` (existing outlier operations). |
| Config/agent | Unchanged backend enum; IO counters in the result and agent output. |
| UI | Outlier panel: Accept / Discard for a GPU result, the observation state and the IO counters. |
| Publication | Score and mask on the input domain, same cardinality. GPU preview: yes for the score (colormap scalar); no for the mask and removal; commit via the scalar transaction, removal via the existing atomic removal publication. |
| End-to-end tests | Contract tests on the mock device; one gpu;vulkan parity + IO smoke. |

## Completion — 2026-09-30
Commit: see RETIREMENT-LOG (`claude/cpd-nystrom`). Statistical, radius and LDR outliers run
on the GPU property residency with backend `vulkan_lbvh`:
- Positions come from the canonical slot through the LBVH. The shared `lbvhQuery` traversal
  (`point_lbvh.glsl`) is used by both `lbvh_query.comp` and `outlier_analysis.comp`.
- The mean/std reduction is fixed-order and done in double.
- Score and mask are written to rings (the score is observed by the colormap), with float
  presentation.
- Accept goes through the existing atomic two-field publication, then `BindRevision`.
- Removal stays on the atomic CPU compaction stage.
- Admission refuses radius² below `FLT_MIN` (float32 denormal preservation is not guaranteed)
  and sizes the workspace per method.
- Panels keep terminal and refused-start results on screen, for outliers and normals.

Evidence:
- Contract tests `OutlierTransaction.*` and the panel test
  `SandboxProcessingPanels.GpuTransactionsDisplayTerminalFailuresAndStaleResults`.
- gpu;vulkan `RUNTIME297OutlierResidency.ParityResidentSecondRunAndDiscard`: measured max
  deltas 0 / 0 / 2e-6 (statistical / radius / LDR), zero-upload second run, Discard keeps the
  rows.
- `PointLBVHGpuSmoke` outlier phases now cancel a real pending Accept.
- Implemented by Codex 6 Astra (medium), reviewed by independent Codex instances; all findings
  fixed:
  - subnormal intermediates;
  - admission width;
  - panel terminal results;
  - cancellation test;
  - refused-start results.
- CPU gate 5345/5345. GPU suite 132/133, only the environmental `VulkanShutdownLsanContract`
  red. Operational.

## Acceptance criteria
- [x] gpu;vulkan parity smoke: the accepted rows equal the CPU reference within a stated, justified tolerance.
- [x] IO counters: a second run on the same input revision uploads zero input bytes (the residency reports uploads, hits and any declared CPU-stage bytes in the result and the agent output).
- [x] Panel Accept / Discard (Accept disabled with its reason when stale); batch and agent commands accept automatically.
- [x] `method.engine-integration` publication row states "GPU preview: yes/no; commit via X" and the method docs record the backend identity and parity delta.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```

## Implementation decisions
- Reuse: `SpatialIndexCache` owns the index and residency; compact deleted-row inputs
  gather from the canonical slot on the device. `lbvhQuery` shares the existing
  sorted kNN/radius traversal with the outlier shader. `GpuFrontReadback` and the
  existing atomic two-field publisher own Accept; no new publication framework.
- Right-sizing: one graphics workspace isolates device recording from runtime's
  ECS/config/history transaction; no new service or queue. The fixed-order reduction
  is a single invocation, with no float atomics.
- Removal remains the existing atomic CPU compaction stage (no extra GPU readback).
- Vulkan storage is float score / uint32 mask plus float presentation. Unsupported
  storage, absent float64, subnormal inputs and workspace limits are admission refusals.
- Measured on the operator's machine (RTX 3050): see the completion section.
