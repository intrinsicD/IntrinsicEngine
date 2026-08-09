---
id: BUG-147
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
# BUG-147 — Editor UV regeneration replaces the mesh with the atlas chart-split mesh

## Goal
- Make the editor's "Regenerate UVs" command preserve the selected mesh's
  vertex/edge/halfedge topology and publish the atlas result on the corner
  domain, the same guarantee `BUG-137` established for import.

## Non-goals
- No new unwrapping method, chart packer, or atlas backend, and no change to
  which backend is the default. Better charts are a separate improvement and
  would not fix this.
- No change to the atlas's own output contract in `geometry`; `UvAtlas`
  legitimately produces a chart-split output mesh.
- No change to `BUG-146`'s remesh/subdivide UV-discard reporting.

## Context
- Symptom: regenerating UVs on a closed manifold shatters it into a triangle
  soup. Probe on `548cf690` through the real editor command
  (`ApplyEditorUvRegenerationCommand`, resolution 64, padding 2) with a closed
  icosahedron:

  ```
  PROBE in : V=12  E=30  H=60   F=20   (closed, no boundary)
  PROBE run: status=Applied  seamSplit=48  charts=20
  PROBE out: V=60  E=60  H=120  F=20   v:texcoord=1  h:texcoord=0
  ```

  Twenty faces became twenty charts — one per triangle — so every vertex was
  split and no edge is shared any more. Face count is the only quantity
  preserved. The command reports `Applied`.
- Mechanism: this is `BUG-137`'s defect at a second entry point that slice C
  never touched. `Runtime.GeometryProcessingOperations.Mesh.cpp` builds the
  published mesh with
  `Geometry::Mesh::Conversion::ToHalfedgeMesh(atlas.OutputMesh)` and commits it
  through `CommitUvMeshTopologyReplacement`, exactly as import did before
  `BUG-137`. `Geometry.UvAtlas` emits a fresh output vertex per
  `(chart, source-vertex)` pair by design, so consuming `OutputMesh` as the
  entity mesh is what converts a manifold into a soup.
- `SeamSplitVertexCount` is computed and *is* reported here (48 above), unlike
  the import path where `BUG-137` found it discarded — but reporting the damage
  is not the same as not doing it.
- Impact: this silently undoes `BUG-137` for any mesh the user regenerates UVs
  on, which is the one command a user reaches for precisely when they want a
  usable parameterization. Downstream, every connectivity-dependent operation
  then runs on a soup: denoise pins every vertex as boundary, simplify collapses
  nothing meaningful, curvature becomes per-triangle speckle. `BUG-137`'s
  retirement narrative describes the same cascade from the import side.
- The fix shape already exists and is proven: publish the atlas UVs on
  `h:texcoord` over the *source* topology through
  `Runtime::PublishMeshCornerTexcoords`, which is exactly what
  `Runtime.AssetWorkflowGeometryMaterialization` does after `BUG-137` slice C,
  and let the renderer's existing corner-UV de-indexing (slice B) handle the
  duplication at upload.
- Owner: `runtime` composes the command; `geometry` owns the atlas and needs no
  change.

## Required changes
- [ ] Stop building the published mesh from `atlas.OutputMesh`; keep the
      selected mesh's topology and publish atlas UVs on the corner domain.
- [ ] Preserve the existing `NoChange` idempotence contract: re-running on a
      mesh whose UVs the atlas reproduces must still report `NoChange` and leave
      no undo entry, now compared on the corner domain.
- [ ] Keep reporting the GPU-side split count, which after the fix describes
      upload duplication rather than mesh damage — the same wording change
      `BUG-137` made for import.
- [ ] Verify the atlas-failure and authored-UV-preserved branches keep the same
      topology guarantee, structurally rather than per branch.

## Tests
- [ ] Contract test: regenerate UVs on a closed manifold fixture and assert
      vertex/edge/halfedge counts and Euler characteristic are unchanged and no
      vertex is on a boundary. Fails against the current source with the probe's
      12→60 vertex split.
- [ ] Contract test: UVs are resolved and finite afterwards, on the corner
      domain, with no element-domain cardinality change.
- [ ] Contract test: the seam-free case stays on the vertex domain, mirroring
      `DirectObjImportKeepsVertexUvsWhenTheAtlasNeedsNoSeam`.
- [ ] The existing `UvRegenerationThatReproducesStoredUvsReportsNoChange`
      idempotence case still passes.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Record that the corner-domain publication rule applies to every UV
      producer, not only import, in `docs/architecture/geometry-api-style.md`.

## Acceptance criteria
- [ ] Regenerating UVs on a closed manifold leaves it closed and manifold with
      its original counts.
- [ ] UVs are present, finite, and corner-owned when the atlas produced a seam.
- [ ] Split counts are still reported, described as upload duplication.
- [ ] No dual authoritative mesh identity is introduced.
- [ ] No layering violation.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'UvRegeneration|SandboxEditorUi' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Disabling UV regeneration, or narrowing which meshes it accepts, to avoid the
  split.
- Switching the default atlas backend and calling this fixed — atlasing splits
  at seams by definition, so a better packer reduces the damage without
  removing it. `BUG-137` rejected exactly this substitution.
- Introducing a second authoritative mesh identity for the atlas output.
- Weakening the existing idempotence or authored-UV-preservation coverage to
  make counts line up.

## Maturity
- Target: `CPUContracted`, and `no Operational follow-up is owed`. The topology
  guarantee and the corner-domain publication are both observable on the CPU
  editor command path. `BUG-137` slice B already owns the `gpu;vulkan` evidence
  that a corner-UV mesh renders, and this task publishes into that same proven
  upload path.
