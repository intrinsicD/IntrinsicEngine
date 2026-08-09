---
id: BUG-147
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug147"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-09T20:10:00Z"
maturity_target: CPUContracted
contract_schema: 1
contracts:
  - geometry.element-domain-sources
  - geometry.property-coherence
---
# BUG-147 — Editor UV regeneration replaces the mesh with the atlas chart-split mesh

## Status

- Completed and retired on 2026-08-10.
- Completion commit: this retirement commit.
- Commits: `af092d51` (shared chart-split corner recovery, mechanical),
  `8514b43f` (UV regeneration keeps the mesh's topology).

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
- [x] Stop building the published mesh from `atlas.OutputMesh`; keep the
      selected mesh's topology and publish atlas UVs on the corner domain. The
      published mesh is built from the source soup, and the UVs are recovered
      onto the source faces' corners through the shared chart-split mapping.
- [x] Publish on the domain that can represent the result: corner when the
      atlas cut a seam, vertex when it did not, so a mesh is not promoted to
      corner UVs for nothing. Exactly one UV authority survives either way.
- [x] Preserve the existing `NoChange` idempotence contract. The comparison
      reads both UV domains, and the before-state now carries the mesh's
      existing corner UVs, so a corner-UV rewrite is not mistaken for a no-op
      and undo restores what was there.
- [x] Keep reporting the GPU-side split count, now documented as upload
      duplication rather than vertices added to the mesh — the same wording
      correction `BUG-137` made for import.
- [x] Verify the atlas-failure and authored-UV-preserved branches keep the same
      topology guarantee, structurally rather than per branch:
      `atlas.OutputMesh` is no longer used to build topology anywhere in this
      path, only read for its UVs and cross-references, so every branch
      inherits the guarantee from the single source-soup conversion.

## Tests
- [x] Contract test: regenerate UVs on a closed icosahedron and assert vertex,
      edge, and face counts and the Euler characteristic are unchanged
      (`UvRegenerationPreservesClosedManifoldTopology`). Against the unfixed
      source it reports 60 vertices against 12, 60 edges against 30, and χ = 20
      against 2 — the probe's split, reproduced as an assertion.
- [x] Contract test: UVs are resolved and finite afterwards, on the corner
      domain, count-matched to the halfedge count, with no element-domain
      cardinality change and no surviving `v:texcoord` (same test). It also
      asserts undo restores the unparameterized mesh and its topology.
- [x] Contract test: the seam-free case stays on the vertex domain
      (`UvRegenerationWithoutASeamStaysOnTheVertexDomain`), mirroring
      `DirectObjImportKeepsVertexUvsWhenTheAtlasNeedsNoSeam`.
- [x] The existing `UvRegenerationThatReproducesStoredUvsReportsNoChange`
      idempotence case still passes, now through a comparison that reads both
      UV domains.
- [x] Default CPU gate stays green: 4176/4176 with the expected
      `GlfwLifecycleLsan` skip.

## Docs
- [x] Record that the corner-domain publication rule applies to every UV
      producer, not only import, in `docs/architecture/geometry-api-style.md`
      ("Every UV producer publishes over the source topology").

## Acceptance criteria
- [x] Regenerating UVs on a closed manifold leaves it closed and manifold with
      its original counts.
- [x] UVs are present, finite, and corner-owned when the atlas produced a seam.
- [x] Split counts are still reported, described as upload duplication.
- [x] No dual authoritative mesh identity is introduced: the atlas output is an
      intermediate that is read and discarded, never published.
- [x] No layering violation.

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
