---
id: RUNTIME-109
theme: none
depends_on: [ASSETIO-008, GRAPHICS-087]
maturity_target: CPUContracted
---
# RUNTIME-109 — Extensible mesh attribute texture bake pipeline

## Goal
- Generalize runtime CPU mesh texture baking so 1D, 2D, 3D, and 4D mesh algorithm results can be baked into generated texture assets over resolved UVs.

## Non-goals
- No GPU compute baking or Vulkan-only baker.
- No new UV backend; this consumes resolved UVs from `ASSETIO-008`.
- No editor UI in this task.
- No graph or point-cloud texture baking.
- No displacement tessellation or material model expansion beyond producing generated texture payloads and data-only binding requests.

## Context
- Owning subsystem/layer: `runtime` owns CPU property-to-texture baking, generated texture asset registration, dirty stamps, and material/visualization binding requests.
- `ASSETIO-006` and `GRAPHICS-087` established normal and vec3/vec4 color bakes, but the helper is still specialized around normal/color semantics.
- The desired long-term rule is that finite mesh algorithm outputs with component counts 1, 2, 3, or 4 can be visualized as textures when a resolved UV atlas exists.
- Initial domain should cover vertex and face properties. Edge/halfedge-specific baking can be a later task unless this task defines a deterministic rasterization rule for them.
- Generated bake outputs must remain CPU payloads (`AssetTexture2DPayload`) until graphics/runtime upload paths consume them; runtime must not own Vulkan resources.

## Required changes
- [ ] Introduce a generic `MeshAttributeTextureBakeRequest` with source property name, source domain, value kind, target semantic, encoder, texture size, color space, pixel format, colormap/range policy, and dirty stamp.
- [ ] Add a generic bake API that handles finite scalar float/double, label `uint32`, `glm::vec2`, `glm::vec3`, and `glm::vec4` properties where property count matches the selected mesh domain.
- [ ] Preserve existing normal and color bake helpers as wrappers over the generic API.
- [ ] Add face-domain rasterization support for constant-per-face properties; keep unsupported domains explicit in the status taxonomy.
- [ ] Add encoder policies for at least scalar colormap, linear scalar packing, vector2, vector3/normal, and RGBA color outputs.
- [ ] Add generated texture asset path/key rules for algorithm outputs so rebakes replace or reload the intended generated payload instead of minting unbounded duplicates.
- [ ] Add diagnostics for missing UVs, unsupported value type, unsupported domain, invalid range, non-finite values, degenerate UV triangles, and zero-coverage bakes.
- [ ] Add a runtime command-facing seam that later UI and algorithm tasks can call without importing graphics or asset internals.

## Tests
- [ ] Add CPU bake tests for vertex scalar, vertex vec2, vertex vec3, vertex vec4, and label properties.
- [ ] Add face-domain scalar/color tests proving each covered triangle receives the expected constant value.
- [ ] Add wrapper-regression tests proving existing normal and color bake helpers still produce byte-compatible payloads.
- [ ] Add invalid-input tests for missing resolved UVs, count mismatch, unsupported type, non-finite values, invalid range, invalid resolution, and degenerate-all-triangle UVs.
- [ ] Add generated asset key/reload tests proving rebakes do not create unbounded duplicate generated texture assets for the same entity/property/semantic.

## Docs
- [ ] Update `src/runtime/README.md` with the generic bake request/status/encoder contract.
- [ ] Update `docs/architecture/graphics.md` or runtime docs if generated texture binding policy changes.
- [ ] Update `tests/README.md` only if new labels are introduced.
- [ ] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.

## Acceptance criteria
- [ ] Runtime exposes one generic CPU bake path for 1D-4D mesh properties over resolved UVs.
- [ ] Existing generated normal and albedo/color behavior remains compatible.
- [ ] Algorithm outputs can request generated texture payloads without adding graphics, Vulkan, or UI dependencies.
- [ ] Missing or invalid UVs are treated as a bake failure with diagnostics, not silently repaired in the baker.
- [ ] Generated texture assets have stable keys suitable for later dirty-stamp rebake/reload workflows.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicAssetUnitTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeMeshAttributeTextureBake|RuntimeAssetModelSceneHandoff|RuntimeAssetImportFormatCoverage' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not allocate or mutate Vulkan/RHI resources in the CPU bake helper.
- Do not make graph or point-cloud visualization depend on mesh UV bakes.
- Do not auto-generate UVs inside the bake helper; UV resolution is upstream.
- Do not override authored material textures unless a caller explicitly requests the generated texture binding.
- Do not collapse all values to RGBA8 if the requested target semantic requires a documented linear/scalar format.

## Maturity
- Target: `CPUContracted`.
- This task closes the generic CPU bake contract. `Operational` owned by `GRAPHICS-088`.
