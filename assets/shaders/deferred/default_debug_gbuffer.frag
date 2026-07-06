#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

// GRAPHICS-072 Slice A — default-debug GBuffer fragment shader.
//
// Pairs with `forward/default_debug_surface.vert` so the deferred GBuffer
// pipeline reuses the same GpuScene-aware vertex stage as the forward
// default-debug-surface pipeline. The vertex shader forwards
// `fragMaterialSlot`, `fragConfigSlot`, visualization values, packed vertex
// color, and `fragInstanceSlot`; this fragment consumes that full interface so
// Vulkan pipeline-interface validation stays clean, then writes three GBuffer
// color attachments matching the recipe's deferred resource declarations:
//   - location 0 -> SceneNormal (RGBA16F): packed vertex normal.
//   - location 1 -> Albedo (RGBA8): material BaseColorFactor multiplied by a
//     bound albedo texture when present.
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
layout(location = 2) in vec3 fragWorldNormal;
layout(location = 3) flat in uint fragConfigSlot;
layout(location = 4) in float fragVisualizationScalar;
layout(location = 5) in vec4 fragVisualizationColor;
layout(location = 6) in vec4 fragVertexColor;
layout(location = 7) flat in uint fragHasVertexColor;
layout(location = 8) flat in uint fragInstanceSlot;

layout(location = 0) out vec4 SceneNormal;
layout(location = 1) out vec4 Albedo;
layout(location = 2) out vec4 Material0;

bool IsValidTextureID(uint id) {
    return id != 0u && id != 0xFFFFFFFFu;
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
    return vec2(mrSample.g, mrSample.b);
}

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuInstanceStatic inst =
        GpuInstanceStaticRef(scene.InstanceStaticBDA).Data[fragInstanceSlot];
    const uint materialSlot = fragMaterialSlot != 0xFFFFFFFFu
        ? fragMaterialSlot
        : inst.MaterialSlot;
    const uint configSlot = fragConfigSlot != 0xFFFFFFFFu
        ? fragConfigSlot
        : inst.ConfigSlot;
    const GpuMaterialSlot mat = GpuMaterialSlotRef(scene.MaterialBDA).Data[materialSlot];
    const GpuEntityConfig cfg = GpuEntityConfigRef(scene.EntityConfigBDA).Data[configSlot];

    vec3 n = vec3(0.0, 0.0, 1.0);
    const float vertexNormalLen = length(fragWorldNormal);
    if (vertexNormalLen > 1.0e-6) {
        n = fragWorldNormal / vertexNormalLen;
    }

    vec4 baseColor = mat.BaseColorFactor;
    if (IsValidTextureID(mat.AlbedoID)) {
        baseColor *= texture(globalTextures[nonuniformEXT(mat.AlbedoID)], fragUv);
    }
    if (fragHasVertexColor != 0u) {
        baseColor = fragVertexColor;
    }
    float visualizationScalar = fragVisualizationScalar;
    vec4 visualizationColor = fragVisualizationColor;
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

    SceneNormal = vec4(n, 0.0);
    Albedo = baseColor;
    const vec2 roughnessMetallic = ResolveMetallicRoughness(mat, fragUv);
    Material0 = vec4(roughnessMetallic.x, roughnessMetallic.y, float(cfg.ColorSourceMode), 0.0);
}
