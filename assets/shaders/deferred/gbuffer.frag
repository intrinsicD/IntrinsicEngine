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
layout(location = 4) in float vVisualizationScalar;
layout(location = 5) in vec4 vVisualizationColor;

layout(location = 0) out vec4 GBuf_Normal;
layout(location = 1) out vec4 GBuf_Albedo;
layout(location = 2) out vec4 GBuf_Material;
layout(location = 3) out uvec4 GBuf_EntityId;

bool IsValidTextureID(uint id) {
    return id != 0u && id != 0xFFFFFFFFu;
}

vec3 ResolveSurfaceNormal(
    GpuMaterialSlot mat,
    GpuInstanceDynamic dyn,
    vec3 vertexWorldNormal,
    vec2 uv)
{
    const float normalLength = length(vertexWorldNormal);
    vec3 n = (normalLength > 1.0e-6)
        ? (vertexWorldNormal / normalLength)
        : vec3(0.0, 0.0, 1.0);

    // The Normal channel samples the baked object-space normal texture when
    // the material's per-channel source selects Texture (GRAPHICS-105). The
    // legacy ObjectSpaceNormalMap flag is honored as a transitional alias.
    const bool normalFromTexture =
        GpuMaterialChannelSource(mat, GpuMaterialChannel_Normal) == GpuAttributeSource_Texture ||
        (mat.Flags & GpuMaterialFlag_ObjectSpaceNormalMap) != 0u;
    if (!normalFromTexture || !IsValidTextureID(mat.NormalID)) {
        return n;
    }

    const vec4 normalSample = texture(globalTextures[nonuniformEXT(mat.NormalID)], uv);
    if (normalSample.a <= 1.0e-6) {
        return n;
    }

    vec3 objectNormal = normalSample.rgb * 2.0 - vec3(1.0);
    const float objectNormalLength = length(objectNormal);
    if (objectNormalLength <= 1.0e-6) {
        return n;
    }
    objectNormal /= objectNormalLength;

    const mat3 normalMatrix = transpose(inverse(mat3(dyn.Model)));
    const vec3 worldNormal = normalMatrix * objectNormal;
    const float worldNormalLength = length(worldNormal);
    return (worldNormalLength > 1.0e-6)
        ? (worldNormal / worldNormalLength)
        : n;
}

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;

    GpuInstanceStaticRef instanceStatic = GpuInstanceStaticRef(scene.InstanceStaticBDA);
    GpuInstanceDynamicRef instanceDynamic = GpuInstanceDynamicRef(scene.InstanceDynamicBDA);
    GpuEntityConfigRef entityConfigs = GpuEntityConfigRef(scene.EntityConfigBDA);
    GpuMaterialSlotRef materials = GpuMaterialSlotRef(scene.MaterialBDA);

    const GpuInstanceStatic inst = instanceStatic.Data[vInstanceSlot];
    const GpuInstanceDynamic dyn = instanceDynamic.Data[vInstanceSlot];
    const GpuEntityConfig cfg = entityConfigs.Data[inst.ConfigSlot];
    const GpuMaterialSlot mat = materials.Data[inst.MaterialSlot];

    vec4 baseColor = mat.BaseColorFactor;
    if (IsValidTextureID(mat.AlbedoID)) {
        baseColor *= texture(globalTextures[nonuniformEXT(mat.AlbedoID)], vUv);
    }
    float visualizationScalar = vVisualizationScalar;
    vec4 visualizationColor = vVisualizationColor;
    if (cfg.VisDomain == GpuVisualizationDomain_Face) {
        const uint faceId = uint(gl_PrimitiveID);
        visualizationScalar = GpuVisualizationReadScalar(cfg, faceId, cfg.ScalarRangeMin);
        visualizationColor = GpuVisualizationReadColor(cfg, faceId, baseColor);
    }
    baseColor = (cfg.ColorSourceMode == GpuColorSource_ScalarField &&
                 GpuVisualizationHasValidBindless(cfg.ColormapID))
        ? GpuResolveVisualizationColorWithColormap(
            cfg,
            visualizationScalar,
            visualizationColor,
            baseColor,
            globalTextures[nonuniformEXT(cfg.ColormapID)])
        : GpuResolveVisualizationColorFallback(
            cfg,
            visualizationColor,
            baseColor);

    const vec3 n = ResolveSurfaceNormal(mat, dyn, vWorldNormal, vUv);
    GBuf_Normal = vec4(n, 0.0);
    GBuf_Albedo = baseColor;
    GBuf_Material = vec4(mat.RoughnessFactor, mat.MetallicFactor, float(cfg.ColorSourceMode), 0.0);
    GBuf_EntityId = uvec4(vEntityId, 0u, 0u, 0u);
}
