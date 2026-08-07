---
id: UI-047
theme: F
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: >-
  Path-entry affordance for existing import/scene commands. No geometry
  element-domain, property, support-radius, parameterization, or
  method-integration surface changes.
---
# UI-047 — File paths must be hand-typed into a raw text field

## Goal
- Give `File / Import` and `File / Scene` a usable way to choose a path, and
  make the working drag-and-drop route discoverable.

## Non-goals
- No new asset browser or project/library system.
- No change to import routing, payload-hint resolution, or the AssetIO queue.
- No fix for the key-event defect itself — `BUG-139` owns that.

## Context
- Symptom: both `File / Import` and `File / Scene` expose only an
  `ImGui::InputText` over a `std::array<char,1024>`
  (`Sandbox.EditorShell.cpp:3173-3174`) and no chooser of any kind.
- Compounded by `BUG-139`: because ImGui never receives key events, the field is
  append-only — Backspace, arrows and Ctrl+V all do nothing. In testing, a
  mistyped path could not be corrected at all; the only way to enter a path
  successfully was to type it into a freshly opened, empty field in one
  uninterrupted pass. This is why `UI-047` depends on `BUG-139`.
- Drag-and-drop *does* work end to end and is currently the only practical route
  (verified: `File drop received` → `Dropped geometry file routed to deferred
  import` → `Queued dropped geometry import` → `Asset import succeeded ...
  primitive_entities=1`), but **nothing in the UI mentions it** — no hint text,
  no drop-target affordance.
- `File / Scene` already renders a `FileDialogBoundaryText` string, which
  suggests the boundary was considered but no chooser was built.
- Impact: the first thing a new user does — open a file — is the hardest thing
  in the application.
- Hard layering constraint, verified: every `import` in `src/app/Sandbox` is
  `Extrinsic.Runtime.*`, matching the `app -> runtime` rule in `AGENTS.md`. The
  app cannot reach `platform` or `core` directly, and no runtime filesystem or
  dialog seam exists today. **Every option therefore requires a new
  runtime-owned surface**, so that cost does not distinguish the alternatives.

### Decision — in-ImGui browser over a runtime-owned listing model (decided 2026-08-07)

`runtime` owns a directory-listing model (entries, kinds, filtering by the
runtime-owned importable-extension set, current directory, parent navigation);
`app` draws it with ImGui. This is the same runtime-owns-model /
app-owns-presentation split every other editor panel already uses.

Alternatives considered and rejected:

- *Native dialog via a vcpkg dependency* (`nativefiledialog-extended`,
  `tinyfiledialogs`, or an XDG portal). Best end-user experience — recent files,
  keyboard navigation, network mounts. Rejected because it adds a dependency
  that on Linux pulls GTK or portal/DBus and would have to sit behind the
  `windowing` vcpkg feature; because the implementation would have to live in
  `platform` for window modality and then be re-exposed through `runtime`
  anyway; because a modal native dialog blocks its calling thread, which is
  exactly the `BUG-021` poll-thread freeze the input-lifecycle contract forbids;
  and because it cannot be covered on the default CPU gate.
- *Recent-files list only, no browser.* Roughly a tenth of the cost and covers
  the observed pain of re-entering the same asset repeatedly, but it cannot open
  a file the user has not already opened. Reasonable as a later addition on top
  of the browser; not a substitute.

The deciding factors were testability (a directory-listing model is trivially
contract-testable on the default CPU gate, a native dialog is not), the
repository dependency policy, and avoiding modal blocking on the platform poll
thread.

- Relationship to `BUG-139`: a browser is driven by clicks, so it is usable
  before the key-event fix lands. `BUG-139` is therefore **not** a hard blocker
  for this task; it remains required for the typed-path fallback to be
  correctable, and for any future save-as filename entry.
- Owner: `runtime` owns the listing model and the existing import/scene
  commands; `app` owns presentation only. Filesystem enumeration itself belongs
  in `core`/`platform` beneath the runtime model, never in `app`.

## Control surfaces
- Config: optional last-used-directory persistence.
- UI: a browse affordance next to each path field, plus a visible drag-and-drop
  hint.
- Agent/CLI: unchanged.

## Slice plan

- **Slice A — runtime directory-listing model.** Current directory, entries with
  kind (directory/file), parent navigation, and filtering against the
  runtime-owned importable-extension set. Presentation-free and fully covered on
  the default CPU gate. Defers all ImGui work to Slice B.
- **Slice B — app browser panel and drag-and-drop hint.** Draw the model,
  fill the existing path buffer on selection, keep the typed field as fallback,
  and add the drop hint.

## Required changes
- [ ] Slice A — add a runtime-owned directory-listing model with extension
      filtering sourced from the runtime importable-format table (see
      `ASSETIO-012` for the single-source table this should read).
- [ ] Slice A — enumerate the filesystem beneath `runtime` (in `core` or
      `platform`), never from `app`.
- [ ] Slice B — add a browse affordance to `File / Import` and `File / Scene`
      that fills the existing path buffer.
- [ ] Slice B — add a visible drag-and-drop hint to the import panel.
- [ ] Keep the typed path field working as the fallback and for scripted use.
- [ ] Handle unreadable/permission-denied directories as a reported state, not a
      crash or a silently empty listing.

## Tests
- [ ] Slice A — contract test over the listing model: entries, parent
      navigation, extension filtering, and an unreadable-directory case.
- [ ] Slice B — contract test asserting the chooser result populates the same
      runtime import/scene command path as typed entry.
- [ ] Test asserting the import panel reports the drag-and-drop route in its
      status/hint text.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update the `File / Import` and `File / Scene` prose in
      `src/app/Sandbox/README.md`.
- [ ] Record the chooser ownership split (runtime model, app presentation) in
      the owning runtime editor doc.

## Acceptance criteria
- [ ] A file can be selected without typing a path.
- [ ] The drag-and-drop route is discoverable from the UI.
- [ ] The listing model is covered by the default CPU gate.
- [ ] No new third-party dependency is added for this feature.
- [ ] `app` still imports only `Extrinsic.Runtime.*`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'SandboxEditor|FileImport|SceneFile' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Adding a native-dialog or filesystem-browsing third-party dependency; the
  decision above rejects that route.
- Enumerating the filesystem directly from `src/app/Sandbox`, or importing
  anything other than `Extrinsic.Runtime.*` there.
- Blocking the platform poll thread on directory enumeration (`BUG-021`).
- Removing the typed path field.
