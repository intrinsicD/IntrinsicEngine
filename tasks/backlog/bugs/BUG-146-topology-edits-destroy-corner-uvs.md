---
id: BUG-146
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
# BUG-146 — Topology-changing mesh operations silently destroy corner-domain UVs

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
- [ ] Decide and document, per topology-replacing operation, whether corner
      UVs are preserved (simplify: collapse keeps surviving corners),
      resampled, or explicitly dropped with a reported diagnostic. A silent
      drop is not an acceptable outcome for any of them.
- [ ] Forward `h:texcoord` into the scratch halfedge mesh through a real
      per-corner map for the operations that preserve it, reusing
      `BuildMeshSurfaceTriangleCornerTopology` rather than writing a second
      corner walk.
- [ ] Report the outcome in the operation result when UVs are dropped or
      resampled, so a lost parameterization is never silent.
- [ ] Audit the sibling paths (`remesh`, `subdivide`, UV regeneration) against
      the same rule and record the per-operation decision in the task.

## Tests
- [ ] Contract test: simplify a corner-UV mesh through the editor command and
      assert `h:texcoord` still exists, is count-matched to the new halfedge
      count, and is finite. Fails against the current source with the property
      absent.
- [ ] Contract test: FA_QEM `SeamVerticesPinned` is non-zero for a corner-UV
      mesh with a real seam driven through the editor command, mirroring the
      existing vertex-domain case in `Test.SandboxEditorMeshMethods`.
- [ ] Contract test for each operation that deliberately drops UVs: the drop
      is reported, not silent.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Record the per-operation corner-UV policy in
      `docs/architecture/property-coherence.md` next to the `BUG-137`
      resolution order.

## Acceptance criteria
- [ ] A corner-UV mesh survives simplify with its UVs intact and
      count-matched.
- [ ] No topology-replacing operation removes a UV property without saying so
      in its result.
- [ ] `PreserveUvSeams` pins a real corner seam through the editor path.
- [ ] No layering violation; no second corner-walk implementation.

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
