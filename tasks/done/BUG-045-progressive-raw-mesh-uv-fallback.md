---
id: BUG-045
theme: F
depends_on: [RUNTIME-114, BUG-044]
maturity_target: CPUContracted
---
# BUG-045 — Progressive raw mesh surface UV fallback

## Completion
- Completed: 2026-06-17. Commit/PR: pending local commit.
- Maturity: `CPUContracted`.
- Root cause: progressive direct/drop mesh import published raw decoded mesh
  entities before UV atlas post-processing, but `Runtime.MeshGeometryPacker`
  still failed closed when `v:texcoord` was missing, count-mismatched, or
  non-finite. No-UV OBJ drops therefore created/selectable entities while the
  surface upload failed until deferred UV work completed.
- Fix summary: raw mesh surface packing now uses zero U/V values when source
  texcoords are absent or invalid, while runtime extraction records the
  missing/non-finite UV fallback counters. ECS geometry remains unchanged, so
  UV atlas generation and UV-dependent texture bakes still wait for real
  texcoords.

## Goal
- Make raw imported mesh surfaces renderable immediately when UV enrichment is
  still pending, including OBJ files dropped through the platform event path.

## Non-goals
- No renderer-side UV atlas generation.
- No texture-bake fallback without real UVs.
- No Vulkan-only behavior change.
- No changes to platform window close semantics without a failing close-path
  reproduction.

## Context
- Owning subsystem/layer: `runtime` mesh packing and render extraction.
- `RUNTIME-114` requires raw imported geometry to publish before UV, normal,
  bake, upload, and bind enrichment completes.
- `BUG-044` split direct mesh post-processing onto `StreamingExecutor`, which
  exposed the older packer requirement that mesh surface uploads needed
  count-matched finite `v:texcoord`.
- The packer may synthesize GPU vertex U/V defaults for presentation, but must
  not write placeholder texcoords into authoritative ECS `GeometrySources`.

## Required changes
- [x] Change `Runtime.MeshGeometryPacker` so missing, mismatched, or non-finite
      mesh texcoords no longer prevent a surface upload.
- [x] Preserve extraction diagnostics for missing and non-finite UV fallback
      use.
- [x] Keep topology/position failures fail-closed.
- [x] Add a platform-drop no-UV OBJ regression that proves raw surface upload
      happens before deferred post-processing applies.

## Tests
- [x] Update mesh packer tests for missing, mismatched, and non-finite
      texcoords to expect default GPU U/V values.
- [x] Update mesh extraction tests so missing/non-finite texcoords upload a
      surface and increment the corresponding fallback counter.
- [x] Preserve direct import, dropped-file, progressive model-scene, and close
      path regressions.

## Docs
- [x] Update the mesh packer module comments to describe zero-U/V fallback.
- [x] No generated API inventory refresh was required because exported type
      signatures did not change.

## Acceptance criteria
- [x] A dropped OBJ with no authored UVs creates a mesh entity and uploads one
      raw surface in the frame where the import event becomes visible.
- [x] Missing/non-finite UVs remain observable through extraction counters.
- [x] UV-dependent texture bakes still require real resolved UVs.
- [x] Existing window close regressions remain green.
- [x] Fix stays inside runtime and does not add graphics-to-runtime ownership.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests -j 30
ctest --test-dir build/ci --output-on-failure -R 'MeshGeometryPackerTest\.(MissingTexcoordsUseDefaultUvsEvenWhenNormalsExist|MismatchedTexcoordCountUsesDefaultUvs|NonFiniteTexcoordUsesDefaultUvForInvalidVertex)|MeshGeometryExtraction\.(MissingTexcoordsUploadsWithDefaultUvFallback|NonFiniteTexcoordsUploadWithDefaultUvFallback)|SandboxEditorUi\.(PlatformDropNoUvObjUploadsRawSurfaceBeforeDeferredPostProcess|PlatformDropEventImportsObjMeshSelectsItAndEnablesRenderComponents|PlatformCloseEventStopsEngineRunState)|ImGuiAdapterEngineWiring\.RunNormalizesNativeClose' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'RuntimeAssetImportFormatCoverage\.DirectObjImport|SandboxEditorUi\.(Dropped|PlatformDrop|PlatformClose|EngineImportFacadeMaterializes(NonManifoldObj|ObjWithoutAuthoredTexcoords)|DuplicateDroppedGeometryImport|DroppedGeometryAssetReimport)|RuntimeAssetModelSceneHandoff\.(ProgressiveRawGeometryFirstQueuesUvNormalAndBakeJobs|MissingTexcoordsReceiveGeneratedAtlasBeforeGeneratedMaterialTextures)|ImGuiAdapterEngineWiring|RuntimeFrameLoop|RuntimeEngineLayering' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Do not write placeholder UV properties into ECS geometry to satisfy the
  renderer.
- Do not allow texture baking to proceed without real resolved UVs.
- Do not move progressive import, atlas, or bake ownership into graphics.
- Do not claim a close-button fix without a close-path red gate.

## Maturity
- Target: `CPUContracted`.
- This bug closes a backend-neutral runtime extraction contract. No
  `Operational` follow-up is owed because the fix does not alter Vulkan/RHI
  ownership or backend-specific command recording.
