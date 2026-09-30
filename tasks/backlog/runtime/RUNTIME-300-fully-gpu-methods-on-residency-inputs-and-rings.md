---
id: RUNTIME-300
theme: I
depends_on: [RUNTIME-293, GRAPHICS-156]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: slice of RUNTIME-294 planned from ADR 0030 decisions 8-9 (2026-09-30); implementation owes the contract tests and the gpu;vulkan parity + IO smoke listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources, method.engine-integration]
---
# RUNTIME-300 — Fully-GPU methods onto residency inputs and rings (LOP, k-means, FPS, keypoints)

## Goal
- RUNTIME-294 rows "LOP", "k-means", "FPS" and "Keypoints": one task, because each already records its own compute with no CPU stage and the change per method is the same seam swap (the private input upload becomes `ResolveGpuPropertyInput`; the terminal readback becomes a ring plus Accept). Split a row out if it turns out larger than a session.
- LOP: stride-12 kernels read the canonical positions; a position ring previews every k iterations through the `GpuWorld` position preview; Accept through the positions run API (RUNTIME-293).
- k-means: a labels ring (integer, with a float presentation ring for the colormap); Accept through the scalar transaction.
- FPS: positions -> double on the device from the canonical slot; a completion-only submit replaces the interim double copy; order / mask published through the existing publication (no preview).
- Keypoints: index views from the residency; score / mask rings; Accept through the scalar transaction.
- ADR 0030 decisions 8-9: the kernels record from `GpuPropertyView` inputs into ring outputs, never from CPU spans; `RequestedBackend` / `ActualBackend`, fallback reasons and the residency IO counters stay uniform. The CPU reference stays canonical; the GPU backend reports its identity and parity delta.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged per method: a count-matched vec3 position property on any point domain. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds. |
| RuntimeModule | Existing modules: `PointCloudConsolidation` (LOP), clustering (k-means), `PointSamplingOperations` (FPS), `PointAnalysisOperations` (keypoints). |
| Config/agent | Unchanged backend enums; IO counters in the results and agent output. |
| UI | Each panel: Accept / Discard, observation state, IO counters. |
| Publication | Same cardinality per method. GPU preview: LOP yes (positions), k-means yes (label colormap), keypoints yes (score), FPS no; commit via the positions run API (LOP) or the scalar transaction (labels, scores, masks); FPS via its existing publication. |
| End-to-end tests | Contract tests on the mock device per method; one gpu;vulkan parity + IO smoke per method. |

## Acceptance criteria
- [ ] gpu;vulkan parity smoke per method: the accepted rows (LOP positions, k-means labels, keypoint scores / masks) and FPS's published order / mask equal the CPU reference within a stated, justified tolerance.
- [ ] IO counters: a second run on the same input revision uploads zero input bytes (the residency reports uploads, hits and any declared CPU-stage bytes in the result and the agent output).
- [ ] Panel Accept / Discard for LOP, k-means and keypoints (Accept disabled with its reason when stale); FPS publishes on completion as today; batch and agent commands accept automatically.
- [ ] `method.engine-integration` publication row states "GPU preview: yes/no; commit via X" and the method docs record the backend identity and parity delta.
- [ ] Each of the four methods moves data only at start (resident input) and end (Accept readback or the terminal publication).

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
