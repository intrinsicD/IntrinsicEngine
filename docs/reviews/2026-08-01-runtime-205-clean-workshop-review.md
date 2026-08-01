# Clean-workshop review — RUNTIME-205 scene-interaction helpers

## Change under review

- Change: retire
  [`RUNTIME-205`](../../tasks/done/RUNTIME-205-internalize-scene-interaction-helpers.md)
  after moving one-consumer gizmo-frame and selection-readback state directly
  into `SceneInteractionModule`, then deleting both helper BMIs and their
  direct tests.
- Trigger(s): changes runtime composition ownership and removes exported
  runtime module surfaces.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/ci/run_clean_workshop_review.sh . --strict` scanned 735 source files, 6,915 import/include references, and 85 CMake links with no violation or allowlist entry. All relocated behavior remains in `runtime`; graphics still receives only copied interaction values and readbacks. |
| 2 | CMake target links match layer policy | pass | Four files were removed from the existing runtime target. No target or `target_link_libraries(...)` edge was added or changed. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | The public surface only shrank. `SceneInteractionModule` retains its opaque PImpl and existing accessors; durable `GizmoInteraction`, `SelectionController`, refinement, and copied snapshot contracts are unchanged. |
| 4 | Renderer member/subsystem growth is justified by an owning seam | n/a | No renderer member, graphics subsystem, service, or ownership rule changed. |
| 5 | New passes use typed IDs, not string routing | n/a | No frame-graph pass or renderer command route changed. |
| 6 | New frame-recipe dependencies resource-driven or explicitly justified | n/a | No frame recipe, resource declaration, or ordering edge changed. Existing viewport-input, before-extraction, and maintenance phases are preserved. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | The task closes at `Retired`: owner-level lifecycle/correlation behavior passes the focused selector, the full CPU gate, and both required sanitizer gates; the helper surfaces and direct tests are absent. No maturity follow-up is owed. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No compatibility shim, forwarding facade, replacement helper, allowlist row, warning-mode gate, or temporary exception was added. |

## Architecture checklist result

- `SceneInteractionModule` remains the sole interaction composition owner and
  stores the relocated state only inside its PImpl.
- World/document epoch validation, live-registry drag cancellation, history
  gating, selection sequence bounds, and copied render-snapshot publication
  retain their existing phase and ownership boundaries.
- No feature, backend axis, config surface, byte layout, or performance claim
  was introduced.

## Findings → follow-ups

- No findings.
