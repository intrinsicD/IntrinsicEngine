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
- Status: not started.
- Owner/layer: `graphics/renderer` for the material registration + pipeline; `assets/shaders` for the SPIR-V output.
- Planning parent: [`tasks/done/GRAPHICS-031-default-debug-surface-material.md`](../../done/GRAPHICS-031-default-debug-surface-material.md), Decisions 4, 5, 6, 7, 9, 11. Recorded as Impl-A in the parent's Required changes.
- Vertex format: position `vec3` (BDA-driven from `GpuWorld` position buffer) + optional packed RGBA8 `uint32` (color buffer); matches `GRAPHICS-030A` Triangle packer output.
- Pipeline state: `CullMode = Back`, `DepthCompareOp = Less` (or `Equal` after `Pass.DepthPrepass`), `BlendEnabled = false`, `PolygonMode = Fill`, `TriangleList`, MSAA samples = 1, dynamic state `{Viewport, Scissor}`. Push-constants: existing scene-table BDA; descriptors: `MaterialBuffer` SSBO at `set = 3, binding = 0`.
- Performance bound: ≤ 32 vertex / ≤ 16 fragment SPIR-V instructions.

## Required changes
- [ ] Author `assets/shaders/forward/default_debug_surface.vert` and `default_debug_surface.frag`. The vertex shader reads scene-table BDA push constants, fetches position and packed RGBA8 vertex color, transforms by view-projection, and forwards interpolated color. The fragment shader reads `BaseColorFactor` from `MaterialBuffer[slot]` (`set = 3, binding = 0`) and outputs `vec4(baseColor.rgb * vertexColor.rgb, baseColor.a)`. No textures, no normals, no lighting.
- [ ] Add `kMaterialTypeID_DefaultDebugSurface = 2u` to `Graphics.Material` (next to existing material type IDs).
- [ ] Register `"Material.DefaultDebugSurface"` `MaterialTypeDesc` in `MaterialSystem::Initialize(...)` with `MaterialFlags::Unlit`, the recorded `BaseColorFactor = {0.55f, 0.20f, 0.85f, 1.0f}`, and pipeline-state references.
- [ ] Populate material slot 0 with the registered params at `MaterialSystem::Initialize` (replacing the existing StandardPBR opaque-white slot-0 placeholder).
- [ ] Create the default-debug-surface graphics pipeline once at renderer init via `PipelineManager::Create(PipelineDesc{...})` and store the lease alongside the existing `m_DepthPrepassPipelineLease` in `NullRenderer`. Republish the same handle byte-identical from `RebuildOperationalResources()`.
- [ ] Build-wire shader compilation so the SPIR-V binaries land where `RHI::PipelineDesc::VertexShaderPath = "assets/shaders/forward/default_debug_surface.vert"` resolves at runtime. (Wired through the existing `intrinsic_add_glsl_shaders()` invocation; `BUILD-001` adds that invocation to `ExtrinsicSandbox`.)

## Tests
- [ ] `unit;graphics` test: `MaterialLayoutContract::DefaultSlot == kDefaultMaterialSlotIndex`.
- [ ] `unit;graphics` test: `MaterialSystem` exposes a registered type with name `"Material.DefaultDebugSurface"` and `MaterialTypeID == kMaterialTypeID_DefaultDebugSurface`.
- [ ] `unit;graphics` test: slot 0 `BaseColorFactor` is byte-equal to `{0.55, 0.20, 0.85, 1.0}` after `Initialize()`.
- [ ] `unit;graphics` test: pipeline lease survives `RebuildGpuResources()` with identical RHI handle.
- [ ] No `gpu`/`vulkan` test in this slice; the pipeline is created against the operational-device gate but never recorded yet.

## Docs
- [ ] Update `src/graphics/renderer/README.md` to record the new shader file paths and the slot-0 registration.
- [ ] Refresh `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [ ] Slot 0 carries `Material.DefaultDebugSurface` after `Initialize()`.
- [ ] The single graphics pipeline exists in the `PipelineManager` lease pool and references both shader paths.
- [ ] No frame-recipe change, no fallback substitution change.
- [ ] Layering checks pass; no new dependency edges.

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

## Next verification step
- Author the shaders, register the slot, create the pipeline, run the verification commands above.
