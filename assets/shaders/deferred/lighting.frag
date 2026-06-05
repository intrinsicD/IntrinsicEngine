#version 460

#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// GRAPHICS-072 Slice C — the third word of the push-constant block carries
// the `ShadowSystem`-owned atlas's bindless slot. The renderer
// (`DeferredLightingPass::Execute`) sources it from
// `ShadowSystem::GetAtlasBindlessIndex()`. The engine's promoted Vulkan
// pipeline layout binds only the bindless descriptor heap at `set = 0`, so
// the legacy `set 1, binding 1` `sampler2DShadow` model from
// `assets/shaders/deferred_lighting.frag` cannot be honored; the bindless-
// index push-constant is the equivalent wiring. See
// `src/graphics/renderer/README.md` ("Slice C: shadow-atlas binding") for
// the durable rule and `ShadowAtlasBindlessIndex == 0` as the
// `kInvalidBindlessIndex` sentinel that disables shadow sampling.
layout(push_constant, scalar) uniform PushConstants
{
    uint64_t SceneTableBDA;
    uint     ShadowAtlasBindlessIndex;
    uint     _pad0;
} pc;

// Engine bindless texture heap. The shadow atlas is bound here through
// `ShadowSystem`'s lazy allocation (`TextureManager::Create(...)` registers
// the new slot, and `ShadowSystem::GetAtlasBindlessIndex()` returns it).
layout(set = 0, binding = 0) uniform sampler2D globalTextures[];

#include "common/gpu_scene.glsl"

void main()
{
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const float positiveViewZ = GpuPositiveViewZFromDeviceDepth(gl_FragCoord.z, scene);
    const vec3 accum = GpuAccumulateClusteredSceneLightsDebug(
        scene,
        gl_FragCoord.xy,
        positiveViewZ);

    // GRAPHICS-072 Slice C — sample the bound shadow atlas when the binding
    // is valid (non-zero slot, since slot 0 is reserved as the engine
    // default/error texture). A full Blinn-Phong + PCF cascade integration
    // matches the legacy `deferred_lighting.frag` shape but requires the
    // G-buffer sampler wiring + CameraUBO cascade matrices, both follow-ups
    // beyond Slice C. The sample MUST feed the fragment output so an
    // optimizer cannot eliminate the bindless fetch: a constant-zero weight
    // would constant-fold the texture call away, hiding descriptor/index
    // wiring regressions. Use the sampled value directly as the shadow
    // factor; a freshly-cleared atlas (depth 1.0) sampled via the
    // `CompareEnable=true, Compare=Less` shadow sampler returns ~1.0
    // (fully lit) so the visible output stays close to the unshadowed
    // accumulation while the bindless path is guaranteed to be live.
    float shadowFactor = 1.0;
    if (pc.ShadowAtlasBindlessIndex != 0u)
    {
        shadowFactor = texture(
            globalTextures[nonuniformEXT(pc.ShadowAtlasBindlessIndex)],
            vUV).r;
    }

    // Temporary debug composition: encode light accumulation modulated by
    // the sampled shadow factor. Full Blinn-Phong + PCF cascade integration
    // is a follow-up pass; `shadowFactor` lives on the output path so the
    // bindless shadow-atlas fetch above stays alive through optimization.
    outColor = vec4(accum * shadowFactor, 1.0);
}
