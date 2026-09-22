---
id: RUNTIME-270
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive implementation; source diff, tests, review and checkpoints retain evidence without unattended custody.
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, method.engine-integration, runtime.processing-compilation-locality, repo.source-documentation]
---
# RUNTIME-270 — Property binding equivalence across methods

## Goal

Make all suitable properties interchangeable by numeric shape, independently of
name, prefix or producing method, across runtime, config/agent and UI bindings.
Expose supplied feature properties to segmentation instead of always recomputing
curvature. The operator explicitly broadened the previous UI-037 continuation
on 2026-09-20; this task owns the broader change, while UI-037 retains readiness.

## Acceptance criteria

- [x] Generic GMM segmentation accepts explicit mesh vertex/face numeric features;
      selected features are used, preserved, and never replaced with curvature.
- [x] One Vec2 and two scalar channels with equal values produce equal results;
      numeric storage conversion preserves representable values and rejects
      precision loss at conversion/non-finite samples. Subsequent kernel arithmetic
      and vertex averaging use floating-point arithmetic, not exact arithmetic.
      Unsupported dimensions fail explicitly.
- [x] Config file round-trip, agent apply, runtime preflight and UI selection use
      the same feature bindings and compatibility predicate. Computed curvature
      remains an explicit input choice; intrinsic feature curves retain their
      actual curvature requirements.
- [x] Remove arbitrary input name/prefix gates from curvature, segmentation,
      parameterization and geodesics; retain topology/correspondence, output
      aliasing and structural storage requirements. Regression tests use names
      unrelated to position, curvature or domain prefixes.
- [ ] Audit every method/config/UI binder against a family matrix, including
      point-set, graph-neighborhood, mesh-correspondence and owning topology edits.
      Sampled clean paths are not an exhaustive engine-wide compliance claim.
- [ ] Replace source-name-derived normal/color interpretation in visualization,
      surface appearance and texture baking with explicit recipe/config choices.
- [ ] Extend checked numeric adapters across remaining input and output bindings.
      A compatible output shape may require checked conversion into the declared
      target storage; never reinterpret existing storage, silently narrow values,
      or weaken output aliasing/ownership rules. Conversion failure preserves the entire publication cohort.
- [ ] Migrate geodesics string slots to canonical property refs. Make
      parameterization corner-UV binding/retirement an explicit optional ref;
      preserve unrelated corner UVs and restore retired values through history.
- [ ] Decide supported feature widths beyond the existing GMM's 1–3 channels;
      Vec4 is rejected until an implementation supports it, never truncated.
- [ ] Close stale-source, invalidation, no-per-frame-scan and prepared-frame
      readiness coverage with UI-037, including newly bound feature fields.
- [x] Full relevant tests, layering/docs checks and independent fixed-diff review
      pass; record remaining limitations without claiming universal completion.

- [x] Close the existing mesh-field/fixture compilation follow-up with matched
      clean and target-incremental measurements, explicit shared-owner costs,
      fixed-diff review and separate integration commits (C103/C104). This is a
      bounded development-cost result, not completion of the product matrix.

## Engine integration

| Surface | Required behavior / owner |
| --- | --- |
| Least-structured input | GMM consumes numeric face features; face-dual regularization genuinely needs mesh faces/adjacency. |
| Compatible entity sources | Vertex properties aggregate to their incident faces; face properties retain face correspondence. No property-name eligibility. |
| RuntimeModule | Existing mesh-field operations bind current properties and publish through existing history; no new service. |
| Config/agent | Serializable full property refs plus explicit computed-input selection; RUNTIME-270. |
| UI | Canonical catalog filtered by the same runtime feature predicate; RUNTIME-270. |
| Publication | Face/edge output properties preserve topology and unrelated input properties; existing history owner. |
| End-to-end tests | Type/name substitution, config round-trip, command outputs/history, invalid inputs and UI command routing; RUNTIME-270. |
| Readiness | Metadata now, deferred finite-input readiness where needed; UI-037 owns complete nonblocking closure. |

## Discovery and decisions

Reuse the existing CPU GMM/face-dual solver, curvature adapter, canonical
`GeometryPropertyRef`, property catalog and resolver. No new method/backend or
compatibility aliases. The numerical kernel never receives property names.
The source-label audit found unnecessary prefix gates in curvature,
parameterization and geodesics, and name-derived normal interpretation in
visualization/baking. Point-input catalogs and sampled executors already use
full property refs; mesh-correspondence and topology-changing domain restrictions
are legitimate and must stay. Geodesics now uses full canonical refs; parameterization retires only its
explicit optional corner ref. Subdivision copies selected scalar edge markers by
endpoint correspondence into its Boolean kernel field, then republishes the
declared storage after its owning topology mutation.

The operator was asked whether numeric storage kinds should interoperate.
Pending a different preference, use checked numeric conversion as the stated
implementation assumption; exact storage identity remains part of property
resolution and output mutation, not a substitute for feature shape.

## Retirement implementation plan — 2026-09-22

The operator explicitly authorized completion through retirement, including actual
blockers, Claude review and isolated subagents. UI-037's unrelated ICP, bake and
service workflows are not dependencies of this task; this task closes its bound
mesh-feature readiness overlap using that task's existing cache lifecycle.

The source audit covers canonical reference owners, processing operations, codecs
and both Sandbox method-panel owners. This is the implementation inventory, not
an execution-completeness claim:

| Binding family | Existing owners and remaining closure |
| --- | --- |
| Point normals, bilateral, registration, construction | PointProperties capture; Vec3 is the sole catalogued three-component storage. Preserve correspondence and selected domains. |
| Density, spacing, weights, outliers, keypoints, descriptors | Existing point publication/history; extend scalar storage conversion before widening config/UI. |
| K-Means | ClusteringTypes/ClusteringModule; remove provenance-only execution domain restrictions and retain deleted-slot correspondence; checked scalar outputs. |
| LOP/WLOP/CLOP/EAR | Existing canonical Vec3 inputs and same-domain publication; preserve cardinality/normal policy. |
| Progressive Poisson | Four scalar outputs and owning permutation; checked publication must retain reordering/structural invariants. |
| Graph normals | Real adjacency requirements remain; structural connectivity types are not arbitrary scalar slots. ShortestPath/VectorHeat/ConvexHull have menu metadata but no executable runtime binders. |
| Curvature and segmentation | Existing mesh-field capture/history; checked scalar targets, alias protection and cached bound-feature readiness. |
| Geodesics | Full canonical config refs; checked distance/mask publication, including unreachable-distance infinity semantics. |
| Parameterization | Explicit optional corner-UV retirement ref, preserving unrelated corners and exact undo. |
| Owning topology edits | Denoise/smoothing/remesh/simplify/subdivide/repair/CSG/reconstruction/construction retain explicit owning mutations and dependent-data handling. |
| Visualization, surface appearance and bake | Explicit normal interpretation, name-independent catalogs and consistent scalar storage admission. |

GMM deliberately supports one to three numeric channels. Wider feature vectors,
including Vec4, remain explicitly rejected without truncation; extending the
numerical algorithm is unnecessary to establish truthful binding equivalence.

Reuse/right-sizing: retain the existing GeometryAvailability owner for exact
scalar snapshots and checked conversion, then reuse it across point and mesh
publication. One plain snapshot plus compiled functions serves current callers;
no service, registry or forwarding layer. Readiness extends the existing
session-owned verdict cache. Appearance adds one persisted interpretation field
and a shared bake-encoding mapping; cache identity includes interpretation.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'CurvatureSegmentation|MeshCurvature|Geodesic|Parameterization|GeometryProperty|ProcessingCompilationLocality' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
```

## Implementation scope and limits

Generic GMM uses `SegmentFaceFeatures` with one to three ordered numeric channels,
including Vec2 or paired scalars. Vertex features average onto incident faces;
face features preserve correspondence. Empty feature lists explicitly select
computed curvature. Unsupported Vec4 input fails without truncation. Curvature
methods retain their real curvature requirements.

Scalar publication uses checked conversions for all six scalar storage kinds.
Representability is a value-dependent runtime condition: for example a general
Double geodesic distance need not fit Float exactly. Such failure rejects the
whole cohort without partial output or history. Vector catalog storage remains
float Vec2/Vec3/Vec4, so equal vector shape already has one canonical storage.
Existing skipped scalar slots preserve exact storage, including NaNs and signed zero;
new skipped scalar slots initialize to zero. Curvature direction vectors retain
the existing finite, full-slot publication contract. Structural topology storage and explicitly
owning topology edits remain outside same-cardinality publication rules.

All executable point/graph/mesh binder families in the matrix were inspected
across metadata, codecs where present, commands, publication and panel owners.
ShortestPath/VectorHeat/ConvexHull are catalog metadata with no runtime executors; this task
adds no missing algorithms. Subdivision retains its existing typed command/UI
surface; no persisted subdivision section existed to migrate. GMM width limits,
mesh correspondence and structural-property ownership are deliberate constraints.

Display projection is distinct from output mutation: shader data uses float,
rejects out-of-range values and inexact integers, while finite Double display
values may round. Source storage is untouched. All scalar kinds have the same
raw linear bake default; label palettes and normal directions are explicit
interpretations. Interpretation participates in buffer identity. Consolidation
never chooses a normal input merely because its name contains “normal”.

The UI-037 overlap uses its existing verdict cache for selected mesh features,
with mutation invalidation, prepared-frame reuse and detach/world guards. The
remaining UI-037 service inventory is independent of this task. The 512-property
picker reuses selected-analysis metadata and catalog data without numeric scans;
it constructs one lightweight domain wrapper per frame. Existing metadata copies
and ImGui row enumeration remain linear in catalog size under UI-037. No new
performance, solver-parity or backend-maturity claim is introduced.

## Review and verification log — 2026-09-22

- Claude reviewed the plan and fixed source diffs. Findings fixed: explicit-recipe
  normal buffer identity, negative interpretation tests, accidental scalar color
  emission from vector recipes, range checking before floating conversion, and
  fallback key collision. Exact Float geodesic rejection is intentional under the
  checked-publication contract and has a nontrivial rejection regression.
- Isolated agents reviewed binders and implemented point, mesh, geodesic and
  subdivision slices. Cross-review identified skipped-slot initialization and
  stale point-family capability masks; combined verification follows those fixes.
- An initial sanitizer compilation overlapped a source rewrite and failed while
  lexing TextureBake. The build was interrupted; frozen-source rebuilds with
  ccache disabled supersede that invalid run. No source workaround was added.

## Closed development-cost batch — 2026-09-21

The operator explicitly prioritized duplicate-code and compilation-time closure
for this session. Close the previously attached batch here; future compilation
work must identify its own acceptance criterion and owner rather than extending
this property-binding task by default.

Existing `EditorFeatureTestContext.cpp` owns context/presentation, graph and
icosahedron fixture construction. Consumers reuse it; `MockRHI.cpp` owns device
construction/destruction. No new abstraction or translation unit. The ten-file
batch removes 262 net lines, preserving specialized fixture differences. Source
commit: `e804afc24`. C103 closes the earlier four-commit measurement obligation;
C104 closes the accumulated fixture batch. The exact ten-file tree matches local
snapshot `887415fb7e6df4c04caa154f06f64f04a422b731`.

See the [recent-locality report](../../ara/evidence/tables/runtime270_recent_locality_measurement.md)
and [fixture-batch report](../../ara/evidence/tables/runtime270_fixture_batch_measurement.md)
for raw results, source identities and all regressions. C104 runtime-contract target
medians: clean 428.676 → 426.548 s (negligible); Models edit 23.748 → 17.723 s;
Visualization edit 21.253 → 16.787 s; fixture header 67.902 → 58.503 s; mock header
53.364 → 35.166 s (11 → 8 compilers). Fixture implementation regresses
8.570 → 10.112 s. Clang 23, ci Debug Null/headless, four jobs, cache disabled,
ABBA n=2 per arm; scanning/linking and owner compilation included. Other executable
and full-engine costs remain unmeasured, not mandatory follow-up experiments.

SessionLifecycle dependency cleanup is separately committed as `8affd8dcc`.
Its code matches the previously reviewed after snapshot; no clean-build or
end-to-end incremental speedup is claimed. The direct compiler diagnostics excluded
scanning/linking and cannot extend C104. A future performance claim needs its own
matched target baseline. No remaining implementation or integration obligation for
these completed source changes.

BUG-204's consumer-probe fix is committed as `defaafeac` and retired at
[BUG-204](../done/BUG-204-compile-benchmark-header-probes.md). The unchanged predicate
is extracted for negative-path tests; retained measurement runners keep their
original byte identities. No accepted experiment was rerun.

Closure verification: canonical ci configure and IntrinsicTests build; 459 focused
CPU tests; 4,861 passed, zero failed and one expected GLFW/LSan
capability skip out of 4,862 selected (156.62 s). All 29 compile-tooling tests, 103 manifests and eight
retained canonical results pass. All 41 indexed evidence files match their hashes.
Strict layering, test layout, task policy, docs sync, claims and root hygiene pass;
relative links and session brief pass. Header documentation audit: zero errors;
its one existing comment explains the test-only live-dependency boundary.
Claude Fable 5.1 performed independent plan/fixed-diff review; the missing negative
probe tests were fixed. No sanitizer/GPU run or new performance claim this session.

Historical implementation notes are preserved in `e804afc24`; this current
summary replaces repeated chronological checklists without dropping open scope.

Independent review endpoint: Claude reported no blockers for the frozen combined
diff through `40cfd093c`; a separate agent cross-reviewed subdivision through
`ec3a0efba` with no blockers. Cross-reviews also covered scalar publication and
the later skipped-face/capability corrections. Reviews did not substitute for
execution gates below.
