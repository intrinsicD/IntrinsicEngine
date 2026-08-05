---
id: BUG-131
theme: G
depends_on: [GEOM-075, METHOD-020]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "Codex-LOPVisibility"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-05T06:52:29Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.support-radius-policy, method.engine-integration]
contract_review: "BUG-131 repairs backend-aware automatic-radius admission, restores WLOP's paper-defined 1+sum density base case consistently on CPU/Vulkan, and preserves the existing property-aware publication path without narrowing compatible domains or changing cardinality."
maturity_target: Operational
---
# BUG-131 — LOP Auto radius rejects visible imported-mesh execution

## Status

- Completed on 2026-08-05 at `Operational` for ordinary LOP and isotropic
  WLOP on the repository `child.obj` asset and this Vulkan-capable host. The
  final opt-in run selected actual `gpu_vulkan_compute` with zero fallback,
  non-zero canonical displacement, and a changed resident position
  fingerprint/content revision. This is no speedup, convergence,
  visual-quality, or cross-device claim.
- Clean-workshop manual scorecard: row 3 pass (new public fields expose only
  owning-layer scalar data); rows 4–6 not applicable (no renderer subsystem,
  frame-graph pass, or recipe edge was added); no follow-up findings.
- Commit: pending final evidence binding.

## Maturity

- Reached: `Operational` on a Vulkan-capable host, with `CPUContracted`
  behavior retained by the default, ASan, and UBSan gates.

## Goal
- Make a default Auto-radius LOP or isotropic-WLOP request on the imported
  `child.obj` mesh enter the operational Vulkan path when its concrete GPU plan
  is safe, publish canonical position changes, and expose unambiguous
  submitted/running/applied diagnostics in the Sandbox panel.

## Non-goals
- Do not weaken or bypass CPU-reference workload protection.
- Do not add CUDA, LBVH, a public spatial-index abstraction, or another queue.
- Do not exaggerate/fake method displacement or silently change the selected
  LOP-family equations.
- Do not change mesh topology/cardinality or publish a separately named
  property as though it were rendered canonical geometry.
- Do not claim Vulkan speedup, quality superiority, or cross-device coverage.

## Context
- Symptom: with `child.obj` selected and support-radius mode `Auto`, the
  Sandbox reports `UnsafeSupportRadius`; no job applies and the mesh has no
  visible response.
- Expected behavior: Auto resolves a finite local radius; a safe requested
  Vulkan plan runs and reports its actual backend/fallback state; publishing to
  canonical `v:position` marks the existing render residency dirty and changes
  the rendered position fingerprint.
- Impact: the newly delivered Auto/Vulkan path is technically present but its
  default large-mesh UI workflow appears inert to the researcher.
- Ranked diagnosis: the rank-16 default exceeds the current `100,000,000`
  contribution budget on this asset even though smaller policy-valid ranks are
  safe; Vulkan plan construction also treated a hypothetical
  `query_count * max_neighbors` diagnostic-counter sum as a hard execution
  limit. Output binding is already canonical by default; stale-build and
  Vulkan availability remain secondary checks.
- Real-device diagnosis: workload backoff can produce projected particles with
  no other particle inside compact support. Huang et al. define both WLOP
  densities as `1 + sum`, so their density remains exactly one; the current CPU
  and Vulkan paths instead reject that valid base case as `EmptyNeighborhood`.
- Owners/layers: geometry retains the backend-independent radius/occupancy
  analysis; runtime owns backend-aware admission, execution, publication, and
  telemetry; app owns presentation only.

## Control surfaces
- Config: keep the schema-versioned `sandbox.point_cloud_consolidation`
  Auto/Manual and explicit safety-limit fields round-trippable.
- UI: show the precise analysis/admission reason, actual execution phase, and
  applied displacement without a private bypass.
- Agent/CLI: consume the same validated config and runtime result fields as UI.

## Backends
- Backend axis: both backends retain the shared conservative work guard and
  consume one explicit resolved radius. Vulkan additionally validates its
  concrete bounded cell-grid/resource plan and remains fail-closed when the
  device or plan is unavailable; saturating diagnostics are not plan limits.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | A finite `vec3` position property/span; no mesh provenance is required by the method. |
| Compatible entity sources | Preserve every compatible vertex, edge, halfedge, and face property on mesh, graph, and point-cloud sources. |
| `RuntimeModule` | Repair admission and progress/result telemetry in the existing `PointCloudConsolidationModule`/private Vulkan participant only. |
| Config/agent | Preserve the shared validated config path; any clarified limit semantics round-trip through the existing section. |
| UI | Project runtime submission/progress/result truth and default canonical position binding; no app-owned execution path. |
| Publication | Same-cardinality output updates only the selected originating property; canonical `v:position` uses the existing dirty-upload path. |
| End-to-end tests | Reproduce 50k-scale Auto admission and prove actual Vulkan completion plus canonical position/render-residency change. |

## Required changes
- [x] Add a deterministic 50k-scale `child.obj`-equivalent feedback loop that
      records the exact Auto analysis status, radius, occupancy, predicted CPU
      work, and Vulkan plan disposition.
- [x] Make Auto choose and report the largest policy-valid neighbor rank that
      satisfies the unchanged work budget, and stop rejecting a concrete safe
      Vulkan plan solely because hypothetical diagnostic-counter totals exceed
      32 bits.
- [x] Restore the paper-defined WLOP singleton density value of one in the CPU
      reference, optimized CPU, and Vulkan paths; retain fail-closed behavior
      for an actually empty attraction denominator.
- [x] Preserve fail-closed CPU limits and actionable Vulkan plan/device
      rejection; do not turn configured limits into ignored UI fields.
- [x] Make Sandbox queued and terminal diagnostics name the analysis/backend/
      publication pipeline, Auto backoff, applied output, and rejection.
- [x] Prove canonical position publication enters the existing dirty-upload
      path and produces a non-zero before/after position/render fingerprint.

## Tests
- [x] Add a failing regression for the reported default Auto large-mesh
      request before applying the fix.
- [x] Cover CPU-reference over-budget rejection and Vulkan concrete-plan
      admission without changing property-domain/cardinality contracts.
- [x] Cover isolated direct/reciprocal density weights and CPU/Vulkan parity so
      a zero non-self density contribution count is not reported as failure.
- [x] Add or extend an opt-in `gpu;vulkan` integration/readback proof that
      records actual Vulkan execution, zero fallback, applied displacement,
      and changed visible residency.
- [x] Run focused config, panel, runtime, support-radius, and Vulkan selectors.

## Docs
- [x] Update support-radius/runtime/UI current-state docs with backend-aware
      admission and actionable diagnostics.
- [x] Update task indexes/session brief and append the retirement narrative on
      closure; refresh generated artifacts required by touched surfaces.

## Acceptance criteria
- [x] The reported Auto workflow no longer ends at `UnsafeSupportRadius` when
      the resolved radius and concrete Vulkan plan are safe.
- [x] Unsafe shared work still fails before execution with sampled-neighbor or
      predicted-contribution diagnostics, and an unsafe Vulkan plan remains
      fail-closed with its bounded grid/resource category named.
- [x] A successful default canonical-position run reports actual Vulkan,
      zero fallback, non-zero displacement, applied publication, and a changed
      render-residency fingerprint.
- [x] UI, config, and agent/CLI use one validated execution/result path.
- [x] No layering, property-domain, topology/cardinality, or method-parity
      regression is introduced.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'SupportRadius|PointCloudConsolidation|SandboxPointCloudConsolidationPanel' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci-vulkan --target IntrinsicRuntimePointCloudConsolidationGpuParityTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'PointCloudConsolidation.*(Auto|Visible|Publication)' -L gpu -L vulkan --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Shipping a higher global contribution default as the only fix.
- Bypassing analysis or configured limits whenever Vulkan is requested.
- Treating queued/submitted as applied or claiming visibility from CPU-only
  contracts.
- Publishing non-canonical output as if the mesh renderer consumed it.
- Mixing unrelated LOP variants, importer changes, or renderer refactors into
  this bug fix.
