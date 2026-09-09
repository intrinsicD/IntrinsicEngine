---
id: RUNTIME-209
theme: I
depends_on: [HARDEN-087]
workflow_schema: 1
template: micro
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive implementation with reviewed diff, CPU/Vulkan tests and bounded method evidence."
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-209 — Point-set outlier analysis and publication split

## Goal

- Separate statistical/radius outlier detection from destructive removal so
  every caller-selected finite `vec3` property on a resolved element domain
  can publish a same-cardinality outlier mask while explicit cardinality
  reduction remains restricted to topology-free point clouds.

## Non-goals

- No new outlier estimator, topology-aware mesh/graph deletion, conversion, or
  general geometry filtering framework.
- No ImGui implementation; `UI-041` owns the three-domain panel behavior.

## Context

- The current command rejects mesh/graph provenance and immediately compacts a
  point cloud. Its statistical and radius kernels first compute rejected input
  indices, which are valid analysis results for any typed sample property,
  including mesh face centers.
- Review Rusu et al.'s statistical neighborhood formulation (DOI
  `10.1016/j.robot.2008.08.005`) and the official PCL behavior references for
  StatisticalOutlierRemoval
  (`https://pointclouds.org/documentation/classpcl_1_1_statistical_outlier_removal.html`)
  and RadiusOutlierRemoval
  (`https://pointclouds.org/documentation/classpcl_1_1_radius_outlier_removal.html`)
  before editing. Search density-aware successors during the task's literature
  intake and record stable identities plus selection/exclusion rationale, but
  do not silently change the two existing estimators. Preserve detection vs
  deletion as an explicit engine distinction.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | A caller-selected finite `Property<vec3>` plus neighborhood parameters. |
| Compatible entity sources | Every resolved mesh/graph/point-cloud element domain for detection; point-cloud points only for destructive compaction. |
| RuntimeModule | Extend the existing geometry-processing outlier command/job path with explicit Analyze and Remove modes. |
| Config/agent | Add one validated point-set-outlier config used by UI, agents, and direct commands. |
| UI | `UI-041` provides Detect on all domains and explicit Remove only for point clouds. |
| Publication | Analyze writes named same-domain mask and score/diagnostic properties; Remove compacts only point-cloud `Vertices` through history. |
| End-to-end tests | All property-domain families including face centers, point-cloud-only removal, config parity, async staleness, and UI readiness tests. |

## Spatial acceleration consideration

Radius detection can reuse the canonical position-property cache with exact self
filtering and complete-hit/count handling. Statistical detection needs kNN and
self exclusion, now available in the shared point LBVH (GEOM-077; GPU k=1..64).
RUNTIME-219 adds framed radius batches with complete total counts and a GPU-to-CPU
job dependency pattern. Reuse that query transport while preserving outlier
self-exclusion, rather than adopting the normal estimator's extra-candidate rule.
Consumer adapter integration and parity remain part of this task. Preserve current
estimators and the Analyze/Remove split; record deferred acceleration rather than
silently substituting a radius or truncated neighborhood.

See the [shared spatial-index consumer inventory](../../docs/architecture/spatial-index-consumers.md).

## Required changes

- [x] Introduce an explicit operation mode and property-aware capability
      record; Analyze resolves a canonical `GeometryPropertyRef` on any element
      domain, while Remove additionally requires topology-free point-cloud
      provenance and canonical point properties.
- [x] Publish deterministic same-cardinality mask and available score data to
      the originating property domain without changing topology, element
      order, or unrelated properties.
- [x] Preserve point-cloud removal as a separate undoable mutation consuming
      the analysis result/current generation; fail closed for graph/mesh Remove
      with an actionable shared diagnostic.
- [x] Add one serializable preview/validate/apply config section for method and
      parameters, reused by direct, agent, and UI requests.
- [x] Keep queued completion generation-bound and use existing geometry
      operation/history abstractions rather than a new service.

## Tests

- [x] Parameterize statistical and radius Analyze over every physical
      property-domain family, including mesh face centers, and compare
      masks/diagnostics for identical values.
- [x] Verify Analyze preserves all topology/cardinality and undo/redo restores
      target properties exactly.
- [x] Verify Remove succeeds and round-trips only for point clouds; graph/mesh
      fail before queueing with no dirty/history mutation.
- [x] Add config round-trip, control-surface parity, and stale async completion
      coverage.

## Docs

- [x] Update runtime/config and method notes with detection/removal semantics,
      literature references, property names, and domain matrix.

## Acceptance criteria

- [x] Outlier detection is available for every valid typed sample property.
- [x] Destructive removal is explicit and cannot damage graph/mesh topology.
- [x] All callers share the same config, readiness, diagnostics, and history
      behavior.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'Outlier' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No mesh/graph compaction, hidden conversion, combined detect-and-delete
  default, UI-only mode selection, or silent property loss.

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
