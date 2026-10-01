---
id: BUG-230
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive bug fix; evidence is the diff, regression tests, review and CI.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources]
---
# BUG-230 — Topology edits drop user properties, and undo cannot restore them

## Completion — 2026-10-02
Commit: `5a8172d1a`. Undo restores an exact copy of the stored source
components (all four domains, so user properties and original numbering return
bit-exact); redo republishes the after mesh. Simplify forwards surviving
vertices' user values through mesh garbage collection; edge, halfedge and face
user properties have no map and are dropped. Remesh and subdivide drop and
report. Dropped names are `<domain>:<name>` in `DroppedProperties`, appended to
the result message and returned as `dropped_properties` by the agent. Deviation
from the task wording: the undo state is the whole stored-component snapshot,
not only the properties `PopulateFromMesh` does not regenerate, because the
re-derived mesh renumbers edges, halfedges and faces. Added
`PropertyRegistry::CopyPropertyFrom` for type-erased copies. See
`docs/architecture/property-coherence.md`.

## Goal
Mesh simplify, remesh and subdivide preserve or explicitly report user-defined
properties on apply. Undo restores every property the entity had before the
edit.

## Context
- Found 2026-10-01 during RUNTIME-312 slice 7D. A 6x6 grid with a user vertex
  property `height` lost it after `mesh_simplify` and undo. The same commit path
  serves the Sandbox panels and the agent tools.
- `BuildHalfedgeMeshForTopologyEdit` (`Runtime.GeometryProcessingOperations.MeshSupport.cpp`)
  rebuilds from a triangle soup with positions and topology only.
- `CopyMeshSimplifyAuxiliaryProperties` (`Runtime.MeshTopologyOperations.Topology.cpp`)
  forwards only texcoords (the BUG-146 fix).
- `ApplyMeshTopologyState` calls `GS::PopulateFromMesh`, which replaces the
  Vertices/Edges/Faces property sets.
- `CommitMeshTopologyReplacement` snapshots before and after as a bare
  `HalfedgeMesh::Mesh`, so the undo state never contained user properties.
- Affected: simplify, remesh, subdivide. Not affected: denoise, which rewrites
  positions in place.
- Apply policy for remesh and subdivide, which change the vertex set: drop user
  properties on those domains and name them in the result/diagnostic, as
  BUG-146 does for UVs. Simplify carries surviving vertices' values through the
  index map. Interpolation is a possible later extension, not part of this fix.

## Acceptance criteria
- [x] The topology history state captures the entity's user property sets (all element domains, excluding those `PopulateFromMesh` regenerates); undo and redo restore them exactly.
- [x] Simplify keeps the values of surviving vertices' user properties through the old-to-new map; remesh and subdivide report dropped properties in the result instead of losing them silently.
- [x] Regression tests cover simplify, remesh and subdivide followed by undo, checking that `height` returns with its original values, and redo without crashes. The RUNTIME-312 agent test drops its BUG-230 workaround.
- [x] Panel and agent paths share the fix, with no agent-only branch.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'MeshTopology|Simplify|Remesh|Subdivide|SandboxAgentServer|SandboxEditorSession' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```
