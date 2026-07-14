---
id: GRAPHICS-087
theme: F
depends_on: [ASSETIO-006, GRAPHICS-015, RUNTIME-083]
maturity_target: CPUContracted
---
# GRAPHICS-087 — Bake vec3/vec4 vertex color properties to surface albedo textures

## Goal
- Bake any mesh vertex-domain `glm::vec3` or `glm::vec4` property into a generated texture map that can bind as the surface albedo/color source, including properties loaded from assets, computed by algorithms, or set by users.

## Non-goals
- No scalar colormap, label, isoline, vector-field, or overlay visualization rewrite.
- No GPU compute baking or Vulkan-only atlas generation.
- No editor UI for choosing the property in this slice.
- No generated UV unwrapping; existing `v:texcoord` is required for texture baking.

## Context
- Owner/layer: `runtime` translates CPU `GeometrySources` properties into generated texture payloads and material bindings; `graphics` owns shader consumption of material `AlbedoID`; `assets` remains payload storage only.
- `ASSETIO-006` adds the shared CPU bake helper and model-scene generated texture handoff pattern.
- Existing visualization adapters can expose color properties as overlay packets, but material surface coloring needs an albedo texture path so default/deferred shading uses the property as the base surface color.
- The same updateability rule as generated normal maps applies: if an algorithm writes a new `glm::vec3`/`glm::vec4` vertex property, callers should be able to rebake and re-register/reload the generated texture payload.

## Required changes
- [x] Extend the runtime texture bake helper to accept named `glm::vec3` and `glm::vec4` vertex properties.
- [x] Encode vec3 properties as RGB with alpha 255 and vec4 properties as RGBA, clamping to `[0, 1]`.
- [x] Allow model-scene handoff to create a generated albedo texture from a configured vertex color property when no authored albedo texture is present.
- [x] Keep authored base-color textures higher priority than generated color-property textures.
- [x] Ensure shader color resolution samples `MaterialParams::AlbedoID` and base color factor after generated binding resolution.

## Tests
- [x] Add runtime contract coverage for vec3 and vec4 property bake payload bytes/metadata.
- [x] Add model-scene handoff coverage proving generated albedo texture binding from a mesh vertex color property.
- [x] Add shader/material contract coverage proving albedo texture IDs remain part of forward/deferred shading paths.

## Docs
- [x] Update `src/runtime/README.md` and `src/graphics/renderer/README.md` if shader/material consumption behavior changes.
- [x] Retire/update task notes and regenerate `tasks/SESSION-BRIEF.md`.
- [x] Refresh generated module inventory if a module surface changes.

## Acceptance criteria
- [x] Any finite mesh vertex `glm::vec3` or `glm::vec4` property matching vertex count can bake to an RGBA8 generated texture payload through the shared runtime helper.
- [x] Generated color-property textures bind as material albedo only when an authored albedo texture is absent or the caller explicitly requests the generated source.
- [x] A later algorithm/user property update can rebake through the same helper without importing graphics or Vulkan.
- [x] Focused runtime/graphics contract tests pass under the CPU gate.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeAssetModelSceneHandoff|RuntimeMeshAttributeTextureBake|RendererFrameLifecycle|SurfacePass' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

2026-06-12 results:
- Commit: pending local generated texture/impostor commit.
- `cmake --preset ci` passed before focused and broad verification.
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGraphicsContractTests IntrinsicAssetUnitTests` passed.
- Focused generated texture/impostor/material CTest set passed 43/43.
- `cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests` passed.
- Focused point-impostor regression set passed 3/3.
- `cmake --build --preset ci --target IntrinsicTests` passed.
- Default CPU-supported CTest gate passed 3008/3008.
- Direct `glslc` compile of edited shader sources passed.
- Structural checks passed: task policy, doc links, layering, test layout, docs-sync diff mode, PR contract, and `git diff --check`. Root hygiene remains warning-mode with existing `imgui.ini`.

## Forbidden changes
- Replacing existing visualization packet behavior.
- Moving property/algorithm execution into graphics.
- Treating non-finite or count-mismatched properties as bakeable.
- Overriding authored material textures by default.

## Maturity
- Closed at `CPUContracted`; no `Operational` follow-up is owed by this task because the runtime bake/material binding contract is CPU-testable.
