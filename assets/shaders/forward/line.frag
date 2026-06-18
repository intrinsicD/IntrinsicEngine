#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "common/gpu_scene.glsl"

layout(set = 0, binding = 0) uniform sampler2D globalTextures[];

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) flat in uint vConfigSlot;
layout(location = 1) in float vVisualizationScalar;
layout(location = 2) in vec4 vVisualizationColor;
layout(location = 0) out vec4 outColor;

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuEntityConfig cfg =
        GpuEntityConfigRef(scene.EntityConfigBDA).Data[vConfigSlot];
    outColor = (cfg.ColorSourceMode == GpuColorSource_ScalarField &&
                GpuVisualizationHasValidBindless(cfg.ColormapID))
        ? GpuResolveVisualizationColorWithColormap(
            cfg,
            vVisualizationScalar,
            vVisualizationColor,
            vec4(1.0),
            globalTextures[nonuniformEXT(cfg.ColormapID)])
        : GpuResolveVisualizationColorFallback(
            cfg,
            vVisualizationColor,
            vec4(1.0));
}
