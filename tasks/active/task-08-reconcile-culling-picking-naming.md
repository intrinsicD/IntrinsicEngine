# Task 8 — Reconcile CullingPass and Picking/Selection pass naming

- Status: in-progress
- Owner: Claude (claude/next-active-task-YdXAU)
- Branch / PR: `claude/next-active-task-YdXAU`
- Next verification step: re-run `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root . --strict` on the proposed PR (both currently pass locally with zero findings).

`docs/architecture/rendering-three-pass.md` now lists `CullingPass` in the Pass Contract table and Pipeline Order, identifies it as a real rendergraph pass owned by `Extrinsic.Graphics.Pass.Culling`, and adds a Pass module naming map that documents the split selection source modules under the logical `PickingPass` stage. `GRAPHICS-007` claims `Pass.Culling` ownership and adds `CullingPass::Execute` test expectations. `GRAPHICS-012` defines the logical `PickingPass` over the split `Pass.Selection.*` modules with explicit acceptance criteria that names agree across docs and source. `tasks/backlog/rendering/README.md` was updated to reference both ownership claims.

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

1. Update `docs/architecture/rendering-three-pass.md`:
   - Define `CullingPass` or `Visibility/Culling stage` explicitly.
   - Clarify whether it is a real rendergraph pass or a helper stage.
   - If the source keeps `Pass.Culling`, document it as the canonical module name.

2. Update GRAPHICS-007:
   - Explicitly mention `Pass.Culling`.
   - State whether GRAPHICS-007 owns culling pass command contracts, draw-bucket contracts, or both.
   - Add tests for `Pass.Culling` pass contract if not already listed.

3. Update GRAPHICS-012:
   - Define `PickingPass` as a logical picking/selection stage composed of:
     - `Pass.Selection.EntityId`
     - `Pass.Selection.FaceId`
     - `Pass.Selection.EdgeId`
     - `Pass.Selection.PointId`
     - readback/result seam
     - `Pass.Selection.Outline`
   - Do not require source consolidation unless there is a strong architectural reason.
   - Add acceptance criteria that docs and pass module names agree.

4. Update `tasks/backlog/rendering/README.md` dependency notes if needed.

## Acceptance criteria

- Agents can tell whether culling is a pass, a helper, or a stage.
- Agents can tell that split selection modules are acceptable implementation units under the logical picking/selection stage.
- No source file renames are required in this task.

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
