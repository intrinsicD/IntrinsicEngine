#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "common/gpu_scene.glsl"

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
} camera;

layout(set = 1, binding = 0) uniform sampler2D globalTextures[];

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) in vec3 vWorldNormal;
layout(location = 1) in vec2 vUv;
layout(location = 2) flat in uint vInstanceSlot;
layout(location = 3) flat in uint vEntityId;

layout(location = 0) out vec4 GBuf_Normal;
layout(location = 1) out vec4 GBuf_Albedo;
layout(location = 2) out vec4 GBuf_Material;
layout(location = 3) out uvec4 GBuf_EntityId;

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;

    GpuInstanceStaticRef instanceStatic = GpuInstanceStaticRef(scene.InstanceStaticBDA);
    GpuEntityConfigRef entityConfigs = GpuEntityConfigRef(scene.EntityConfigBDA);
    GpuMaterialSlotRef materials = GpuMaterialSlotRef(scene.MaterialBDA);

    const GpuInstanceStatic inst = instanceStatic.Data[vInstanceSlot];
    const GpuEntityConfig cfg = entityConfigs.Data[inst.ConfigSlot];
    const GpuMaterialSlot mat = materials.Data[inst.MaterialSlot];

    vec4 baseColor = mat.BaseColorFactor;
    if (mat.AlbedoID != 0u) {
        baseColor *= texture(globalTextures[nonuniformEXT(mat.AlbedoID)], vUv);
    }

    if (cfg.ColorSourceMode == 1u) {
        baseColor = cfg.UniformColor;
    }

    const vec3 n = normalize(vWorldNormal);
    GBuf_Normal = vec4(n, 0.0);
    GBuf_Albedo = baseColor;
    GBuf_Material = vec4(mat.RoughnessFactor, mat.MetallicFactor, float(cfg.ColorSourceMode), 0.0);
    GBuf_EntityId = uvec4(vEntityId, 0u, 0u, 0u);
}
