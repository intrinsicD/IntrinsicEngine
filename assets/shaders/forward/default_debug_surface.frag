#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

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

layout(set = 0, binding = 0) uniform sampler2D globalTextures[];

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) flat in uint fragMaterialSlot;
layout(location = 1) in vec2 fragUv;

layout(location = 0) out vec4 outColor;

bool IsValidTextureID(uint id) {
    return id != 0u && id != 0xFFFFFFFFu;
}

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuMaterialSlot mat = GpuMaterialSlotRef(scene.MaterialBDA).Data[fragMaterialSlot];
    vec4 baseColor = mat.BaseColorFactor;
    if (IsValidTextureID(mat.AlbedoID)) {
        baseColor *= texture(globalTextures[nonuniformEXT(mat.AlbedoID)], fragUv);
    }

    vec3 sampledNormal = vec3(0.0, 0.0, 1.0);
    if (IsValidTextureID(mat.NormalID)) {
        vec3 normalTex = texture(globalTextures[nonuniformEXT(mat.NormalID)], fragUv).xyz * 2.0 - 1.0;
        float normalTexLen = length(normalTex);
        if (normalTexLen > 1.0e-6) {
            sampledNormal = normalTex / normalTexLen;
        }
    }

    const float positiveViewZ = GpuPositiveViewZFromDeviceDepth(gl_FragCoord.z, scene);
    const vec3 lightDebug = GpuAccumulateClusteredSceneLightsDebug(
        scene,
        gl_FragCoord.xy,
        positiveViewZ);
    const vec3 debugLightDir = normalize(vec3(-0.35, 0.45, 0.82));
    const float normalShade = mix(0.35, 1.0, max(dot(sampledNormal, debugLightDir), 0.0));
    const float debugLift = min(length(lightDebug) * 0.05, 0.25);
    outColor = vec4(baseColor.rgb * normalShade * (1.0 + debugLift), baseColor.a);
}
