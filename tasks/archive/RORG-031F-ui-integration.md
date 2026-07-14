---
id: RORG-031F
theme: F
depends_on: []
completed: 2026-07-05
---
# RORG-031F — UI integration backlog seed

## Status
- Retired on 2026-07-05 at `Scaffolded`.
- Branch/PR: local `main`; PR not opened.
- This task was a planning-only umbrella. No source or UI feature changes are
  owed by this retirement.
- Concrete UI work has moved into independently tracked children and follow-up
  runtime/rendering/platform tasks. Current retired UI children through
  `UI-031` are indexed in [`tasks/backlog/ui/README.md`](../backlog/ui/README.md)
  and the retirement log.
- Remaining deferred UI workflows keep prospective IDs (`UI-009..012`) and
  explicit external triggers; none has a fired, accepted implementation owner
  in the current backlog state.

## Goal
- Serve as the umbrella for promoted editor/UI integration work over
  `Extrinsic.Runtime.SandboxEditorUi`: keep the current-state inventory and
  the deferred-workflow list honest, and name the concrete child tasks (with
  their triggers) that future UI work opens under.

## Non-goals
- Implementing UI features or panels in this task.
- Refactoring panel systems or the ImGui adapter internals in this task.
- Re-opening workflows that were explicitly deferred by design (native
  dialogs, IME/multi-window) without their named trigger firing.
- This planning scaffold is the intended endpoint; no `Operational` follow-up
  is owed for `RORG-031F` itself.

## Context
- Current promoted state: `Extrinsic.Runtime.SandboxEditorUi`
  (`src/runtime/Editor/Runtime.SandboxEditorUi.cppm` + `.cpp`) is attached to
  the sandbox app by `src/app/Sandbox/Sandbox.cppm` through the
  `Runtime::IApplication` hook.
- The retired `UI-001..008` series (2026-06-03 .. 2026-06-09) established the
  promoted baseline: scene hierarchy / inspector / selection panels,
  per-domain windows for mesh/graph/point-cloud, geometry-processing
  capability discovery and K-Means execution, visualization property presets,
  render-graph diagnostics, drag/drop import status, and editor dirty-state,
  undo/redo, file-path entry, and headless-safe workflows.
- Later retired UI children are now self-describing in
  [`tasks/backlog/ui/README.md`](../backlog/ui/README.md) and
  [`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md): render-hint and
  visualization controls (`UI-013`, `UI-019`, `UI-020`), progressive
  render-data/property/bound-state/UV/bake inspection (`UI-014..017`),
  menu-first startup (`UI-018`), geometry availability migration (`UI-021`),
  focused method windows for normals, denoise, remesh/subdivide, curvature,
  outlier removal, simplification, and ICP registration (`UI-022`, `UI-024..029`),
  frame-pacing diagnostics (`UI-030`), and domain-window information
  architecture cleanup (`UI-031`).
- Deferred-by-design workflows and their owners:
  - IME composition and multi-window support — deferred until an alternative
    platform backend lands (`PLATFORM-004` seed; platform owns the events).
  - Native file dialogs — the promoted endpoint is path-entry UI plus
    platform dropped-path events; reopening requires an explicit
    platform/runtime ownership decision.
  - Sample/debug scene creation workflows — deferred by the `LEGACY-011`
    value gate (no runtime-owned command currently improves an accepted
    fixture).
- `GRAPHICS-084` and `GRAPHICS-084C` retired selected visualization
  property-buffer residency for current promoted adapters; both explicitly
  avoided adding arbitrary property-array editor UI without a concrete
  consumer.
- Broader selected-entity responsiveness remains open under `RUNTIME-138`, not
  this UI seed.

## Child tasks (open when the trigger fires)
- **UI-009 — IME and multi-window editor support.** Trigger: an alternative
  platform backend task (per `PLATFORM-004`) lands with the required window/
  input events.
- **UI-010 — Native file-dialog boundary decision.** Trigger: a platform or
  runtime task accepts native-dialog ownership and defines the request
  surface; until then path-entry + drop events remain the endpoint.
- **UI-011 — Arbitrary property-array visualization UI.** Trigger:
  a concrete runtime/editor workflow needs selecting arbitrary non-preset
  property arrays for visualization beyond the current promoted scalar,
  isoline, color-buffer, vector-candidate, and binding controls. The
  `GRAPHICS-084`/`GRAPHICS-084C` residency seam is available for selected
  promoted adapters, but it deliberately did not create this editor workflow.
- **UI-012 — Sample/debug scene workflows.** Trigger: the `LEGACY-011` value
  gate re-evaluates in favor of a runtime-owned scene-authoring command.
Each future child cites this seed's retired record or the UI backlog README in
its Context and declares which deferred workflow it resolves. New concrete UI
IDs continue after `UI-031`; `UI-009..012` remain reserved prospective IDs for
the historical deferred workflows above.

## Required changes
- [x] Keep the current-state inventory and deferred-workflow list above
      aligned with `Runtime.SandboxEditorUi` and the platform/graphics task
      state whenever a child opens or a trigger fires.
- [x] Open child tasks per the trigger list above using
      `tasks/templates/task.md`; do not bundle multiple workflows into one
      child. No new child is opened by this retirement because the remaining
      prospective workflows have no fired, accepted implementation trigger.
- [x] Retire this seed once UI work is steady-state (children open from
      triggers without the umbrella adding coordination value).

## Tests
- [x] No new UI tests in this planning seed; child tasks own verification.

## Docs
- [x] Keep [`tasks/backlog/ui/README.md`](../backlog/ui/README.md) aligned with
      the open child list.
- [x] Child tasks update `docs/migration/nonlegacy-parity-matrix.md` when a
      deferred workflow gains a promoted owner.

## Acceptance criteria
- [x] The inventory in Context matches the promoted `SandboxEditorUi` state
      and the UI category README.
- [x] Every deferred workflow names its trigger and prospective child task.
- [x] `python3 tools/agents/validate_tasks.py --root tasks --strict` passes.

## Verification
```bash
test -f tasks/archive/RORG-031F-ui-integration.md
grep -q "SandboxEditorUi" tasks/archive/RORG-031F-ui-integration.md
grep -q "UI-009" tasks/archive/RORG-031F-ui-integration.md
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Implementing UI features, panels, or platform events under this seed.
- Introducing unrelated graphics/renderer/runtime feature work.

## Maturity
- Target: `Scaffolded` — this is a planning umbrella, and the scaffold is the
  intended endpoint; no `Operational` follow-up is owed. Children own their
  own maturity targets.
- Closed at `Scaffolded` by retiring the planning umbrella after concrete UI
  work moved into child task records and the remaining deferred workflows were
  left with named prospective IDs and triggers.
