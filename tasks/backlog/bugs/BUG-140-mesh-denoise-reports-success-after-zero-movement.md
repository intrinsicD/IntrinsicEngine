---
id: BUG-140
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
# BUG-140 — Mesh denoise reports Applied/Success after moving zero vertices

## Goal
- Make the mesh denoise result state distinguish "smoothed the mesh" from "did
  nothing", so a run that moves no vertices is not reported as `Applied` /
  `Success`.

## Non-goals
- No change to the bilateral denoise algorithm or its default iteration counts.
- No fix for the import topology defect that causes the all-boundary condition
  here — `BUG-137` owns that.
- No redesign of the shared `EditorCommandStatus` enum.

## Context
- Symptom: `Mesh / Processing / Denoise` on an imported `sculpt.obj` reports
  `Last denoise run: Applied`, `Geometry status: Success`,
  `Written: 21464 / 21464`, but **`moved: 0`** and
  `Pinned boundary vertices: 21464` (of 21464). The mesh is bit-identical
  afterwards while the UI says the operation succeeded.
- Unchecking `Preserve boundary` and re-running gives
  `Pinned boundary vertices: 0`, `moved: 716` — still only 3% of vertices,
  because the shredded topology leaves most vertices without real neighbors.
- Root cause of the all-boundary condition is `BUG-137` (import replaces the
  halfedge topology with the UV-atlas chart-split mesh, so every vertex becomes
  a boundary vertex). This task is deliberately scoped to the **reporting**
  defect, which is independently wrong: even on a correct mesh, a no-op run
  should not read as `Applied`.
- Expected behavior: a run where `moved == 0`, or where every vertex was pinned,
  should report a distinct non-success state (e.g. `NoChange`) and say why —
  "all 21464 vertices pinned as boundary; nothing to smooth".
- Impact: silent no-op masked as success. A user cannot tell that denoise did
  nothing, and the same masking pattern likely applies to other operations that
  report `Applied` from a written-count rather than a changed-count.
- Owner: `runtime` editor geometry-processing operations (mesh denoise result
  construction).

## Required changes
- [ ] Report a distinct non-success status when a denoise run moves zero
      vertices, and include the reason (all vertices pinned / no interior
      neighborhood).
- [ ] Include the pinned-vertex ratio in the surfaced result, not just the
      absolute count.
- [ ] Audit the sibling mesh operations that build their status from a written
      or processed count rather than a changed count, and list the ones that
      share the defect in this task before fixing them or spinning out a
      follow-up.

## Tests
- [ ] Add a runtime contract test that denoises a fully-pinned mesh and asserts
      a non-success status plus the explanatory diagnostic.
- [ ] Add a test asserting a normal manifold denoise still reports `Applied`
      with a non-zero moved count.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Record the "changed-count, not written-count" status rule wherever editor
      command status semantics are documented.

## Acceptance criteria
- [ ] A denoise run that moves zero vertices never reports `Applied`/`Success`.
- [ ] The result explains why nothing moved.
- [ ] The audit of sibling operations is recorded in this task.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'SandboxEditorMeshMethods|GeometryProcessing' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Fixing the symptom by changing denoise defaults (e.g. turning off
  `Preserve boundary`) instead of the status semantics.
- Absorbing `BUG-137`'s topology fix into this task.
