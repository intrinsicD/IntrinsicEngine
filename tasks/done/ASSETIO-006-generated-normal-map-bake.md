---
id: ASSETIO-006
theme: F
depends_on: [ASSETIO-001, BUG-041, RUNTIME-085, GRAPHICS-015]
maturity_target: CPUContracted
---
# ASSETIO-006 — Generated normal-map bake from mesh vertex normals

## Goal
- Generate a runtime-owned normal-map texture asset from mesh-domain `v:normal` when a loaded model material has no authored normal texture, and keep the same bake function callable for later algorithm-produced normal updates.

## Non-goals
- No new UV unwrapping, tangent-space generation, smoothing-group interpretation, or hard-edge seam splitting.
- No live `AssetService` dependency from `assets`, `graphics`, or Vulkan layers.
- No broad PBR feature-completeness work beyond consuming the existing `MaterialParams::NormalID` slot.
- No editor UI for selecting the generated normal map source.

## Context
- Owner/layer: `runtime` owns model-scene materialization, generated texture asset registration, upload requests, and material texture binding records; `assets` stays CPU-only; `graphics` consumes resolved bindless material slots only.
- `BUG-041` guarantees runtime materialization exposes `v:normal` on mesh-domain `GeometrySources`.
- The promoted material handoff already creates child `AssetTexture2DPayload` assets for embedded model textures and resolves `MaterialTextureAssetBindings` through `GpuAssetCache`.
- A valid texture bake needs an existing `v:texcoord` property. When a mesh has no texture coordinates, the task must fail closed and keep shading on vertex/geometry normals rather than inventing an invalid atlas.

## Required changes
- [x] Add a runtime CPU bake helper that turns `v:normal` + `v:texcoord` into an `AssetTexture2DPayload` with generated metadata.
- [x] Preserve decoded `v:texcoord` from model/mesh payload materialization into mesh-domain `GeometrySources`.
- [x] Extend model-scene handoff so materials without `NormalTexture` receive a generated normal texture asset when at least one bound primitive has bakeable normals.
- [x] Request upload and re-resolve the generated normal texture through the existing texture handoff/material binding path.
- [x] Keep the bake helper callable for a later algorithm/user update by accepting a generic mesh view + property name rather than only a model-scene payload.

## Tests
- [x] Add runtime contract coverage for successful normal texture bake payload bytes/metadata from `v:normal` + `v:texcoord`.
- [x] Add model-scene handoff coverage proving a material with no source normal texture records a generated normal texture binding and upload request.
- [x] Add fail-closed coverage for missing `v:texcoord`.

## Docs
- [x] Update `src/runtime/README.md` with generated texture bake ownership and UV prerequisite.
- [x] Update task/session brief state after opening/retiring.
- [x] Refresh generated module inventory if the runtime module surface changes.

## Acceptance criteria
- [x] Loaded model-scene materials missing a normal texture can receive a generated texture asset when their mesh primitive has matching `v:normal` and `v:texcoord` vertex properties.
- [x] The generated texture asset uses linear RGBA8 payload data with normals encoded into RGB and alpha set to 255.
- [x] Material texture bindings use the generated normal texture for shading through the existing `NormalID` slot.
- [x] Recomputing normals can call the same runtime bake helper to produce an updated payload without touching graphics or Vulkan ownership.

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
- Moving asset-service or texture-upload ownership below `runtime`.
- Treating a mesh without texture coordinates as successfully baked.
- Replacing authored normal textures with generated ones.
- Changing unrelated model import formats or parser behavior.

## Maturity
- Closed at `CPUContracted`; no `Operational` follow-up is owed by this task because it wires the CPU/runtime/material binding contract. GPU-visible proof can be covered by a later Vulkan smoke if needed.
