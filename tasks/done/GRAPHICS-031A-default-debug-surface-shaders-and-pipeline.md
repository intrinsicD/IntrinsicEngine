# GRAPHICS-031A — Default debug surface shaders and pipeline

## Goal
- Author the canonical missing-material fallback declared by `GRAPHICS-031`: produce `assets/shaders/forward/default_debug_surface.vert/frag`, register `kMaterialTypeID_DefaultDebugSurface = 2u` in `Graphics.Material`, register the `"Material.DefaultDebugSurface"` `MaterialTypeDesc` at `MaterialSystem::Initialize()`, populate slot 0 (`kDefaultMaterialSlotIndex = 0u`) with the recorded params, and create the single graphics pipeline at renderer init (republished byte-identical by `RebuildOperationalResources()`).

## Non-goals
- No fallback substitution wiring at the renderer span-copy step (that is `GRAPHICS-031B`).
- No `MaterialSystemDiagnostics` counters (`MissingMaterialFallbackCount` / `InvalidMaterialSlotCount` / `DefaultDebugSurfaceUses` are added by `GRAPHICS-031B`).
- No additional debug variants (`Wireframe`/`Line`/`Point`/`Normals`/`UVs`/`Depth`/`InstanceId` are `GRAPHICS-031C`/follow-ups).
- No frame-recipe change (the `MinimalDebugSurface` recipe is `GRAPHICS-032A`).
- No extraction wiring or runtime change.

## Context
- Status: done.
- Owner/agent: Claude.
- Branch: `claude/setup-agentic-workflow-m5W12`.
- Owner/layer: `graphics/renderer` for the material registration + pipeline; `assets/shaders` for the SPIR-V output.
- Planning parent: [`tasks/done/GRAPHICS-031-default-debug-surface-material.md`](GRAPHICS-031-default-debug-surface-material.md), Decisions 4, 5, 6, 7, 9, 11. Recorded as Impl-A in the parent's Required changes.
- Vertex format: position `vec3` (BDA-driven from `GpuWorld` position buffer) + optional packed RGBA8 `uint32` (color buffer); matches `GRAPHICS-030A` Triangle packer output.
- Pipeline state: `CullMode = Back`, `DepthCompareOp = Less` (or `Equal` after `Pass.DepthPrepass`), `BlendEnabled = false`, `PolygonMode = Fill`, `TriangleList`, MSAA samples = 1, dynamic state `{Viewport, Scissor}`. Push-constants: existing scene-table BDA; descriptors: `MaterialBuffer` SSBO at `set = 3, binding = 0`.
- Performance bound: ≤ 32 vertex / ≤ 16 fragment SPIR-V instructions.

## Required changes
- [x] Author `assets/shaders/forward/default_debug_surface.vert` and `default_debug_surface.frag`. The vertex shader reads scene-table BDA push constants, fetches position (procedural vertex layout `{vec3 pos, vec2 uv}` matching `GRAPHICS-030A`), transforms by view-projection, and forwards a default vertex colour (`vec4(1.0)` until the optional packed RGBA8 buffer lands). The fragment shader reads `BaseColorFactor` from `MaterialBuffer[slot]` (`set = 3, binding = 0`) and outputs `vec4(baseColor.rgb * vertexColor.rgb, baseColor.a)`. No textures, no normals, no lighting.
- [x] Add `kMaterialTypeID_DefaultDebugSurface = 2u` plus the `kMaterialTypeName_*` string constants and the `kDefaultDebugSurfaceBaseColor` array to `Graphics.Material` (next to existing material type IDs).
- [x] Register `"Material.DefaultDebugSurface"` `MaterialTypeDesc` in `MaterialSystem::Initialize(...)` with `MaterialFlags::Unlit` and the recorded `BaseColorFactor = {0.55, 0.20, 0.85, 1.0}`. The same `Initialize()` also registers `StandardPBR` (TypeID 0) and `SciVis` (TypeID 1) so all three well-known TypeIDs are reserved before any subsystem-specific registration runs; `VisualizationSyncSystem::Initialize()` now looks up the registered SciVis handle via `MaterialSystem::FindType(kMaterialTypeName_SciVis)` instead of re-registering it.
- [x] Populate material slot 0 with the registered DefaultDebugSurface params (`Unlit`, purple `BaseColorFactor`) at `MaterialSystem::Initialize` (replacing the existing StandardPBR opaque-white slot-0 placeholder).
- [x] Create the default-debug-surface graphics pipeline once at renderer init via `PipelineManager::Create(BuildDefaultDebugSurfacePipelineDesc())` and store the lease (`m_DefaultDebugSurfacePipelineLease`) alongside the existing `m_DepthPrepassPipelineLease` in `NullRenderer`. The descriptor is rebuilt byte-identical from the same static `BuildDefaultDebugSurfacePipelineDesc()` so `RebuildOperationalResources()` republishes the same pipeline state.
- [x] Build-wire shader compilation so the SPIR-V binaries land where `RHI::PipelineDesc::VertexShaderPath = "assets/shaders/forward/default_debug_surface.vert"` resolves at runtime. (No build-system edits required: `intrinsic_add_glsl_shaders()` already `GLOB_RECURSE`s over `assets/shaders/**/*.vert/*.frag/*.comp`, so the new pair is picked up automatically by `ExtrinsicSandbox_Shaders` once `BUILD-001` invocation runs.)

## Tests
- [x] `unit;graphics` test: `MaterialLayoutContract::DefaultSlot == kDefaultMaterialSlotIndex` (existing `GraphicsMaterialSystem.CanonicalLayoutContractMatchesRhiSlot`).
- [x] `unit;graphics` test: `MaterialSystem` exposes the three built-in types with the well-known TypeIDs (`GraphicsMaterialSystem.RegistersDefaultDebugSurfaceWithStableTypeId`).
- [x] `unit;graphics` test: slot 0 `BaseColorFactor` is byte-equal to `{0.55, 0.20, 0.85, 1.0}` and carries `MaterialFlags::Unlit` after `Initialize()` (`GraphicsMaterialSystem.DefaultSlotCarriesDefaultDebugSurfaceParams`).
- [x] `contract;graphics` test: pipeline lease survives `RebuildOperationalResources()` with the same byte-identical `PipelineDesc` (`RendererFrameLifecycle.DefaultDebugSurfacePipelineSurvivesOperationalRebuild`).
- [x] No `gpu`/`vulkan` test in this slice; the pipeline is created against the operational-device gate but never recorded yet.

## Docs
- [x] Update `src/graphics/renderer/README.md` to record the GRAPHICS-031A implementation landing (shader paths, slot-0 registration, byte-identical republish).
- [x] Refresh `docs/api/generated/module_inventory.md` if module surfaces change (no diff after this slice; constants added to existing exports do not appear in the module-level inventory).

## Acceptance criteria
- [x] Slot 0 carries `Material.DefaultDebugSurface` after `Initialize()`.
- [x] The single graphics pipeline exists in the `PipelineManager` lease pool and references both shader paths.
- [x] No frame-recipe change, no fallback substitution change.
- [x] Layering checks pass; no new dependency edges.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicGraphicsUnitTests ExtrinsicSandbox_Shaders
ctest --test-dir build/ci --output-on-failure -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding fallback substitution logic (reserved for `GRAPHICS-031B`).
- Adding new `MaterialSystemDiagnostics` counters in this slice.
- Adding lighting, normal, or texture sampling to the shader pair.
- Adding new debug-material variants.
- Mixing mechanical file moves with semantic refactors.

## Completion
- Completed: 2026-05-13.
- Commit reference: `886b197` ("GRAPHICS-031A: author default debug surface shaders and pipeline") on branch `claude/setup-agentic-workflow-m5W12`.
- Verification (all run in this session against the configured `ci` preset):
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — 1837/1838 pass; the only failure was `CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering`, which is a pre-existing flaky scheduler-ordering test (`tests/unit/core/Test.Core.TaskGraphLegacy.cpp`) unrelated to this slice. Reran with `--repeat until-pass:3 -R "CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering"` → 1/1 PASS.
  - `python3 tools/agents/check_task_policy.py --root . --strict` — 203 task files validated, 0 findings.
  - `python3 tools/repo/check_layering.py --root src --strict` — clean (no new allowlist entries; no new dependency edges).
  - `python3 tools/repo/check_test_layout.py --root . --strict` — clean.
  - `python3 tools/docs/check_doc_links.py --root .` — clean.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` — no diff against the committed inventory.
- Notes:
  - SciVis material-type registration moved from `VisualizationSyncSystem::Initialize()` into `MaterialSystem::Initialize()` so all three well-known TypeIDs (StandardPBR=0, SciVis=1, DefaultDebugSurface=2) are reserved before any subsystem-specific registration runs. `VisualizationSyncSystem::Initialize()` now uses `MaterialSystem::FindType(kMaterialTypeName_SciVis)` and asserts the well-known TypeID. The change keeps the registration order invariant load-bearing for `kMaterialTypeID_SciVis = 1`.
  - The procedural triangle packer (GRAPHICS-030A) currently emits `{vec3 pos, vec2 uv}` (20 bytes/vertex) and no separate packed RGBA8 vertex-colour buffer. The default-debug-surface vertex shader reads positions at the actual 20-byte stride via a private `ProceduralVertex` buffer reference and forwards `vec4(1.0)` as the default vertex colour until an optional packed colour buffer is added by a follow-up. Per task Non-goals, no packer changes were made.
  - `IRenderer::GetDefaultDebugSurfacePipeline()` / `GetDefaultDebugSurfacePipelineDesc()` are added as accessors so the contract test can verify the lease and byte-identical descriptor survive `RebuildOperationalResources()`. The pipeline is not yet bound by any pass; `GRAPHICS-031B` will own substitution + the three new `MaterialSystemDiagnostics` counters.
  - Slot 0 now carries `MaterialFlags::Unlit` and the purple `BaseColorFactor` in place of the previous opaque-white StandardPBR default. Tests that previously expected `RegisteredTypeCount == 1` were updated to expect 3 (the three built-in types). No other tests depended on slot 0's pre-031A colour or flags.

## Follow-up — operational-pipeline alignment (post-merge fix on PR #817)
- The initial implementation commit (`886b197`) authored the shader pair with a `set = 0, binding = 0` camera UBO and a `set = 3, binding = 0` material SSBO and pointed `PipelineDesc::VertexShaderPath`/`FragmentShaderPath` at the raw GLSL sources. Both choices are incompatible with the promoted Vulkan backend:
  - `VulkanDevice::CreatePipeline()` reads `PipelineDesc::*ShaderPath` verbatim as a SPIR-V binary via `ReadSpirvFile()`; `intrinsic_add_glsl_shaders()` emits `<runtime>/shaders/<relative>.spv`.
  - The global Vulkan pipeline layout is created with `setLayoutCount = 1` (the bindless heap layout at set 0) and command recording binds only `m_BindlessHeap->GetSet()` — any `set = 0` UBO would alias the bindless set and `set = 3` references an unbound set.
- Follow-up commit on the same branch re-authors the shader pair to match the existing `depth_prepass.vert` BDA-only contract (positions, instances, geometry records, and material slots all reached via `pc.SceneTableBDA` + `buffer_reference` pointers; vertex transform lands in `dyn.Model`-space until a camera BDA contract is wired through the scene table) and pre-resolves `VertexShaderPath`/`FragmentShaderPath` through `Core::Filesystem::GetShaderPath("shaders/forward/default_debug_surface.{vert,frag}.spv")` so the operational Vulkan pipeline-creation path can read SPIR-V directly. The contract test now verifies the resolved paths end with the canonical SPV filenames so it stays meaningful whether or not the SPV artifacts are present in the test sandbox.
- The byte-identical republish across `RebuildOperationalResources()` invariant is preserved because `GetShaderPath` is deterministic for a stable filesystem state.
