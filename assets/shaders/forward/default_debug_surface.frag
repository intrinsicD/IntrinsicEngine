#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-031A — canonical missing-material fallback fragment shader.
//
// BDA-only contract: the promoted Vulkan pipeline layout binds only the
// global bindless descriptor set at set 0, so the material slot must be
// reached through `GpuScenePushConstants::SceneTableBDA -> scene.MaterialBDA`
// rather than a `set = 3` SSBO. Push constants are visible in both stages
// (`stageFlags = VK_SHADER_STAGE_ALL`) so re-reading the scene table here
// avoids passing the 64-bit BDA through a varying.
//
// Reads the material slot from the per-instance index forwarded by the
// vertex shader and emits `BaseColorFactor`. Slot 0 carries the recorded
// `Material.DefaultDebugSurface` params (`MaterialFlags::Unlit`, purple
// `BaseColorFactor`) so any invalid material handle resolves to a visible
// missing-material surface.

#include "../common/gpu_scene.glsl"

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) flat in uint fragMaterialSlot;

layout(location = 0) out vec4 outColor;

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuMaterialSlot mat = GpuMaterialSlotRef(scene.MaterialBDA).Data[fragMaterialSlot];
    outColor = mat.BaseColorFactor;
}
