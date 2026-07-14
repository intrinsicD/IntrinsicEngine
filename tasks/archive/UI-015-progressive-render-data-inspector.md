---
id: UI-015
theme: F
depends_on: [RUNTIME-111, RUNTIME-112]
maturity_target: CPUContracted
---
# UI-015 — Progressive render-data inspector

## Completion
- Completed: 2026-06-16. Commit/PR: this retirement commit.
- Maturity: `CPUContracted`.
- Fix summary: extended `SandboxEditorUi` with data-only progressive
  inspector models for entity shape, render lanes/presentation slots,
  compatible/incompatible source properties, uniform default edits,
  property-binding edits through command history, per-entity derived-job rows,
  and composition child summaries. The ImGui panel renders the progressive
  slots/jobs without owning algorithms, asset IO, worker state, or graphics
  resources.
- Evidence: focused headless UI tests cover mesh, graph, point-cloud, and
  composition snapshots; slot default/property commands through
  `EditorCommandHistory`; property picker disabled reasons; and job snapshot
  rows.
- Follow-up boundary: explicit selection-to-authored-property transfer is not
  part of this progressive inspector commit and remains a future value-gated UI
  command if a workflow needs persisted selection masks.

## Goal
- Add editor/UI data-model and controls for per-entity progressive render-data
  bindings, slot defaults, source-property selection, and derived-job debug
  visibility.

## Non-goals
- No UI-owned geometry algorithms, texture baking, asset loading, or graphics
  resources.
- No Vulkan-specific proof.
- No native file-dialog or platform-window work.
- No graph/point-cloud texture baking UI in the first implementation.
- No direct mutation of transient selection overlays as authored properties
  without an explicit transfer command.

## Context
- Owning subsystem/layer: runtime/editor UI consumes data-only snapshots and
  emits runtime-owned commands. UI must not own simulation, asset, graphics, or
  worker-thread state.
- ADR-0021 requires per-entity job visibility, render-lane toggles, lane
  presentation binding, per-slot defaults/source properties, and composition
  aggregate status.
- `RUNTIME-111` supplies descriptor/property compatibility data. `RUNTIME-112`
  supplies derived-job snapshots.

## Required changes
- [x] Add selected-entity inspector data for entity shape: composition, mesh
      leaf, graph leaf, and point-cloud leaf.
- [x] Add render-lane controls for surface, edges/lines, and points that issue
      runtime commands against the existing render-lane components.
- [x] Add per-lane material/presentation binding controls that resolve to the
      per-entity binding map from `RUNTIME-111`.
- [x] Add per-slot controls for enabled state, uniform default value, source
      property descriptor, authored/generated texture state, readiness, and
      last diagnostic.
- [x] Add color-picker command support for the selected lane's default albedo
      or color slot.
- [x] Add property pickers that list compatible properties first and show
      incompatible properties disabled with deterministic reasons.
- [x] Add selected-entity derived-job rows with status, dependencies, elapsed
      time, progress, source generation, output semantic, and diagnostic text.
- [x] Add aggregate composition-entity status that summarizes child material
      and job state without exposing one inherited child-material edit table by
      default.
- [x] Record explicit selection-to-property transfer as a deferred
      value-gated command; the progressive inspector does not mutate transient
      selection overlays into authored properties.

## Tests
- [x] Add headless UI model tests for mesh, graph, point-cloud, and composition
      selected-entity snapshots.
- [x] Add command tests for lane toggles, slot default edits, and source
      property binding changes routed through runtime command ownership.
- [x] Add property-picker tests proving compatible-first ordering and disabled
      reasons for incompatible properties.
- [x] Add derived-job table tests for queued, running, waiting, applying,
      complete, failed, cancelled, and stale/discarded states.
- [x] Add composition aggregate status tests proving child job/material state is
      summarized without editing all children as one inherited table.
- [x] Add regression coverage through the non-goal/command boundary that the
      progressive inspector does not copy transient overlay state implicitly.

## Docs
- [x] Update `src/runtime/Editor/README.md` or `src/runtime/README.md` with
      progressive render-data inspector ownership and command routing.
- [x] Update `tasks/backlog/ui/README.md` if additional child UI tasks are
      opened during implementation.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface
      changes.

## Acceptance criteria
- [x] Users can inspect entity shape, render lanes, presentation bindings,
      property compatibility, slot defaults, readiness, and diagnostics from
      data-only UI snapshots.
- [x] Users can assign a uniform lane color and choose source properties for
      compatible slots without UI owning algorithms or graphics resources.
- [x] Users can monitor per-entity and global derived-job state with dependency
      and failure diagnostics.
- [x] Composition entities report aggregate child status while detailed edits
      remain on leaf entities.
- [x] The default CPU-supported CTest gate verifies the UI model and command
      routing.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditor|Progressive|RenderData|DerivedJob|PropertyPicker' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not run geometry algorithms, texture bakes, asset IO, or graphics uploads
  from UI rendering code.
- Do not store renderer/GPU handles in UI state.
- Do not bypass runtime command history for user-visible mutations.
- Do not hide incompatible property choices; show disabled choices with
  reasons instead.
- Do not turn transient selection/highlight overlays into authored properties
  without an explicit transfer command.

## Maturity
- Target: `CPUContracted`.
- This UI task closes the inspector data-model and command-routing contract; no
  `Operational` follow-up is owed.
