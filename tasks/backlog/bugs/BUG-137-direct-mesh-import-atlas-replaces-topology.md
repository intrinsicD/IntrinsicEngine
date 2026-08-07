---
id: BUG-137
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - geometry.element-domain-sources
  - geometry.property-coherence
---
# BUG-137 — Direct mesh import replaces halfedge topology with the UV-atlas chart-split mesh

## Goal
- Make direct mesh import preserve the source mesh's vertex/edge/halfedge
  topology, so a closed manifold OBJ materializes as a closed manifold ECS
  geometry entity while still resolving `v:texcoord`.

## Non-goals
- No new UV unwrapping method, chart packer, or atlas backend.
- No change to the default-off progressive model-scene UV job (`BUG-097` owns
  that path).
- No change to authored-UV preservation policy when the source file already
  carries valid `v:texcoord`.
- No texture-bake, material, or GPU-residency redesign.

## Context
- Symptom: importing `tests/data/sculpt.obj` (3669 vertices, 7342 triangles,
  11013 edges, every edge exactly 2-manifold, consistent orientation, closed
  genus-2, Euler χ = −2) materializes an ECS entity reporting **21464
  MeshVertices, 21745 MeshEdges, 43490 MeshHalfedges, 7342 MeshFaces**, with
  **every one of the 21464 vertices on a boundary**. Face count is the only
  quantity preserved.
- Mechanism: `Runtime.AssetWorkflowGeometryMaterialization.cpp:610-616` builds
  the entity halfedge mesh from `atlas.OutputMesh` — the UV atlas *output* mesh
  — rather than from the source mesh with UVs attached.
  `Geometry.UvAtlas.cpp:1016-1045` emits a fresh output vertex per
  `(chart, source-vertex)` pair, because `sourceVertexToOutput` is reallocated
  per chart; vertices are therefore shared only within a chart. The default
  backend is `UvAtlasMethod::FastStaged`
  (`Runtime.AssetWorkflowGeometryMaterialization.cppm:33-34`), whose chart
  decomposition produced ~5.9 output vertices per source vertex here.
- The split is already computed and named: `diagnostics.SeamSplitVertexCount`
  (= 17795 for this asset) is populated at
  `Runtime.AssetWorkflowGeometryMaterialization.cpp:623-627` and never surfaced
  in any UI or log.
- Impact: this is upstream of most geometry-processing failures observed in the
  Sandbox. Every connectivity-dependent operation runs on a triangle soup —
  mesh denoise pins all 21464 vertices as boundary and moves 0 (`BUG-140`),
  LSCM parameterization rejects the mesh as invalid input, per-vertex curvature
  is per-triangle speckle, and the UV layout view renders thousands of
  disconnected micro-charts.
- Owner: `runtime` asset-workflow geometry materialization composes the step;
  `geometry` owns the atlas method and the halfedge conversion. A UV seam is a
  per-corner/UV-layer concern and must not rewrite the connectivity that the
  element-domain contract exposes to methods.

### Decision — per-corner UVs, de-indexed at GPU upload (decided 2026-08-07)

The underlying conflict is that the GPU path is an *indexed* mesh with
per-vertex attribute arrays — `Runtime.GeometryPlanBuilders.Mesh.cpp:183-195`
binds `v:texcoord` as a per-vertex channel sized to `vertexCount`, and
`Runtime.RenderExtraction.cpp:706` requires
`texcoords.size() == vertexCount`. The renderer therefore admits exactly one UV
per vertex, while an atlas needs two or more at a seam vertex. Something must
duplicate; today the duplication is applied to the authoritative ECS mesh.

**Decision: the seam is a UV fact, not a topology fact.** Atlas UVs are stored
on the halfedge/corner element domain, the entity keeps source topology, and the
duplication happens at GPU upload where it belongs.

Alternatives considered and rejected:

- *Separate render mesh distinct from the processing mesh.* Smaller geometry
  diff, and `SourceVertexForOutputVertex` already exists — but it creates a
  permanent dual identity that picking, primitive IDs, gizmo, readback, and
  texture bake would all have to map across. The element-domain contract and
  `UI-051`'s no-alias-entity rule exist precisely to keep one authoritative
  source; this would trade a one-time diff for a standing invariant.
- *Do not atlas on import; materialize UVs lazily.* Cheapest option and a
  reasonable default for a geometry-processing tool, but it defers the seam
  problem rather than resolving it. Retained as a possible follow-up policy
  question, not as this task's fix.
- *Switch the default backend from `FastStaged` to `xatlas`.* `xatlas` is
  already a vcpkg dependency and `DefaultXAtlasBackend()` is wired, so this is
  nearly free and would pack far better charts — but atlasing splits at seams by
  definition, so all-boundary vertices and broken connectivity would remain. A
  quality improvement, not a fix; must not be mistaken for one.

The corner domain already exists and carries properties today (`h:connectivity`,
`h:to_vertex`, `h:next`, `h:face`), so no new element domain is required. The
cost is breadth: `v:texcoord` is assumed vertex-domain in roughly eighteen
places across `geometry` and `runtime` — the OBJ/PLY/STL readers *and* writers
(`Geometry.HalfedgeMesh.IO.cpp`), `Geometry.HalfedgeMesh.Parameterization.cpp:509`,
`Geometry.HalfedgeMesh.Simplification.cpp:777` (FA-QEM reads UVs for seam-aware
collapse), `Runtime.AssetWorkflowModelTextureDecode.cpp:888`,
`Runtime.ParameterizationOperations.cpp`, both extraction sites, and the plan
builder. That breadth is why this task carries a slice plan.

Note that OBJ already stores UVs per corner natively (`f v/vt/vn`), so the
corner domain is the representation the source format uses; the current
vertex-domain assumption is the lossy one.

## Slice plan

Ordered so that every slice preserves the default CPU gate and the consuming
side is ready before the producing side changes.

- **Slice A — corner-domain UV property and resolution policy (`geometry`).**
  Introduce the halfedge/corner UV property and a documented resolution order
  (corner UVs win; vertex UVs remain the fallback when no corner UVs exist).
  No behavior change: nothing authors corner UVs yet. Closes `CPUContracted`.
  Defers all consumer migration to Slices B and D.
- **Slice B — GPU upload de-indexes corner UVs (`runtime`).** Teach
  `Runtime.GeometryPlanBuilders.Mesh.cpp` and the extraction validation in
  `Runtime.RenderExtraction.cpp` to split GPU vertices where corner UVs differ
  across corners sharing a vertex, and to keep the existing per-vertex path when
  only vertex UVs exist. Still no behavior change for current assets. Closes
  `Operational` for the upload path via a `gpu;vulkan` readback smoke.
- **Slice C — import preserves source topology (`runtime` asset workflow).**
  Stop building the entity halfedge mesh from `atlas.OutputMesh`; keep the
  source mesh and publish atlas UVs to the corner domain. **This is the slice
  that fixes the reported bug.** Slice B guarantees the renderer can already
  consume the result.
- **Slice D — retire the remaining vertex-domain assumptions.** OBJ/PLY/STL
  read/write round-trip preserving per-corner UVs, FA-QEM seam cost in
  `Geometry.HalfedgeMesh.Simplification.cpp`, texture decode/bake binding, and
  parameterization writeback.

## Required changes
- [x] Slice A — add the corner-domain UV property and the corner-over-vertex
      resolution policy in `geometry`.
- [x] Slice B — de-index corner UVs when building GPU vertex buffers; keep the
      per-vertex path intact when no corner UVs exist.
- [x] Slice C — stop building the entity halfedge mesh from `atlas.OutputMesh`;
      preserve source vertex/edge/halfedge cardinality and manifoldness.
- [x] Slice C — publish atlas UVs on the corner domain rather than mutating
      element-domain cardinality.
- [x] Slice C — verify the atlas-failure fallback path
      (`ResolvedTexcoordsValid == false`) preserves the same topology guarantee.
      Both branches now share one `ConvertResolvedMeshToHalfedge(entityMesh, …)`
      call built from the source mesh, so the guarantee is structural rather
      than duplicated per branch; `atlas.OutputMesh` is no longer used to build
      topology anywhere.
- [x] Slice C — report the GPU-side vertex duplication count and the resolved
      atlas provenance/backend in the import result, replacing the currently
      computed-but-never-surfaced `SeamSplitVertexCount`, so duplication remains
      visible where it now legitimately happens.
- [x] Slice D — OBJ read/write preserves authored per-corner UVs; the reader no
      longer splits positions to represent a `vt` seam, and materialization
      consumes payload corner UVs instead of re-atlasing them.
- [x] Slice D — `Runtime.TextureBakeModule` resolves UVs by the canonical
      corner-over-vertex order and de-indexes corner UVs into its own vertex
      table, clearing the `MissingTexcoords` failure slice C introduced. The
      split table moved to `Runtime.MeshSurfaceTopology`
      (`BuildMeshCornerTexcoordSplit`) and is shared with renderer upload,
      because the two cross-check each other through GPU residency (vertex
      count, index count, index fingerprint) and a second independently written
      split would silently disagree.
- [x] Slice D — scene save/load round-trips `h:texcoord` on the halfedge
      domain; previously a seam mesh reloaded with no parameterization at all.
- [x] Slice D — the UV-view readiness model and the render-extraction reupload
      revision both follow the resolution order, so a corner-UV mesh is neither
      reported as "no resolved texcoord property" nor left stale on the GPU
      after a UV edit.
- [ ] Slice D — remaining vertex-domain consumers, none of which lose data
      today: `Runtime.ParameterizationOperations` preflight wording,
      `Runtime.VisualizationRecipes` CPU-recipe dirty-stamp fallback,
      `Runtime.EditorWorkspaceSnapshots.Models` reserved-name filters, and
      `Geometry.HalfedgeMesh.Simplification` FA-QEM seam cost (which can now
      read the seam directly rather than inferring it from boundary vertices).

## Tests
- [x] Slice A — unit coverage for corner-domain UV storage and the
      corner-over-vertex resolution order (`tests/unit/geometry/Test.CornerTexcoords.cpp`, 9 cases).
- [x] Slice B — contract test asserting a mesh with differing corner UVs uploads
      the expected split GPU vertex count while the ECS mesh is unchanged
      (`Test.MeshGeometryExtraction` split cases + `Test.CornerTexcoordUpload`).
- [ ] Slice B — `gpu;vulkan` readback smoke asserting a seam-split UV mesh still
      renders. Written and green on hardware (NVIDIA GeForce RTX 4090, driver
      580.159.04) as commit `e1416f08`, then reverted: its wall clock varies
      between 13 s and 34 s and it times out against the cohort's 30 s budget.
      `BUG-143` owns the diagnosis and the restore.
- [x] Slice C — runtime contract test that materializes a closed manifold
      fixture and asserts vertex/edge/halfedge/face counts equal the source,
      boundary-halfedge count is zero, and Euler characteristic is preserved
      (`Test.AssetImportFormatCoverage`,
      `DirectObjImportPreservesClosedManifoldTopology` on a generated cube and
      `DirectObjImportOfSculptFixturePreservesItsManifold` on
      `tests/data/sculpt.obj`, which asserts the reported 3669/11013/22026/7342
      and χ = −2).
- [x] Slice C — test asserting UVs are resolved and finite on that same import
      without element-domain cardinality change (same two tests: `h:texcoord`
      sized to the halfedge count, all finite, a real seam present, and no
      `v:texcoord`). `DirectObjImportKeepsVertexUvsWhenTheAtlasNeedsNoSeam`
      pins the seam-free case to the unchanged vertex domain.
- [x] Slice C — test asserting the non-manifold renderable fallback still
      resolves UVs after the change
      (`NonManifoldObjImportStillResolvesVertexUvs`). The `!atlasUsable` branch
      itself is not reachable through the public import path: every input the
      atlas rejects as degenerate is already rejected earlier by MeshSoup
      validation (`ValidationSeverity::Error`), whose degeneracy tolerance is
      the looser of the two. It is covered structurally instead — see the
      corresponding `## Required changes` entry.
- [x] Slice D — OBJ round-trip test asserting per-corner UVs survive read →
      write → read (`Test.GeometryIO`,
      `ObjCornerTexcoordsSurviveWriteAndReload`), plus
      `LoadsOBJFaceTexcoordSeamOnCornersWithoutDuplicatingPositions` and
      `ObjWithoutTexcoordSeamStaysVertexDomain` pinning both sides of the
      policy, and `DirectObjImportPreservesAuthoredCornerUvs` /
      `SaveLoadRoundTripPreservesCornerDomainTexcoords` at the runtime level.
- [ ] Default CPU gate stays green after every slice.

## Docs
- [x] Record the corner-domain UV decision and the corner-over-vertex resolution
      order in `docs/architecture/property-coherence.md` (rendering-consumer
      section) and `docs/architecture/geometry-api-style.md`.
- [x] Document the canonical UV element domain in
      `docs/architecture/geometry-api-style.md` so future methods do not
      re-assume vertex-domain UVs.
- [x] Record the import topology guarantee in `src/app/Sandbox/README.md`.

## Acceptance criteria
- [x] Importing `tests/data/sculpt.obj` yields 3669 vertices, 11013 edges,
      22026 halfedges, 7342 faces, and zero boundary vertices.
- [x] UVs are present and finite after that import, on the corner domain, with
      no element-domain cardinality change.
- [ ] A textured mesh still renders correctly, with the seam split occurring at
      GPU upload rather than in the ECS mesh. Observed green once on hardware
      before the smoke was reverted for timing out; `BUG-143` owns restoring it,
      so this stays open and the upload path remains `CPUContracted`.
- [x] GPU-side vertex duplication is reported in the import result, never
      silent. `sculpt.obj` reports `gpu-split-vertices=17795`, the same count
      the old `SeamSplitVertexCount` computed and discarded.
- [x] No dual authoritative mesh identity is introduced.
- [x] No layering violation; `geometry` still owns the atlas method and
      `runtime` still owns composition.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'GeometryMaterialization|MeshGeometryExtraction|UvAtlas' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Disabling UV atlas generation on import to avoid the split.
- Introducing a second authoritative mesh identity (the rejected separate-render-
  mesh option); picking, primitive IDs, gizmo, readback, and texture bake must
  continue to address one mesh.
- Switching the default atlas backend to `xatlas` and calling this bug fixed —
  that reduces duplication but leaves the connectivity defect intact.
- Weakening or deleting existing atlas/texcoord coverage to make counts match.
- Landing Slice C before Slice B, which would publish corner UVs the renderer
  cannot yet consume.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere
  else.
- Slices A, C, and D close `Scaffolded → CPUContracted`. Slice B's
  `Operational` close is deferred to `BUG-143`, which owns the readback smoke
  that proves a seam-split textured mesh still renders correctly; until that
  lands the upload path is `CPUContracted` only.
