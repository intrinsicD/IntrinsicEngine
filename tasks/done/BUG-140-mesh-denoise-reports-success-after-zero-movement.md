---
id: BUG-140
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug140"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-08T00:26:00Z"
contract_schema: 1
contracts:
  - geometry.element-domain-sources
  - geometry.property-coherence
---
# BUG-140 — Mesh denoise reports Applied/Success after moving zero vertices

## Status

- Completed and retired on 2026-08-08.
- Both denoise paths computed `MovedVertexCount` against the published
  positions and then set `Applied` unconditionally — the synchronous command
  and the CPU worker, with the publisher re-affirming it. `WrittenCount` is
  slot-derived (`VertexSlotCount - SkippedDeletedVertexCount`), so it stays
  non-zero whenever the kernel runs at all and can never distinguish "smoothed
  the mesh" from "did nothing".
- Both paths now gate on the already-computed moved count: zero moved reports
  `EditorCommandStatus::NoChange` with a message naming the pinned count and,
  when every live vertex is pinned, saying so explicitly. `NoChange` already
  existed on the shared enum and is already produced by
  `ToEditorCommandStatus`, so no enum change was needed.
- The synchronous path returns before `CommitMeshDenoisePositions`, so a no-op
  no longer publishes an identity edit or leaves a useless undo entry. The
  job path does the same before its commit.
- Added a derived `PinnedBoundaryRatio()` accessor rather than a stored field,
  and the panel now prints `Pinned boundary vertices: N (P%)`. The panel's
  detail block was gated on `Succeeded()`, which would have hidden exactly the
  diagnostics that explain a no-op, so it now also renders for `NoChange`.
- Two contract tests were added: an all-boundary single-triangle mesh that
  asserts `NoChange`, `moved == 0`, a pinned ratio of 1.0, the explanatory
  message, unchanged positions, and no history entry; and a closed tetrahedron
  that still reports `Applied` with a non-zero moved count and a zero pinned
  ratio. Against the unfixed source the first fails with `Succeeded()` true and
  `history.IsDirty()` true — the reported symptom, plus the no-op undo entry.
- Verified: 6/6 `MeshDenoise` cases pass and the default CPU gate passes
  4140/4140 with its expected GLFW/LeakSanitizer skip; layering is clean.

## Audit of sibling operations

The required audit found the same written-count-implies-success shape in every
operation below. They are **not** fixed here — this task was scoped to the
reporting defect it reproduced — and are spun out as
[`BUG-145`](../backlog/bugs/BUG-145-editor-operations-report-applied-from-written-counts.md),
which carries the full per-site inventory with file and line references:

- Mesh, graph, and point-cloud **vertex normals** (six sites): status comes
  from the kernel's own `RecomputeStatus`; the reported count is a copied
  `WrittenCount` and nothing checks whether a normal actually differs.
- Mesh **curvature** (two sites): `ScalarWrittenCount` is
  `mean.size() + gaussian.size()`, a pure written count.
- **Remesh, subdivide, simplify** (nine sites): output counts are reported but
  never compared against the input. Simplify even computes a `CollapseCount`
  and does not consult it.
- Point-cloud **outlier removal** (three sites): `RejectedIndices` may be empty
  while the status is still `Applied`.
- **UV regeneration** (two sites).

ICP registration is the counter-example already doing it correctly, mapping
history state through `ToEditorCommandStatus`.

- Completion commit: this retirement commit.

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
- [x] Report a distinct non-success status when a denoise run moves zero
      vertices, and include the reason (all vertices pinned / no interior
      neighborhood).
- [x] Include the pinned-vertex ratio in the surfaced result, not just the
      absolute count.
- [x] Audit the sibling mesh operations that build their status from a written
      or processed count rather than a changed count, and list the ones that
      share the defect in this task before fixing them or spinning out a
      follow-up.

## Tests
- [x] Add a runtime contract test that denoises a fully-pinned mesh and asserts
      a non-success status plus the explanatory diagnostic.
- [x] Add a test asserting a normal manifold denoise still reports `Applied`
      with a non-zero moved count.
- [x] Default CPU gate stays green.

## Docs
- [x] Record the "changed-count, not written-count" status rule wherever editor
      command status semantics are documented.

## Acceptance criteria
- [x] A denoise run that moves zero vertices never reports `Applied`/`Success`.
- [x] The result explains why nothing moved.
- [x] The audit of sibling operations is recorded in this task.

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
