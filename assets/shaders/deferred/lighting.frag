#version 460

#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(push_constant, scalar) uniform PushConstants
{
    uint64_t SceneTableBDA;
    uint _pad0;
    uint _pad1;
} pc;

#include "common/gpu_scene.glsl"

void main()
{
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuLightRef lights = GpuLightRef(scene.LightBDA);

    vec3 accum = vec3(0.0);
    for (uint i = 0u; i < scene.LightCount; ++i)
    {
        const GpuLight light = lights.Data[i];
        const vec3 lightColor = light.Color_Intensity.xyz;
        const float intensity = light.Color_Intensity.w;
        accum += lightColor * intensity;
    }

    // Temporary debug composition: encode light accumulation.
    // The full BRDF + GBuffer integration remains in a follow-up pass.
    outColor = vec4(accum, 1.0);
}
