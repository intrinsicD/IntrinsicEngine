---
id: RORG-031F
theme: F
depends_on: []
---
# RORG-031F — UI integration backlog seed

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

## Context
- Current promoted state: `Extrinsic.Runtime.SandboxEditorUi`
  (`src/runtime/Editor/Runtime.SandboxEditorUi.cppm` + `.cpp`) is attached to
  the sandbox app by `src/app/Sandbox/Sandbox.cppm` through the
  `Runtime::IApplication` hook.
- The retired `UI-001..008` series (2026-06-03 .. 2026-06-09) established the
  promoted baseline: scene hierarchy / inspector / selection panels (UI-001),
  per-domain windows for mesh/graph/point-cloud (UI-002), geometry-processing
  capability discovery and K-Means execution (UI-003/004), visualization
  property presets — scalar, isoline, color-buffer (UI-005), render-graph
  diagnostics panel (UI-006), drag/drop import status (UI-007), and editor
  dirty-state, undo/redo, file-path entry, and headless-safe workflows
  (UI-008). Member-by-member history lives in
  [`tasks/backlog/ui/README.md`](README.md) and the retirement log.
- Deferred-by-design workflows and their owners:
  - IME composition and multi-window support — deferred until an alternative
    platform backend lands (`PLATFORM-004` seed; platform owns the events).
  - Native file dialogs — the promoted endpoint is path-entry UI plus
    platform dropped-path events; reopening requires an explicit
    platform/runtime ownership decision.
  - Sample/debug scene creation workflows — deferred by the `LEGACY-011`
    value gate (no runtime-owned command currently improves an accepted
    fixture).
  - Generic GPU residency for arbitrary property arrays — visualization
    presets only today; the residency seam is owned by `GRAPHICS-084`.

## Child tasks (open when the trigger fires)
- **UI-009 — IME and multi-window editor support.** Trigger: an alternative
  platform backend task (per `PLATFORM-004`) lands with the required window/
  input events.
- **UI-010 — Native file-dialog boundary decision.** Trigger: a platform or
  runtime task accepts native-dialog ownership and defines the request
  surface; until then path-entry + drop events remain the endpoint.
- **UI-011 — Arbitrary property-array visualization UI.** Trigger:
  `GRAPHICS-084` retires its residency seam; extends the preset-only
  visualization controls to arbitrary scalar/color/vector arrays.
- **UI-012 — Sample/debug scene workflows.** Trigger: the `LEGACY-011` value
  gate re-evaluates in favor of a runtime-owned scene-authoring command.

Each child cites this seed in its Context and declares which deferred
workflow it resolves; IDs continue from `UI-009` (UI-001..008 are taken).

## Required changes
- [ ] Keep the current-state inventory and deferred-workflow list above
      aligned with `Runtime.SandboxEditorUi` and the platform/graphics task
      state whenever a child opens or a trigger fires.
- [ ] Open child tasks per the trigger list above using
      `tasks/templates/task.md`; do not bundle multiple workflows into one
      child.
- [ ] Retire this seed once UI work is steady-state (children open from
      triggers without the umbrella adding coordination value).

## Tests
- [ ] No new UI tests in this planning seed; child tasks own verification.

## Docs
- [ ] Keep [`tasks/backlog/ui/README.md`](README.md) aligned with the open
      child list.
- [ ] Child tasks update `docs/migration/nonlegacy-parity-matrix.md` when a
      deferred workflow gains a promoted owner.

## Acceptance criteria
- [ ] The inventory in Context matches the promoted `SandboxEditorUi` state
      and the UI category README.
- [ ] Every deferred workflow names its trigger and prospective child task.
- [ ] `python3 tools/agents/validate_tasks.py --root tasks --strict` passes.

## Verification
```bash
test -f tasks/backlog/ui/RORG-031-ui-integration.md
grep -q "SandboxEditorUi" tasks/backlog/ui/RORG-031-ui-integration.md
grep -q "UI-009" tasks/backlog/ui/RORG-031-ui-integration.md
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Implementing UI features, panels, or platform events under this seed.
- Introducing unrelated graphics/renderer/runtime feature work.

## Maturity
- Target: `Scaffolded` — this is a planning umbrella, and the scaffold is the
  intended endpoint; no `Operational` follow-up is owed. Children own their
  own maturity targets.
