---
id: UI-048
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
  Editor workspace defaults, menu composition, and ImGui layout persistence.
  No geometry element-domain, property, support-radius, parameterization, or
  method-integration surface changes.
---
# UI-048 — Editor opens empty, hides file operations under View, and never remembers layout

## Goal
- Give the Sandbox a usable default workspace: a discoverable menu structure,
  a sensible set of windows open on first run, non-overlapping placement, and
  layout that survives a restart.

## Non-goals
- No docking/workspace-preset system.
- No change to which windows exist or what they contain (`UI-049` owns their
  internals).
- No new settings/preferences subsystem beyond what layout persistence needs.

## Context
Four related first-run defects observed on a clean launch:

- **The editor opens completely empty.** Every window is
  `OpenByDefault = false`, so a fresh launch shows a blue viewport, the
  reference triangle, and four menus. Nothing indicates that a scene hierarchy,
  inspector, or import panel exists.
- **There is no `File` menu.** The top-level menus are
  `View | PointCloud | Graph | Mesh`. `File / Scene` and `File / Import` are
  window *titles* registered with `MenuPath = {"View"}`
  (`Sandbox.EditorShell.cpp:3206` and `kBuiltinWindows` at `:169-177`), so
  opening a file means View → "File / Import". File operations are the most
  common entry point and are the least discoverable.
- **All windows open stacked at the same default position**, so opening two
  windows hides one behind the other with no visual cue. During testing,
  `Geometry Visualization` opened directly underneath `PointCloud / Appearance`
  and appeared not to have opened at all.
- **Layout is never persisted.** `Runtime.ImGuiAdapter.cpp:299` sets
  `io.IniFilename = nullptr` with the comment "the engine owns persistence" —
  but nothing in the repo saves or restores ImGui settings
  (`grep -rn 'SaveIniSettings\|LoadIniSettings'` is empty). The comment asserts
  an ownership that does not exist. Every launch resets all window positions,
  sizes, open state, and collapse state.
- Impact: compounding. Because nothing is open by default, the user must find
  windows through a mis-signposted menu; because they stack, they appear not to
  open; and because nothing persists, this is repeated every session.
- Owner: `app` owns menu composition, window registration defaults, and the
  editor shell; `runtime` owns `EditorWindowRegistry` and the ImGui adapter
  where persistence would live.

## Control surfaces
- Config: layout persistence location/opt-out through the existing engine-config
  lane.
- UI: menu structure and default-open window set.
- Agent/CLI: unchanged.

## Required changes
- [ ] Add a top-level `File` menu for the file/scene/import entries (and export
      once `UI-046` lands), keeping `View` for view windows.
- [ ] Choose a default-open window set for first run (e.g. Scene Hierarchy,
      Inspector, File / Import) and set `OpenByDefault` accordingly.
- [ ] Give newly opened windows non-overlapping default placement.
- [ ] Implement ImGui layout persistence (open state, position, size, collapse)
      through the engine-owned path the adapter comment already claims, or
      remove the claim and adopt ImGui's own ini handling — record which in
      `Context`.

## Tests
- [ ] Add a contract test asserting the builtin window menu paths include a
      `File` group containing the import/scene entries.
- [ ] Add a contract test asserting the default-open window set on a fresh
      workspace attachment.
- [ ] Add a test asserting layout state round-trips through save/restore.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update the window/menu inventory in `src/app/Sandbox/README.md`.
- [ ] Correct or remove the "the engine owns persistence" comment so it matches
      reality.

## Acceptance criteria
- [ ] A fresh launch shows a usable workspace, not an empty viewport.
- [ ] File operations are reachable from a `File` menu.
- [ ] Two newly opened windows do not fully occlude each other.
- [ ] Window layout survives an application restart.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'EditorWindowRegistry|SandboxEditor|ImGuiAdapter' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Leaving a comment that claims persistence the code does not implement.
- Moving window-registry ownership into `app`.
