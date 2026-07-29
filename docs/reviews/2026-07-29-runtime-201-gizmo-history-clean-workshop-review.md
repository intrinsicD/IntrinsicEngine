# Clean-workshop review — RUNTIME-201 gizmo history convergence

## Change under review

- Change: route transform-gizmo drag commit through the document-owned
  `EditorCommandHistory`, coalesce multi-selection into one validated
  transaction, and remove `GizmoUndoStack` plus its public accessors.
- Trigger(s): changes runtime wiring and exported runtime module surfaces.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/ci/run_clean_workshop_review.sh . --strict` and the strict layer check scanned 720 files, 6,323 import/include references, and 85 CMake links with no violation or allowlist entry. All new dependencies remain within `runtime`. |
| 2 | CMake target links match layer policy | pass | No CMake target, source ownership, or `target_link_libraries(...)` edge changed. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | The public surface shrank by deleting `GizmoUndoStack`, `GizmoTransformEdit`, and the frame/module undo-stack accessors. The replacement `DragCommit` and frame input name only same-layer runtime history/world types plus the existing ECS registry; no lower-layer API changed. |
| 4 | Renderer member/subsystem growth justified by an owning seam | n/a | No renderer state, subsystem, or graphics ownership changed. |
| 5 | New passes use typed IDs, not string routing | n/a | No frame-graph pass or renderer command route changed. |
| 6 | New frame-recipe dependencies resource-driven or explicitly justified | n/a | No frame recipe, resource declaration, or ordering edge changed. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | n/a | `RUNTIME-201` remains active at its `Retired` target; this slice makes no closure or maturity claim. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No layering exception, allowlist row, warning-mode gate, temporary shim, or compatibility facade was added. |

## Findings → follow-ups

- No findings.
