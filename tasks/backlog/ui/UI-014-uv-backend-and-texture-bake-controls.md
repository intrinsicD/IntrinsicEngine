---
id: UI-014
theme: none
depends_on: [ASSETIO-008, RUNTIME-109, GRAPHICS-088]
maturity_target: CPUContracted
---
# UI-014 — UV backend and texture bake controls

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
- `GRAPHICS-088` provides renderer-side UV debug/checker and texture residency behavior.
- The UI should let users inspect whether UVs are authored or generated, force regeneration through a chosen backend, and bake selected mesh properties into textures for visualization/material use.

## Required changes
- [ ] Add a mesh UV panel that displays UV provenance, backend id, atlas resolution, padding, chart count, seam-split vertex count, quality diagnostics, and last failure status.
- [ ] Add import/runtime settings for default UV backend, preserve-authored-vs-force-regenerate policy, atlas resolution, padding, and texels-per-unit.
- [ ] Add a selected-mesh command to regenerate UVs through the chosen backend and route the mutation through runtime command history/dirty-state ownership.
- [ ] Add a texture bake panel that lists eligible mesh vertex/face properties by value kind and domain.
- [ ] Add bake controls for target semantic, output size, encoder/colormap/range policy, generated asset key, and material/visualization binding target.
- [ ] Add controls for UV debug/checker preview and explicit Htex-vs-resolved-UV bake mapping where both are available.
- [ ] Surface runtime diagnostics for backend failure, invalid UVs, unsupported property type/domain, non-finite values, and generated texture upload deferrals.

## Tests
- [ ] Add UI model tests proving UV diagnostics rows are populated from runtime snapshots without direct geometry/backend ownership.
- [ ] Add command tests proving backend/regenerate/bake actions enqueue runtime commands and mark document dirty through `EditorCommandHistory`.
- [ ] Add property-list tests proving internal/connectivity properties are hidden or separated from bakeable user/algorithm results.
- [ ] Add tests proving authored-texture priority and generated-binding override choices are represented as explicit user commands.
- [ ] Add regression tests for headless/null UI snapshots where renderer or texture upload is unavailable.

## Docs
- [ ] Update `src/runtime/Editor/README.md` or `src/runtime/README.md` with the UV/bake command ownership model.
- [ ] Update `tasks/backlog/ui/README.md` if follow-up UI work is opened.
- [ ] Update `docs/architecture/graphics.md` or runtime docs if UI changes the mapping policy exposed to users.
- [ ] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.

## Acceptance criteria
- [ ] Users can see whether selected mesh UVs are authored or generated and inspect backend diagnostics.
- [ ] Users can choose a UV backend and request atlas regeneration without UI owning geometry algorithm state.
- [ ] Users can bake eligible 1D-4D mesh properties into generated textures through runtime-owned commands.
- [ ] Users can choose whether a generated texture is for visualization or for a material semantic such as albedo/normal/scalar/vector/displacement-ready output.
- [ ] The UI remains headless-safe and preserves runtime/graphics layering boundaries.

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
