---
id: BUG-146
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug146"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-09T19:20:00Z"
maturity_target: CPUContracted
contract_schema: 1
contracts:
  - geometry.element-domain-sources
  - geometry.property-coherence
---
# BUG-146 — Topology-changing mesh operations silently destroy corner-domain UVs

## Status

- Completed and retired on 2026-08-09.
- Completion commit: this retirement commit.
- Slice commits: `cda42c9f` (shared corner-publish mapping, mechanical),
  `7bba3100` (simplify preserves), `548cf690` (remesh/subdivide report their
  discard).
- Follow-up opened by this task's audit:
  [`BUG-147`](../backlog/bugs/BUG-147-uv-regeneration-shatters-mesh-topology.md).

## Goal
- Stop mesh simplify (and the sibling topology-replacing operations) from
  wiping an entity's `h:texcoord` when they rebuild its topology, so a mesh
  imported through the `BUG-137` corner-UV path does not lose every UV it has
  the first time it is simplified.

## Non-goals
- No new UV resampling method, atlas backend, or seam-aware remeshing
  algorithm. This task is about not discarding what already exists.
- No change to the corner-over-vertex resolution order itself (`BUG-137`
  owns it).
- No change to the vertex-domain (`v:texcoord`) forwarding that already
  works.

## Context
- Symptom: a mesh whose UVs live on the corner domain loses them entirely
  across a topology edit. Probe run on `29303592` + the `BUG-137` slice-D
  changes, through the real editor command path
  (`ApplyEditorMeshSimplifyCommand`, FA_QEM, `PreserveUvSeams = true`) on a
  4x4 grid plane carrying a uniform `h:texcoord`:

  ```
  PROBE before: h:texcoord exists=1 size=112
  PROBE simplify: ok=1 seamPinned=0
  PROBE after:  h:texcoord exists=0 size=64
  ```

  The command reports success. The property is gone, and no diagnostic says
  so.
- Mechanism, in two independent halves:
  1. `Runtime.GeometryProcessingOperations.Mesh.cpp:5497`
     (`CopyMeshSimplifyAuxiliaryProperties`) forwards only `v:texcoord` into
     the scratch halfedge mesh. The scratch mesh is rebuilt from a triangle
     soup (`BuildHalfedgeMeshForDenoise` →
     `Geometry::Mesh::Conversion::ToHalfedgeMesh`), so it starts with no
     halfedge properties at all.
  2. `ApplyMeshTopologyState` publishes the scratch mesh through
     `GS::PopulateFromMesh`, whose halfedge branch assigns
     `hComp.Properties = mesh.HalfedgeProperties()` wholesale
     (`ECS.Component.GeometrySourcesPopulate.cpp`). Whatever the scratch mesh
     does not carry is not merely stale afterwards — it is removed.
- The forwarding in (1) cannot be a straight copy: vertex numbering survives
  the GeometrySources → triangle-soup → halfedge round trip (documented at
  `Runtime.GeometryProcessingOperations.Mesh.cpp:2798-2804`) but **halfedge
  numbering does not**. A corner attribute needs a per-corner map.
  `BuildMeshSurfaceTriangleCornerTopology`
  (`Runtime.MeshSurfaceTopology.cppm:54`) already produces exactly that map
  for the stored side, and the scratch mesh's faces follow soup triangle
  order, so the two can be joined per (face, corner-slot).
- Second-order consequence: because the corner UVs never reach the scratch
  mesh, FA_QEM's `PreserveUvSeams` also sees no seam (`seamPinned=0` above)
  even though `Geometry.HalfedgeMesh.Simplification` can now classify corner
  seams exactly (`BUG-137` slice D). The geometry-layer contract is correct;
  the runtime never hands it the data.
- Scope beyond simplify: every operation routed through
  `BuildHalfedgeMeshForTopologyEdit` + `ApplyMeshTopologyState` shares this
  shape — remesh, subdivide, UV regeneration. Their per-operation
  correctness differs (remesh and subdivide *create* corners that have no
  source UV, so preservation there is resampling, not copying), so the task
  must decide per operation rather than applying one rule.
- Impact: `BUG-137` made import preserve topology by moving UVs to the corner
  domain. This defect means the first topology edit after such an import
  throws those UVs away, silently. Texture bake, the UV view, and GPU upload
  all then see an unparameterized mesh.
- Owner: `runtime` owns the scratch-mesh round trip and the publish step;
  `ecs` owns `PopulateFromMesh`; `geometry` already exposes the corner
  vocabulary and needs no change.

## Required changes
- [x] Decide and document, per topology-replacing operation, whether corner
      UVs are preserved, resampled, or explicitly dropped with a reported
      diagnostic. The decisions, and what the audit found, are recorded in
      `## Per-operation audit` below.
- [x] Forward `h:texcoord` into the scratch halfedge mesh through a real
      per-corner map for the operations that preserve it, reusing
      `BuildMeshSurfaceTriangleCornerTopology` rather than writing a second
      corner walk. The publish half of that mapping moved out of asset
      materialization into `Runtime::PublishMeshCornerTexcoords`, so both
      consumers produce the same mapping by construction.
- [x] Report the outcome in the operation result when UVs are dropped or
      resampled, so a lost parameterization is never silent.
      `EditorMeshTexcoordOutcome` (`None` / `Preserved` / `Discarded`) is on
      all three topology-replacing results, and a discard also states the loss
      in the result message, which all three panels already render.
- [x] Audit the sibling paths (`remesh`, `subdivide`, UV regeneration) against
      the same rule and record the per-operation decision in the task.

## Per-operation audit

| Operation | Decision | State |
| --- | --- | --- |
| Simplify | Preserve | Done. A collapse removes corners and the survivors keep their own UVs, which is the correct answer and is what lets `PreserveUvSeams` see a seam at all. |
| Remesh | Discard, reported | Done. Re-tessellation produces corners with no source UV; resampling onto the new surface is a separate capability, not a side effect of this command. |
| Subdivide | Discard, reported | Done. Same reason. Loop and Catmull-Clark UV rules exist and would be the eventual answer, but they are new work, not a repair. |
| UV regeneration | Neither — it is a UV *producer*, and the audit found it broken in a different way | Spun out as [`BUG-147`](../backlog/bugs/BUG-147-uv-regeneration-shatters-mesh-topology.md). |

The UV-regeneration finding is worth stating plainly because it is not the
defect this task set out to fix. That command builds the published mesh from
`atlas.OutputMesh` and commits it as the entity mesh — exactly what `BUG-137`
slice C removed from the import path, at an entry point slice C never touched.
Probed on a closed icosahedron: 12 V / 30 E / 60 H / 20 F in, 60 V / 60 E /
120 H / 20 F out, 20 charts for 20 faces, status `Applied`. It does not merely
drop UVs; it converts the mesh into a triangle soup.

## Tests
- [x] Contract test: simplify a corner-UV mesh through the editor command and
      assert `h:texcoord` still exists, is count-matched to the new halfedge
      count, and is finite
      (`MeshSimplifyPreservesCornerUvSeamsAcrossTheTopologyEdit`). Against the
      unfixed source the property is absent entirely.
- [x] Contract test: FA_QEM `SeamVerticesPinned` matches an independently
      derived seam set for a corner-UV mesh with a real interior seam, driven
      through the editor command (same test). Against the unfixed source it
      reports 0 against 5 expected.
- [x] Contract test for each operation that deliberately drops UVs: the drop is
      reported, not silent
      (`TopologyOperationsReportWhatTheyDidToTheParameterization`, covering
      remesh, subdivide, the preserving simplify counterpart, and the no-UV
      case). Each reported outcome is cross-checked against what the entity
      actually holds afterwards rather than trusting the field alone.
- [x] Default CPU gate stays green: 4174/4174 with the expected
      `GlfwLifecycleLsan` skip.

## Docs
- [x] Record the per-operation corner-UV policy in
      `docs/architecture/property-coherence.md` next to the `BUG-137`
      resolution order ("Topology-replacing operations and UVs").

## Acceptance criteria
- [x] A corner-UV mesh survives simplify with its UVs intact and
      count-matched.
- [x] No topology-replacing operation removes a UV property without saying so
      in its result.
- [x] `PreserveUvSeams` pins a real corner seam through the editor path: the
      pinned count now equals an independently derived seam set (5 of 25 grid
      vertices) where the unfixed source reported 0.
- [x] No layering violation; no second corner-walk implementation — the publish
      mapping moved into `Runtime::PublishMeshCornerTexcoords` so both
      consumers share one, and the forwarding reuses
      `BuildMeshSurfaceTriangleCornerTopology` rather than adding a walk.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'SandboxEditorMeshMethods|Simplification' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Deleting or weakening the corner-UV coverage added by `BUG-137` to make
  counts line up.
- Writing a second, independent corner walk beside
  `BuildMeshSurfaceTriangleCornerTopology`; the two would silently disagree.
- Reintroducing a vertex-domain-only UV assumption to sidestep the map.
- Declaring the operation successful while its UVs were dropped.

## Maturity
- Target: `CPUContracted`, and `no Operational follow-up is owed`. The whole
  defect and its fix are observable on the CPU editor command path: the
  property is present or absent in `GeometrySources` after the command runs.
  `BUG-137` slice B already owns the `gpu;vulkan` evidence that a corner-UV
  mesh renders, and nothing here changes that upload path.
