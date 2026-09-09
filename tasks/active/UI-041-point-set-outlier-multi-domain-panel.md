---
id: UI-041
theme: I
depends_on: [RUNTIME-209]
workflow_schema: 1
template: micro
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive implementation with reviewed diff, CPU/Vulkan tests and bounded method evidence."
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# UI-041 — Point-set outlier multi-domain panel

## Goal

- Replace the PointCloud-only “Remove Outliers” control with a shared
  mesh/graph/point-cloud analysis panel: Detect publishes an outlier mask for
  any selected typed element-domain property, while explicit Remove is offered
  only for point clouds.

## Non-goals

- No estimator/runtime/config implementation or topology-aware mesh/graph
  deletion.
- No automatic deletion after detection and no geometry conversion.

## Context

- `RUNTIME-209` separates method analysis from ownership-changing publication.
  UI language must preserve the same distinction and the limitations found in
  its SOR/ROR literature review.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | A selected finite `vec3` property. |
| Compatible entity sources | Every mesh/graph/point-cloud element domain for Detect; point-cloud points only for Remove. |
| RuntimeModule | Consume `RUNTIME-209` config/readiness/analyze/remove/result paths. |
| Config/agent | Edit and apply the shared validated outlier config. |
| UI | Register the analysis panel under all three Processing domains with canonical property selectors and capability-specific actions. |
| Publication | Visualize same-domain, same-count mask/score; label point-cloud compaction as a separate destructive history command. |
| End-to-end tests | Three-domain discovery/detection, visualization, point-cloud removal, disabled reasons, and undo/redo. |

## Spatial acceleration consideration

Keep acceleration ownership in RUNTIME-209. The implemented CPU/Vulkan choices and
unsupported diagnostics use the same validated runtime request. Statistical kNN
uses source exclusion; radius consumes complete counts. The panel owns no index
or independent backend implementation.

See the [shared spatial-index consumer inventory](../../docs/architecture/spatial-index-consumers.md).

## Required changes

- [x] Register one feature panel under Mesh, Graph, and PointCloud Processing
      with shared method/parameter state and input/output property selectors.
- [x] Rename the primary action to Detect Outliers and display mask/score and
      diagnostics through existing property visualization controls.
- [x] Show Remove Marked Points only when runtime readiness allows
      topology-free point-cloud compaction; require a separate explicit action
      and show its count/history consequences.
- [x] Use runtime config, readiness, submit, and result records exclusively.

## Tests

- [x] Assert all three menu registrations and Detect routing over each physical
      property-domain family, including mesh face centers, with shared config.
- [x] Cover mask visualization, missing/invalid source diagnostics, explicit
      point-cloud Remove, and absence/disabled state for mesh/graph Remove.
- [x] Verify no detection action mutates cardinality and undo/redo remains
      runtime-owned.

## Docs

- [x] Update Sandbox outlier workflow docs and menu inventory.

## Acceptance criteria

- [x] Every compatible typed sample property can discover and run outlier
      detection.
- [x] Only point clouds expose explicit removal; mesh/graph topology is safe.
- [x] UI and agent callers share config, readiness, and operations.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'Outlier' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No detect-and-delete default, mesh/graph compaction, converter, UI-owned
  property publication, or independent per-domain configs.

## Interactive implementation decisions (2026-09-09)
- User accepted outlier analysis as the next GPU LBVH consumer after RUNTIME-219. Implement statistical/radius detection with CPU octree reference, explicit cached CPU LBVH and Vulkan LBVH queries. CPU owns score/classification reductions; retain the existing population-variance threshold and inclusive radius rule.
- GPU kNN excludes the source identity (k<=64). Coincident distinct samples remain eligible. Radius detection consumes exact total hit counts with source exclusion and capacity 1, so dense neighborhoods do not require complete stored hits.
- Detect publishes named uint32 0/1 mask and float score properties on all eight domains, preserving deleted rows and unrelated data. Remove Marked is a separate operation only for point-cloud points; use property-set copy/swap/resize so every property is retained in source order. Removal requires a current analysis provenance stamp and participates in history.
- Config/agent and one shared Outlier Analysis window use the same preview/apply/execute path. Existing low-level removal APIs retain compatibility; old destructive menu entry routes to the analysis window. LOF-like probability, density estimation and other GEOM-073 utilities remain separate.
- Literature: Rusu et al. 2008 DOI 10.1016/j.robot.2008.08.005 and official PCL SOR/ROR sources; LOF (SIGMOD 2000) and LoOP (CIKM 2009) are density-aware alternative estimators, excluded from this query-execution/analysis split.

## Implementation and verification (2026-09-09)

Implemented and verified locally; pending publication. Bounded result: C81 in
[the claim ledger](../../ara/logic/claims.md), with commands/source identity,
CPU/native and actual Vulkan evidence in the
[verification record](../../ara/evidence/tables/outlier_vulkan_verification_2026-09-09.md).

- Clang 23 `ci`: built `IntrinsicTests` and `ExtrinsicSandbox`; 4,389 distinct CPU
  passes across the full selector and five native-window follow-ups. The one
  remaining LSan-control skip is expected in the unsanitized preset.
- `ci-vulkan`: built `IntrinsicPointLBVHGpuTests`; the outlier case passed on
  RTX 3050 with ASan+UBSan, preserving the cohort's existing leak setting.
- Statistical/radius masks match exactly on the GPU fixture; scores use a 1e-5
  absolute tolerance (observed zero). Dense radius counts, self exclusion,
  deleted rows, stale/cancelled jobs, config/UI/recipe routing, custom property
  preservation and guarded removal/history are covered.
- Schema-v2 smoke, layering, task policy, test layout, method/benchmark manifests
  and doc links pass. No speedup, full Framework24/LOF parity, pixel-readback or
  whole-process leak-freedom claim. CPU classification and the CPU default remain.

Current workflow and limitations: [outlier analysis](../../docs/architecture/outlier-analysis.md).
The separate LOF-like probability, density and other point-analysis adapters remain
under GEOM-073 and the spatial consumer inventory. The old combined-removal APIs
retain CPU compatibility behavior; new UI/config callers use Detect then explicit Remove.
