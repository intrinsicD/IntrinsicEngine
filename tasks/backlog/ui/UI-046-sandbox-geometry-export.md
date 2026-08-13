---
id: UI-046
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - geometry.element-domain-sources
---
# UI-046 — Sandbox cannot export geometry at all

## Goal
- Let the Sandbox write a selected mesh, graph, or point-cloud entity back out
  through the existing geometry IO writers, so processed geometry can leave the
  application.

## Non-goals
- No new file format or writer implementation; this task only reaches the
  writers that already exist.
- No scene-document format change (`File / Scene` save is a scene document, not
  a geometry export).
- No batch/scripted export pipeline.

## Context
- Symptom: `grep -rn 'Export' src/app/Sandbox/Editor/` returns nothing. There is
  no export affordance anywhere in the editor.
- The capability exists one layer down and is unreachable:
  `Geometry.IO.cppm:77-89` declares writers for OBJ, OFF, STL, PLY, PCD, TGF and
  edge-list (`Asset.ImportRouter.cpp:47-66` additionally records exportability
  per format, with the drift noted in `ASSETIO-012`).
- Impact: you can import a mesh, denoise/curvature/K-Means/parameterize it, and
  then have no way to get the result out of the application. For a
  geometry-processing engine this makes the whole processing surface a dead end.
- Owner: `app` owns the editor affordance; `runtime` owns the export command and
  the entity → geometry-payload projection; `geometry` owns the writers. The app
  must not call `geometry` directly.
- Depends in practice on `UI-047` for a usable destination-path chooser, but is
  not blocked by it — a runtime-owned export command can land first.
- Export must read the canonical element-domain sources of the selected entity
  (positions, topology, and the properties the chosen format can represent) and
  report what it dropped.

## Control surfaces
- Config: none required.
- UI: an export affordance on the selected entity, with a format chooser whose
  options come from the runtime-owned format capability table.
- Agent/CLI: none required in this task.

## Required changes
- [ ] Add a runtime-owned export command that projects a selected ECS geometry
      entity to the geometry IO writer surface.
- [ ] Gate the format chooser on the runtime-owned exportability table so
      unsupported (domain, format) pairs are disabled with a reason.
- [ ] Add the editor affordance (menu entry + panel) routed through that command.
- [ ] Report what the chosen format could not represent (dropped properties,
      lost topology) in the result rather than silently discarding it.

## Tests
- [ ] Add a runtime contract test that imports a fixture, exports it, and
      asserts the written file round-trips to equivalent counts.
- [ ] Add a test asserting an unsupported (domain, format) pair is rejected with
      the runtime-owned reason.
- [ ] Add a test asserting dropped-property reporting is populated when the
      format cannot carry a property.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Document the export surface in `src/app/Sandbox/README.md`.
- [ ] Update the geometry IO docs if the exportability table moves.

## Acceptance criteria
- [ ] A selected mesh can be exported to a supported format and re-imported.
- [ ] Unsupported format choices are disabled with a visible reason.
- [ ] Lossy exports report what was dropped.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'GeometryIo|SandboxEditor|SceneEditing' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Importing `geometry` directly from `src/app/Sandbox`.
- Adding a second hand-maintained format capability table (see `ASSETIO-012`).
