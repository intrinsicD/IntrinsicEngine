---
id: UI-034
theme: F
depends_on:
  - ARCH-012
maturity_target: CPUContracted
---
# UI-034 — Decentralized editor window contribution, capture contract, and property-plot widgets

## Status
- Status: in progress.
- Owner: Codex.
- Branch: `codex/ui-034-editor-window-contribution`.
- Progress: Slice A registry/lifecycle contract implemented and focused tests
  pass; callbacks may safely unregister windows during dispatch.
- Next verification step: implement Slice B and rerun the focused ImGui adapter,
  engine-wiring, input-action, and editor-window registry contracts.

## Goal
- Adopt the framework24 viewer's interaction/layout model in the Sandbox editor: domains contribute their menu entries and windows through a registration seam instead of a central enum, closed windows cost nothing per frame, viewport input is gated by one explicit UI-capture contract with a global UI-visibility toggle, and any scalar per-element property of the selection can be inspected through generic property-selector plus histogram/plot widgets.

## Non-goals
- No visual restyle: theme, fonts, and spacing are out of scope (the requirement is layout and control, not appearance).
- No UI-owned algorithm or simulation state — panels keep dispatching runtime-owned commands (retired `RUNTIME-141` lane); this task must not weaken that boundary.
- No windowing/platform backend changes.
- No migration of all existing domain windows in this slice; the seam plus two
  migrated exemplar domains prove the pattern. `ARCH-006` owns the remaining
  panel-family split and relocation into `app/Sandbox`.

## Context
- Owner/layer: `src/runtime/Editor` (`Runtime.SandboxEditorUi`) and the app-side wiring in `src/app/Sandbox`; UI emits commands/events only.
- Reference model (framework24, `lib_bcg_viewer`): each domain system self-registers its menu and panel handlers on the event dispatcher (`src/bcg_system_gui.cpp` triggers `Event::Gui::RenderMenu` / `Event::Gui::Render`; systems connect and disconnect their render handlers as panels open and close), a per-frame capture snapshot (`gui.captured_keyboard` / `captured_mouse` / `widgets_active`) gates camera/picking input, a global hotkey toggles the whole GUI, and generic property widgets (`include/bcg_viewer_gui.h`: `PropertyContainerCombo`, `PropertyList`, ImPlot-backed `PlotData`) bind panels to the property system instead of bespoke per-feature widgets.
- Current state: `Runtime.SandboxEditorUi` exposes three domain kinds and uses a
  private 14-section enum to index a fixed 42-slot open-state array. Adding a
  domain window still requires edits to the central menu, section enum, and
  draw switchboard. Per-window update cost is not uniformly gated by
  visibility (retired `UI-031` moved model builds behind visibility for its
  windows; the async cache side is owned by `RUNTIME-138`).
- Precedents to preserve: menu-first defaults (retired `UI-018`), the Properties-as-data-explorer information architecture (retired `UI-031`), input-capture leak regression coverage (retired `BUG-036`), and the property catalog (retired `UI-016`).
- Clarification (nonblocking): plotting needs an ImPlot equivalent. Default chosen here: add the `implot` vcpkg port next to the existing `imgui` dependency. If a repo-local minimal histogram widget is preferred instead, only the widget-layer checkbox below changes.
- ARCH-013 re-review (2026-07-08): Decision re-scoped onto ADR-0024 D11. The
  `ARCH-012` gate held; the window contribution seam should be the planned
  EditorUiModule/panel registry shape rather than another central enum, and
  input capture should be a single frame snapshot consumed by runtime
  viewport/picking code. Panels issue commands/events through runtime seams
  and may consume module services, but must not own algorithm state or receive
  `Engine&`.
- Ownership split with `ARCH-006`: this task is the sole owner of the generic
  registration, lazy-lifecycle, capture, visibility-toggle, and property-widget
  contracts. `ARCH-006` consumes those contracts for the broader editor-shell
  split and Sandbox-content relocation; it must not introduce a competing
  registry. The two exemplar migrations here prove the seam rather than
  transferring ownership of the full migration to this task.

## Slice plan
- **Slice A — registry and lifecycle contract.** Add a generic runtime editor
  window registry with stable IDs, structured menu paths, explicit open state,
  duplicate rejection, unregister support, and transition observation. Prove
  menu discovery and zero callbacks for closed windows in CPU contract tests;
  keep the existing fixed Sandbox windows operational during migration.
- **Slice B — capture and visibility contract.** Snapshot
  `{captured_keyboard, captured_mouse, widgets_active}` once at the end of the
  editor frame, route camera/gizmo/picking and input-action suppression through
  that snapshot, and add one command path plus the global `G` input action for
  registry visibility. Preserve per-window state while hidden and retain the
  `BUG-036` regression coverage.
- **Slice C — property widget contract.** Add the `implot` manifest dependency,
  pair its context with the existing ImGui adapter lifecycle, and add a generic
  scalar-property selector/plot model that excludes non-scalars and reports
  filtered non-finite samples. Cover the model headlessly before drawing it.
- **Slice D — exemplar integration and closure.** Register one mesh-processing
  window and one appearance/property window through the new seam, including
  the generic scalar plot in the appearance exemplar. Run focused and default
  CPU gates, update architecture/dependency docs, and retire the task at
  `CPUContracted`. `ARCH-006` then consumes the seam to extract the shell and
  move the remaining fixed content.

## Required changes
- [ ] Add a domain-window registration seam to `Runtime.SandboxEditorUi`: domains register `{menu path, window id, draw callback, open-state}` at composition time; the closed `SandboxEditorDomainWindowKind` enum becomes an implementation detail behind the seam (existing kinds keep working during migration).
- [ ] Lazy window lifecycle: a closed window contributes no per-frame model-build or draw cost; open/close transitions are observable so domains can drop caches (aligned with, but not owning, `RUNTIME-138`).
- [ ] Single frame-level capture contract: one editor-owned `{captured_keyboard, captured_mouse, widgets_active}` snapshot derived from ImGui IO once per frame; camera, picking, and viewport input consume only this snapshot (no scattered `WantCapture*` reads).
- [ ] Global UI-visibility toggle (hotkey plus command) that hides all editor windows while keeping the viewport interactive, mirroring framework24's `g` toggle; per-window open-state survives the toggle.
- [ ] Generic property widgets in the editor widget library: a property-selector combo over the selection's property catalog (retired `UI-016` seam) and a histogram/plot view for scalar per-element properties (`implot` per the clarification default).
- [ ] Migrate two exemplar domains (one mesh-processing window, one appearance/visualization window) onto the seam, lazy lifecycle, and property widgets to prove the pattern end to end.

## Tests
- [x] Headless/CPU contract tests for the registration seam: registered windows appear in the menu model, unregistered kinds are absent, duplicate registration fails closed.
- [x] Lazy-lifecycle test: a closed window's draw/model-build callbacks are not invoked across frames (counter probe).
- [ ] Capture-contract test: synthetic ImGui capture states gate a recorded camera/picking input path; the `BUG-036` leak regression coverage stays green.
- [ ] Toggle test: global hide runs no window draw callbacks; restore returns the prior open-state.
- [ ] Property-widget model test: the property-catalog-to-plot-model mapping handles empty selection, non-scalar properties (excluded), and NaN-containing properties (filtered, with the filtered count reported) without failures.

## Docs
- [ ] Update the editor/UI architecture notes that own `Runtime.SandboxEditorUi` with the registration seam, capture contract, and widget-library additions.
- [ ] Record the `implot` dependency decision in `vcpkg.json` and the dependency notes if the clarification default stands.

## Acceptance criteria
- [ ] Adding a new domain window requires no edit to a central enum or switchboard.
- [ ] Closed windows are provably free per frame (test above).
- [ ] All viewport input decisions read the single capture snapshot.
- [ ] The global toggle and both migrated exemplar windows work in the sandbox.
- [ ] The default CPU gate stays green; no UI-owned engine state is introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Editor|Sandbox|Ui' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No algorithm or simulation state ownership in UI.
- No visual/theme changes.
- No big-bang migration of all existing windows in this slice.

## Maturity
- Target: `CPUContracted` for the seam, lifecycle, capture, and widget-model contracts in this slice; the two exemplar migrations are verified interactively in the sandbox as part of this task. Remaining window migrations open as separate follow-up UI tasks; no `Operational` follow-up is owed by this task beyond those.
