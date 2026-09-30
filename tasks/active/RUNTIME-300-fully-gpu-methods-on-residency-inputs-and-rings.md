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
- ADR 0030 decisions 8-9: the kernels record from `GpuPropertyView` inputs into ring outputs, never from CPU spans; `RequestedBackend` / `ActualBackend`, fallback reasons and the residency IO counters stay uniform. The CPU reference stays canonical; the GPU backend reports its identity, and Vulkan tests record its parity delta.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged per method: a count-matched vec3 position property on any point domain. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds. |
| RuntimeModule | Existing modules: `PointCloudConsolidation` (LOP), clustering (k-means), `PointSamplingOperations` (FPS), `PointAnalysisOperations` (keypoints). |
| Config/agent | Unchanged backend enums; LOP `gpu_preview_interval` in the validated config; IO counters in results and `run_point_cloud_consolidation` agent output. |
| UI | Each panel: Accept / Discard, observation state, IO counters. |
| Publication | Same cardinality per method. GPU preview: LOP yes (positions), k-means yes (label colormap), keypoints yes (score), FPS no; commit via the positions run API (LOP) or the scalar transaction (labels, scores, masks); FPS via its existing publication. |
| End-to-end tests | Contract tests on the mock device per method; one gpu;vulkan parity + IO smoke per method. |

## Acceptance criteria
- [ ] gpu;vulkan parity smoke per method: the accepted rows (LOP positions, k-means labels, keypoint scores / masks) and FPS's published order / mask equal the CPU reference within a stated, justified tolerance.
- [ ] IO counters: a second run on the same input revision uploads zero input bytes (the residency reports uploads, hits and any declared CPU-stage bytes in the result and the agent output).
- [ ] Panel Accept / Discard for LOP, k-means and keypoints (Accept disabled with its reason when stale); FPS publishes on completion as today; batch and agent commands accept automatically.
- [ ] `method.engine-integration` publication row states "GPU preview: yes/no; commit via X" and the method docs record the backend identity and parity delta.
- [ ] Each of the four methods moves data only at start (resident input) and end (Accept readback or the terminal publication).

## Current implementation slice and remaining work

The FPS slice changes canonical position/weight input acquisition, device-side
live-row gathering/world conversion, completion-only intermediate submissions,
terminal publication guards, and panel/agent IO reporting. Its existing
`PointLBVHGpuTestObjs` smoke now checks repeat-run residency and reports the
measured clearance delta when executed. GPU execution is still required; no
Operational or parity verdict is recorded for the changed path.

The other rows remain under this task's split-out provision:

- **Keypoints:** retain residency-backed index views through completion; page
  spacing, covariance and suppression work instead of recording every page
  in one submission; write score/mask rings and extend/reuse the scalar
  transaction for atomic publication; wire panel/agent lifecycle and IO
  reporting; add contract and Vulkan parity/IO/preview tests.

Reuse review: FPS keeps `Runtime.PointSamplingOperations`' existing atomic
rank/mask history entry and `Runtime.EditorFramedGpuJob`'s lifecycle. The
completion-only path extends `SpatialIndexCache` and Vulkan's existing timeline
callback queue. No parallel scheduler, transaction service or transfer queue
was introduced. FPS has no preview or output-ring `BindRevision` by design.

## Verification
```bash
# Focused CPU gate (AGENTS.md §7): build the owning test executables, run the touched suites.
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(<touched suites>)\.'
# Per-method Vulkan parity/IO smoke on the opt-in display host.
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900 -R '<method smoke suite>'
```
- 2026-09-30 (slice 1 committed: FPS). FPS reads canonical resident positions/weights,
  gathers and converts on the device, runs bounded completion-only intermediate submissions
  and publishes through the existing undoable terminal publication. Review follow-ups:
  - the transfer timeline signal uses ALL_COMMANDS, so completion-only compute is covered;
  - an all-live input uploads no row map;
  - the panel test waits for the published normals.

  Reviewed by Claude Opus 5.5: no P1/P2. Gates: CPU 5410/5410; GPU suite green except the
  environmental `VulkanShutdownLsanContract` (RUNTIME290 FPS smoke passes). Remaining in
  this task: LOP, k-means and keypoints (see the items above).

- 2026-09-30 (slice 2: LOP). Stride-12 kernels read the canonical resident positions;
  pages are sized from the measured 27-cell candidate maximum (2^18 pairs per page,
  2^24 per submission, immediate completions) so an iteration needs a few submissions and
  degenerate clouds stay bounded; the host reads the finalize diagnostics and stops on
  convergence like the CPU reference. The positions run ring previews every
  `gpu_preview_interval` (default 5) iterations; Stop, stale-aware Accept, Discard and
  batch/agent auto-accept use the RUNTIME-293 positions run API. Named-output/downsampling
  keeps its terminal publication; WLOP/CLOP/EAR are unchanged (RUNTIME-303/304).
  Gates: all 9 `PointCloudConsolidationGpuParity` Vulkan tests pass on the RTX 3050
  (Xephyr). LOP vs CPU reference: resident Linf 6.1e-6 / RMS 1.7e-6; multi-page uniform
  Linf 4.3e-7, identical points 0; frozen fixture Linf 3.1e-4 / RMS 3.2e-5 with identical
  iteration counts (atomic grid-scatter order; tolerance Linf 2e-3 / RMS 5e-4). Repeat run
  uploads 0 input bytes. Focused CPU 111/111; layering strict clean. Reviewed by Codex
  (read-only) and Claude Opus 5.5; the paging P1 and all P2s were fixed. Remaining:
  k-means and keypoints.

- 2026-10-01 (slice 3: k-means). Lloyd iterations read the canonical resident positions and
  run brute-force assignment plus parallel, fixed-shape double reductions (update/reduce), paged
  at <= 2^18 pairs per page, <= 2^24 per submission and <= 2^14 serial lane depth, with
  immediate completions and host convergence matching `Geometry.KMeans` (whose centroid sums
  are now double). A uint label ring plus a float presentation ring preview the colormap every
  `gpu_preview_interval`; Accept/Discard go through the extended `PointScalarTransaction`
  (typed publication reserves both rings at admission). The cluster range stays [1, 1024].
  Gates: `ClusteringServiceGpuSmoke` (3 cases incl. centroid-page ties, 2,049 points/k=8, deleted
  slot) 0 label mismatches, centroid Linf 0; RUNTIME292/297/298 scalar smokes pass; CPU
  RuntimeContract 1518/1518, SandboxEditorIntegration 143/143, Geometry 1606/1606; layering clean.
  The old smoke's `IsOperational()` gate always skipped at cold start and now gates on
  bootstrap readiness; the GMM likelihood assertion is non-strict because the double-sum
  initializer can already be EM's fixed point. Reviewed by Codex and Claude Opus 5.5 (serial
  reduction P1, retired-resource shutdown P1 and all P2s fixed). Not yet measured: 1M-point
  timing. Remaining: keypoints.
