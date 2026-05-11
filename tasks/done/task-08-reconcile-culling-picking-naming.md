# Task 8 — Reconcile CullingPass and Picking/Selection pass naming

- Status: completed (reviewed and corrected 2026-05-02)
- Owner: Claude (claude/next-active-task-YdXAU)
- Branch / PR: `claude/next-active-task-YdXAU`
- Completion date: 2026-05-02
- Commit / PR: local split branch `split/current-working-tree-2026-05-02`; remote PR reference TBD.
- Follow-ups: none for this docs-only queue item.
- Next verification step: re-run `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root . --strict` on the proposed PR (both currently pass locally with zero findings).

`docs/architecture/rendering-three-pass.md` now lists `CullingPass` in the Pass Contract table and Pipeline Order, identifies it as a real rendergraph pass owned by `Extrinsic.Graphics.Pass.Culling`, and adds a Pass module naming map that documents the split selection source modules under the logical `PickingPass` stage. `GRAPHICS-007` claims `Pass.Culling` ownership and adds `CullingPass::Execute` test expectations. `GRAPHICS-012` defines the logical `PickingPass` over the split `Pass.Selection.*` modules with explicit acceptance criteria that names agree across docs and source. `tasks/backlog/rendering/README.md` was updated to reference both ownership claims. A later review reverted out-of-scope C++ class renames from this docs-only task and updated the naming table to match the existing source classes.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Reconcile pass naming between docs, tasks, and source for culling and picking/selection.

## Problem

The source contains concrete pass modules:

- `Pass.Culling`
- `Pass.Selection.EntityId`
- `Pass.Selection.EdgeId`
- `Pass.Selection.FaceId`
- `Pass.Selection.PointId`
- `Pass.Selection.Outline`

But docs/tasks describe culling as internal/implicit and picking as one logical PickingPass. This creates ambiguity for agents.

## Required changes

- [x] Update `docs/architecture/rendering-three-pass.md`:
   - [x] Define `CullingPass` or `Visibility/Culling stage` explicitly.
   - [x] Clarify whether it is a real rendergraph pass or a helper stage.
   - [x] If the source keeps `Pass.Culling`, document it as the canonical module name.

- [x] Update GRAPHICS-007:
   - [x] Explicitly mention `Pass.Culling`.
   - [x] State whether GRAPHICS-007 owns culling pass command contracts, draw-bucket contracts, or both.
   - [x] Add tests for `Pass.Culling` pass contract if not already listed.

- [x] Update GRAPHICS-012:
   - [x] Define `PickingPass` as a logical picking/selection stage composed of:
     - [x] `Pass.Selection.EntityId`
     - [x] `Pass.Selection.FaceId`
     - [x] `Pass.Selection.EdgeId`
     - [x] `Pass.Selection.PointId`
     - [x] readback/result seam
     - [x] `Pass.Selection.Outline`
   - [x] Do not require source consolidation unless there is a strong architectural reason.
   - [x] Add acceptance criteria that docs and pass module names agree.

- [x] Update `tasks/backlog/rendering/README.md` dependency notes if needed.

## Acceptance criteria

- [x] Agents can tell whether culling is a pass, a helper, or a stage.
- [x] Agents can tell that split selection modules are acceptable implementation units under the logical picking/selection stage.
- [x] No source file renames are required in this task.

## Verification

```bash
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- No C++ implementation changes.
- No module renames.
- No shader changes.

The pass module list proves that the current source has split selection passes and a culling pass.
