---
id: UI-014
theme: none
depends_on: [ASSETIO-008, RUNTIME-109, RUNTIME-115, GRAPHICS-088, UI-016]
maturity_target: CPUContracted
---
# UI-014 — UV backend and texture bake controls

## Completion
- Retired on 2026-06-17 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: the sandbox editor now exposes selected-mesh UV diagnostics,
  xatlas-backed UV regeneration commands, property-catalog-driven bake source
  controls, generated texture bake command routing, and live ImGui controls for
  backend policy, atlas settings, target semantic, encoder, and output size.
- Evidence: focused runtime/UI coverage passed in the combined
  `SandboxEditor|EditorCommandHistory|Uv|TextureBake|SelectedMeshTextureBake|MeshAttributeTextureBake|DerivedJob|Progressive|PropertyCatalog`
  CTest run.

## Slice plan
- **Slice A. UV diagnostics panel model.** Surface selected mesh UV provenance, resolved texcoord property status, atlas dimensions, backend diagnostics, and debug/checker toggles from existing runtime snapshots.
- **Slice B. Bake source controls.** Consume the `UI-016` property catalog to list eligible mesh vertex/face bake sources while keeping edge/halfedge/graph/point-cloud rows visible but disabled with reasons.
- **Slice C. Bake command routing.** Call the `RUNTIME-115` selected-mesh bake command surface for user-requested generated texture bakes and report command/job diagnostics without UI owning bake or asset state.
- **Slice D. Tests/docs/retirement.** Add headless UV/bake UI model and command tests, update docs, refresh inventories, and retire at `CPUContracted`.

## Goal
- Expose sandbox editor controls for UV backend selection, resolved-UV diagnostics, atlas regeneration, and mesh attribute texture bake commands.

## Non-goals
- No UI-owned parameterization, bake, asset, or renderer state.
- No direct Vulkan, graphics, or `AssetService` calls from UI widgets.
- No custom algorithm implementation.
- No native file-dialog dependency.
- No graph or point-cloud texture baking UI.

## Context
- Owning subsystem/layer: `runtime` editor UI emits commands/options to runtime-owned systems; UI does not own simulation, render, asset, or geometry algorithm execution.
- `ASSETIO-008` provides resolved UV provenance and backend options.
- `RUNTIME-109` provides generic CPU bake requests and generated texture lifecycle.
- `RUNTIME-115` provides the selected-mesh bake command surface that UI widgets
  call for user-requested property texture bakes.
- `GRAPHICS-088` provides renderer-side UV debug/checker and texture residency behavior.
- `UI-016` provides the all-properties catalog and compatibility model that
  this task consumes for bake-source selection.
- The UI should let users inspect whether UVs are authored or generated, force regeneration through a chosen backend, and bake selected mesh properties into textures for visualization/material use.

## Required changes
- [x] Add a mesh UV panel that displays UV provenance, backend id, atlas resolution, padding, chart count, seam-split vertex count, quality diagnostics, and last failure status.
- [x] Add import/runtime settings for default UV backend, preserve-authored-vs-force-regenerate policy, atlas resolution, padding, and texels-per-unit.
- [x] Add a selected-mesh command to regenerate UVs through the chosen backend and route the mutation through runtime command history/dirty-state ownership.
- [x] Add a texture bake panel that consumes the `UI-016` property catalog and lists eligible mesh vertex/face properties by value kind and domain.
- [x] Add bake controls for target semantic, output size, encoder/colormap/range policy, generated asset key, and material/visualization binding target.
- [x] Add controls for UV debug/checker preview and explicit Htex-vs-resolved-UV bake mapping where both are available.
- [x] Surface runtime diagnostics for backend failure, invalid UVs, unsupported property type/domain, non-finite values, and generated texture upload deferrals.

## Tests
- [x] Add UI model tests proving UV diagnostics rows are populated from runtime snapshots without direct geometry/backend ownership.
- [x] Add command tests proving backend/regenerate/bake actions enqueue runtime commands and mark document dirty through `EditorCommandHistory`.
- [x] Add property-list tests proving internal/connectivity properties remain visible through `UI-016` but are separated from bakeable user/algorithm results.
- [x] Add tests proving authored-texture priority and generated-binding override choices are represented as explicit user commands.
- [x] Add regression tests for headless/null UI snapshots where renderer or texture upload is unavailable.

## Docs
- [x] Update `src/runtime/Editor/README.md` or `src/runtime/README.md` with the UV/bake command ownership model.
- [x] Update `tasks/backlog/ui/README.md` if follow-up UI work is opened.
- [x] Update `docs/architecture/graphics.md` or runtime docs if UI changes the mapping policy exposed to users.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.

## Acceptance criteria
- [x] Users can see whether selected mesh UVs are authored or generated and inspect backend diagnostics.
- [x] Users can choose a UV backend and request atlas regeneration without UI owning geometry algorithm state.
- [x] Users can bake eligible 1D-4D mesh properties into generated textures through runtime-owned commands.
- [x] Users can choose whether a generated texture is for visualization or for a material semantic such as albedo/normal/scalar/vector/displacement-ready output.
- [x] The UI remains headless-safe and preserves runtime/graphics layering boundaries.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditor|EditorCommandHistory|Uv|TextureBake' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not run xatlas or texture baking directly inside UI rendering code.
- Do not store generated texture or renderer handles in UI state.
- Do not add ImGui/platform dependencies below runtime.
- Do not expose unsupported graph/point-cloud bake paths as if they work.
- Do not make Htex regeneration implicit; it must remain an explicit user choice.

## Maturity
- Target: `CPUContracted`.
- No `Operational` follow-up is owed by this UI task; visual proof is owned by `GRAPHICS-088`.
