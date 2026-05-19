#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-072 Slice A — default-debug GBuffer fragment shader.
//
// Pairs with `forward/default_debug_surface.vert` so the deferred GBuffer
// pipeline reuses the same GpuScene-aware vertex stage as the forward
// default-debug-surface pipeline. The vertex shader forwards
// `fragMaterialSlot` at location 0; this fragment reads the material slot
// from `GpuScenePushConstants::SceneTableBDA -> scene.MaterialBDA` and
// writes three GBuffer color attachments matching the recipe's deferred
// resource declarations:
//   - location 0 -> SceneNormal (RGBA16F): unit-Z fallback normal until a
//     dedicated lit GBuffer shader pair lands.
//   - location 1 -> Albedo (RGBA8): material BaseColorFactor.
//   - location 2 -> Material0 (RGBA16F): metallic/roughness packed plus
//     reserved channels.
//
// Push-constant compatibility: the block below MUST mirror
// `RHI::GpuScenePushConstants` exactly. `DeferredGBufferPass::Execute`
// pushes those bytes via `cmd.PushConstants(&pc, sizeof(pc))`. Reusing the
// legacy `assets/shaders/surface_gbuffer.frag` (declares `mat4 Model +
// PtrPositions + ...`) is a known footgun — see the "Shader push-constant
// compatibility policy" section in `src/graphics/renderer/README.md`.

#include "../common/gpu_scene.glsl"

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) flat in uint fragMaterialSlot;

layout(location = 0) out vec4 SceneNormal;
layout(location = 1) out vec4 Albedo;
layout(location = 2) out vec4 Material0;

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuMaterialSlot mat = GpuMaterialSlotRef(scene.MaterialBDA).Data[fragMaterialSlot];

    SceneNormal = vec4(0.0, 0.0, 1.0, 0.0);
    Albedo = mat.BaseColorFactor;
    Material0 = vec4(mat.RoughnessFactor, mat.MetallicFactor, 0.0, 0.0);
}
