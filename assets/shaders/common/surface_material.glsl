// Shared material sampling for surface fragment stages. The including entry
// point owns its globalTextures descriptor binding and all varying/output ABI.
#ifndef INTRINSIC_SURFACE_MATERIAL_GLSL
#define INTRINSIC_SURFACE_MATERIAL_GLSL

#include "gpu_scene.glsl"
#include "property_texture_normal.glsl"

bool IsValidTextureID(uint id) {
    return id != 0u && id != 0xFFFFFFFFu;
}

vec3 ResolveSurfaceNormal(
    GpuSceneTable scene,
    GpuMaterialSlot mat,
    uint instanceSlot,
    vec3 vertexWorldNormal,
    vec2 uv)
{
    const float vertexNormalLen = length(vertexWorldNormal);
    vec3 n = (vertexNormalLen > 1.0e-6)
        ? (vertexWorldNormal / vertexNormalLen)
        : vec3(0.0, 0.0, 1.0);

    // The Normal channel samples the baked object-space normal texture when
    // the material's per-channel source selects Texture (GRAPHICS-105). The
    // legacy ObjectSpaceNormalMap flag is honored as a transitional alias.
    const bool normalFromTexture =
        GpuMaterialChannelSource(mat, GpuMaterialChannel_Normal) == GpuAttributeSource_Texture ||
        (mat.Flags & (GpuMaterialFlag_ObjectSpaceNormalMap |
                      GpuMaterialFlag_WorldSpaceNormalMap)) != 0u;
    if (!normalFromTexture || !IsValidTextureID(mat.NormalID)) {
        return n;
    }

    const vec4 normalSample = texture(globalTextures[nonuniformEXT(mat.NormalID)], uv);
    vec3 objectNormal;
    if (!DecodePropertyTextureNormal(normalSample, objectNormal)) {
        return n;
    }

    if ((mat.Flags & GpuMaterialFlag_WorldSpaceNormalMap) != 0u) {
        return objectNormal;
    }

    const GpuInstanceDynamic dyn =
        GpuInstanceDynamicRef(scene.InstanceDynamicBDA).Data[instanceSlot];
    const mat3 normalMatrix = transpose(inverse(mat3(dyn.Model)));
    const vec3 worldNormal = normalMatrix * objectNormal;
    const float worldNormalLen = length(worldNormal);
    return (worldNormalLen > 1.0e-6)
        ? (worldNormal / worldNormalLen)
        : n;
}

vec2 ResolveMetallicRoughness(GpuMaterialSlot mat, vec2 uv)
{
    vec2 roughnessMetallic = vec2(mat.RoughnessFactor, mat.MetallicFactor);
    const bool metallicRoughnessFromTexture =
        GpuMaterialChannelSource(mat, GpuMaterialChannel_MetallicRoughness) == GpuAttributeSource_Texture;
    if (!metallicRoughnessFromTexture || !IsValidTextureID(mat.MetallicRoughnessID)) {
        return roughnessMetallic;
    }

    const vec4 mrSample = texture(globalTextures[nonuniformEXT(mat.MetallicRoughnessID)], uv);
    const float roughness =
        (mat.Flags & GpuMaterialFlag_ScalarRoughnessTexture) != 0u
            ? mrSample.r
            : mrSample.g;
    const float metallic =
        (mat.Flags & GpuMaterialFlag_ScalarMetallicTexture) != 0u
            ? mrSample.r
            : mrSample.b;
    return vec2(roughness, metallic);
}

vec4 SampleSurfaceBaseColor(GpuMaterialSlot mat, vec2 uv)
{
    vec4 baseColor = mat.BaseColorFactor;
    if (IsValidTextureID(mat.AlbedoID)) {
        vec4 albedoSample =
            texture(globalTextures[nonuniformEXT(mat.AlbedoID)], uv);
        if ((mat.Flags & GpuMaterialFlag_ScalarAlbedoTexture) != 0u) {
            const float t = GpuNormalizeScalarAlbedo(mat, albedoSample.r);
            const uint colormapID = GpuScalarAlbedoColormapID(mat);
            albedoSample = IsValidTextureID(colormapID)
                ? texture(globalTextures[nonuniformEXT(colormapID)], vec2(t, 0.5))
                : vec4(t, t, t, 1.0);
        }
        baseColor *= albedoSample;
    }
    return baseColor;
}

vec4 ResolveSurfaceVisualization(GpuEntityConfig cfg, vec4 baseColor,
                                 float scalar, vec4 color, uint faceId)
{
    float visualizationScalar = scalar;
    vec4 visualizationColor = color;
    if (cfg.VisDomain == GpuVisualizationDomain_Face) {
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
    return baseColor;
}

#endif
