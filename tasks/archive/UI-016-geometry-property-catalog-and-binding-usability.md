---
id: UI-016
theme: F
depends_on: [UI-015, RUNTIME-111, RUNTIME-113]
maturity_target: CPUContracted
---
# UI-016 — Geometry property catalog and binding usability

## Completion
- Retired on 2026-06-17 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: `Extrinsic.Runtime.SandboxEditorUi` now exposes a selected-entity
  property catalog for mesh, graph, and point-cloud domains, selected-value
  previews for supported scalar/label/vector rows, and compatible binding
  targets that keep incompatible choices visible with deterministic reasons.
- Evidence: focused runtime/UI coverage passed in the combined
  `SandboxEditor|EditorCommandHistory|Uv|TextureBake|SelectedMeshTextureBake|MeshAttributeTextureBake|DerivedJob|Progressive|PropertyCatalog`
  CTest run.

## Slice plan
- **Slice A. Property catalog model.** Add data-only property-domain/value/category DTOs, build selected-entity catalogs for mesh vertex/edge/halfedge/face, graph vertex/edge, and point-cloud point domains, and keep unknown property types visible with unsupported diagnostics.
- **Slice B. Value preview and filtering.** Add selected primitive-aware value preview for scalar, label, vec2, vec3, and vec4 rows plus grouping flags for bindable/internal/connectivity/generated/unsupported properties.
- **Slice C. Binding chooser reuse.** Correlate catalog rows with progressive property option descriptors so render/visualization slot pickers can show compatible choices first and disabled reasons for mismatches.
- **Slice D. Tests/docs/retirement.** Add headless UI model tests, update runtime/editor docs, refresh generated inventories, retire the task at `CPUContracted`, and leave bake/UV controls to `RUNTIME-115`/`UI-014`.

## Goal
- Add a headless-safe sandbox editor property catalog and compatible binding chooser for selected mesh, graph, and point-cloud entities so users can inspect all geometry properties and choose compatible properties for render/visualization slots.

## Non-goals
- No UI-owned geometry mutation, texture baking, asset loading, worker scheduling, or graphics resource ownership.
- No Vulkan-specific proof.
- No graph or point-cloud texture baking.
- No implicit type conversion, swizzling, normalization, or scalar/vector reinterpretation beyond explicitly documented compatible value kinds.
- No public geometry reflection expansion unless the existing runtime/UI probes cannot list unknown properties at all.

## Context
- Owning subsystem/layer: `runtime` editor UI consumes data-only geometry/runtime snapshots and emits runtime-owned commands. UI must not own algorithms, asset lifecycle, or renderer state.
- `UI-015` added the progressive render-data inspector with compatible/incompatible property pickers for presentation slots; this task broadens the usability model to a first-class property catalog.
- `RUNTIME-111` and `RUNTIME-113` provide descriptor/property compatibility and extraction contracts for mesh, graph, and point-cloud presentation slots.
- `experimental/framework24/lib_bcg_viewer` is the usability reference: per-domain property browsers, property metadata/value preview, dimension-filtered selectors, and explicit bound-state rows. This task should borrow that interaction model without importing OpenGL/ImGui ownership patterns.
- Current geometry `PropertySet` enumeration exposes names and typed lookups, but not a complete public erased-type metadata record. The first implementation should list all property names, mark known supported value kinds when detected, and keep unknown types visible but unsupported.

## Required changes
- [x] Add a selected-entity property catalog model under `Extrinsic.Runtime.SandboxEditorUi` that enumerates mesh vertex/edge/halfedge/face, graph vertex/edge, and point-cloud point properties.
- [x] Include internal/connectivity/canonical properties in the catalog instead of filtering them out like visualization presets do today.
- [x] Add property row metadata for domain, property name, element count, known value kind, component count, support status, category, and diagnostic reason when unsupported.
- [x] Add selected-primitive/property value preview for supported scalar, label, vec2, vec3, and vec4 properties without storing raw property pointers in UI state.
- [x] Add filter/grouping data for all, bindable, internal/connectivity, generated/algorithm, and unsupported properties.
- [x] Add a compatible binding chooser model that reuses runtime descriptor rules and lists compatible choices first while keeping incompatible choices visible with deterministic reasons.
- [x] Document the initial compatibility map for scalar float/double, label `uint32`, vec2, vec3, and vec4 properties across visualization and progressive presentation targets.
- [x] Keep property identity descriptor-based: domain, property name, value kind, element count/generation where available; never persist raw storage addresses in UI state.

## Tests
- [x] Add headless UI model tests proving mesh vertex/edge/halfedge/face properties are listed, including canonical and connectivity rows such as positions, edge endpoints, halfedge topology, and face halfedge links.
- [x] Add graph and point-cloud property catalog tests proving their point/edge/property domains appear independently.
- [x] Add tests proving visualization-ineligible internal/connectivity properties are visible in the catalog but not silently promoted to visualization presets.
- [x] Add known-type detection and unknown-type visibility tests.
- [x] Add selected-value preview tests for scalar, label, vec2, vec3, and vec4 properties.
- [x] Add compatible/incompatible binding chooser tests covering dimension matches, mismatches, missing domains, unsupported types, and deterministic disabled reasons.

## Docs
- [x] Update `src/runtime/Editor/README.md` or `src/runtime/README.md` with the property catalog ownership model and compatibility rules.
- [x] Update `tasks/backlog/ui/README.md` if follow-up UI tasks are opened or re-gated.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.

## Acceptance criteria
- [x] Users can inspect all selected mesh, graph, and point-cloud properties through a data-only UI model, including properties that are not safe visualization candidates.
- [x] Users can see which properties are bindable for each exposed target and why incompatible properties are disabled.
- [x] Supported property values can be previewed for the current selection without UI storing raw property pointers.
- [x] Unknown property types remain visible with clear unsupported diagnostics.
- [x] The default CPU-supported CTest gate verifies the property catalog and binding chooser contract.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditor|PropertyCatalog|PropertyPicker|Progressive|Visualization' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not add ImGui, platform, graphics, or asset-service dependencies below their owning layers.
- Do not hide incompatible property choices; show disabled choices with reasons.
- Do not mutate geometry properties from the catalog in this task.
- Do not add arbitrary swizzle/cast behavior just to make a property appear compatible.
- Do not implement texture baking or UV regeneration in this task.

## Maturity
- Target: `CPUContracted`.
- This UI task closes the property catalog and binding-chooser data-model contract; no `Operational` follow-up is owed.
