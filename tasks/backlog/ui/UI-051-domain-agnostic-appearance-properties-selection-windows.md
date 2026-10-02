---
id: UI-051
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, and CI
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - geometry.element-domain-sources
---
# UI-051 — A mesh does not pass as a graph or a point cloud in the domain windows

## Goal
- Make the PointCloud and Graph Appearance, Properties, and Selection-details
  windows accept any entity that carries the element-domain data they need, so a
  mesh selection is usable in them.

## Non-goals
- No change to the method panels already owned by `RUNTIME-211`/`UI-043`
  (K-Means), `RUNTIME-212`/`UI-044` (Progressive Poisson),
  `RUNTIME-213`/`UI-045` (point normals), or `RUNTIME-209`/`UI-041`
  (point-set outlier removal). This task covers the remaining domain windows.
- No change to `DetectDomain` itself; `ActiveDomain` remains the entity's
  exact provenance.
- No new conversion command that materializes a point cloud or graph entity from
  a mesh.

## Context
- Symptom: with `sculpt.obj` (a mesh) selected, `PointCloud / Appearance`
  reports:
  > Expected domain: PointCloud · Selected: sculpt.obj (2) ·
  > Selected domain: Mesh ·
  > `UnsupportedGeometryDomain: PointCloud window requires PointCloud-domain
  > selection; selected domain is Mesh.`
  and its render controls are disabled.
- Mechanism: `ECS.Component.GeometrySources.cpp:DetectDomain()` assigns exactly
  one `Domain` per entity (Mesh iff vertices+edges+halfedges+faces; Graph iff
  graph-marked; PointCloud iff vertices only).
  `Runtime.EditorWorkspaceSnapshots.Models.cpp` then computes
  `DomainMatches = SelectedDomain == ExpectedDomain`, and the panels refuse to
  draw controls when it is false
  (`Sandbox.PanelSupport.cpp` and `Sandbox.MeshProcessingPanels.cpp`,
  `Sandbox.DomainPanels.cpp` via `DomainAppearanceReady`).
- This is a provenance test standing in for a capability test. A mesh's vertices
  *are* a point set and its edges *are* a graph, so the data the windows need is
  present.
- The right model already exists in-tree:
  `Consolidate (LOP/WLOP/CLOP/EAR)` is deliberately built as a *semantic
  point-set* window over the property catalog and accepts mesh `v:position`
  (see `src/app/Sandbox/README.md`), and `Mesh / Processing / K-Means` offers a
  `Mesh Vertices` domain. This task extends that shape to the remaining windows.
- Windows in scope (all currently unreachable for a mesh selection):
  `PointCloud / Appearance`, `PointCloud / Properties`,
  `PointCloud / Selection details`, `Graph / Appearance`,
  `Graph / Properties`, `Graph / Selection details`.
- Related but distinct: the disabled render-hint controls give no hover reason,
  which is `UI-037`'s scope; do not duplicate it here.
- Impact: the engine presents itself as a mesh/graph/point-cloud tool, but most
  of the point-cloud and graph surface is inert on the most common asset kind.
- Owner: `runtime` owns the domain-window model and eligibility; `app` owns
  presentation.
- Follow-up (operator feedback 2026-10-02): `UI-075` replaces the three Appearance
  windows with one panel that has per-attribute source selectors, on top of this
  task's capability gating; `RUNTIME-315` owns the attribute-binding model. This
  task keeps the capability test and element-domain statement.

## Control surfaces
- Config: none.
- UI: the affected windows accept compatible selections and state which
  element domain they are reading.
- Agent/CLI: unchanged.

## Spatial acceleration consideration

For future spatial brush/radius selection over displayed point-valued properties,
consider the shared canonical-property cache and preserve original domain IDs,
stale checks and complete radius results. Nearest segment/face or screen-space
lasso/visibility queries need their own exact query semantics. This note does not
add brush/lasso implementation to the current property/selection-window scope.

See the [shared spatial-index consumer inventory](../../../docs/architecture/spatial-index-consumers.md).

## Required changes
- [ ] Replace the exact-provenance `DomainMatches` gate in these windows with a
      capability test over the selected entity's element-domain availability and
      property catalog.
- [ ] Make each window state which element domain it is presenting (e.g.
      "reading MeshVertices as a point set") so provenance stays visible.
- [ ] Keep publication on the originating element domain — no alias entity and
      no implicit conversion.
- [ ] Keep a truthful unsupported path for entities that genuinely lack the
      required domain, with the runtime-owned reason.

## Tests
- [ ] Add runtime contract tests asserting a mesh selection produces a usable
      model for the PointCloud and Graph Appearance/Properties/Selection
      windows.
- [ ] Add a test asserting the presented element domain is reported.
- [ ] Add a test asserting an entity genuinely lacking the domain still reports
      the unsupported reason.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update the domain-window prose in `src/app/Sandbox/README.md`.
- [ ] Cross-reference `docs/architecture/geometry-api-style.md` on
      element-domain eligibility versus provenance.

## Acceptance criteria
- [ ] With a mesh selected, the PointCloud and Graph Appearance, Properties, and
      Selection-details windows are usable.
- [ ] Each window states the element domain it reads.
- [ ] No conversion entity or property alias is created.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'GeometryAvailability|EditorWorkspaceSnapshots|SandboxEditor' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Using entity provenance (or a container's presence) as the eligibility boundary;
  eligibility is the element-domain data the window reads (Vertices for a point
  set, Vertices and Edges for a graph).
- Duplicating the method-panel work owned by `RUNTIME-211/212/213` and
  `UI-041/043/044/045`.

## Log
- 2026-10-02 slice 1 — scope decision. Properties and Selection-details are
  gated in full by `Runtime::ResolveGeometryDomainReading` (runtime owner,
  `geometry.element-domain-sources`): PointCloud reads any Vertices, Graph reads
  Vertices plus Edges, Mesh stays exact (faces are semantic).
  `EditorDomainWindowModel::DomainUsable`/`DomainReading` carry the result and
  the window header states "Reading: Mesh Vertices as a point set"; a point
  cloud in a Graph window keeps the runtime `UnsupportedGeometryDomain` reason.
  `DomainMatches` remains exact provenance for the Mesh/processing consumers.
  Appearance: only the shared predicate lands. `UI-075` replaces the per-domain
  Appearance windows with one panel and reuses the predicate to choose domain
  sections, so no per-domain Appearance UI is built here; the Appearance
  acceptance items stay open for `UI-075`.
- Review follow-up: the Appearance parts of AC1/AC2 transfer to `UI-075`
  (Appearance already gates on `VisualizationTargetAvailable`; UI-075 AC1 covers
  "a mesh read as points/edges"). The Selection element-domain list is limited
  to the reading's elements; Properties stays exhaustive.
