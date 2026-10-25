#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "common/gpu_scene.glsl"
#include "common/point_quad.glsl"

layout(set = 0, binding = 0) uniform sampler2D globalTextures[];

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) out vec4 vColor;
layout(location = 1) flat out uint vPointMode;
layout(location = 2) out vec3 vViewNormal;
layout(location = 3) out vec2 vDiscUV;
layout(location = 4) out vec3 vViewCenter;
layout(location = 5) out float vViewRadius;

vec4 ResolvePointVisualizationColor(GpuEntityConfig cfg, uint pointElementId) {
    const float scalar = GpuVisualizationReadScalar(cfg, pointElementId, cfg.ScalarRangeMin);
    const vec4 elementColor = GpuVisualizationReadColor(cfg, pointElementId, vec4(1.0));
    return (cfg.ColorSourceMode == GpuColorSource_ScalarField &&
            GpuVisualizationHasValidBindless(cfg.ColormapID))
        ? GpuResolveVisualizationColorWithColormap(
            cfg,
            scalar,
            elementColor,
            vec4(1.0),
            globalTextures[nonuniformEXT(cfg.ColormapID)])
        : GpuResolveVisualizationColorFallback(cfg, elementColor, vec4(1.0));
}

invariant gl_Position;

void main() {
    const uint instanceSlot = gl_InstanceIndex;
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;

    GpuInstanceStaticRef instanceStatic = GpuInstanceStaticRef(scene.InstanceStaticBDA);
    GpuInstanceDynamicRef instanceDynamic = GpuInstanceDynamicRef(scene.InstanceDynamicBDA);
    GpuGeometryRecordRef geometryRecords = GpuGeometryRecordRef(scene.GeometryRecordBDA);
    GpuEntityConfigRef entityConfigs = GpuEntityConfigRef(scene.EntityConfigBDA);

    const GpuInstanceStatic inst = instanceStatic.Data[instanceSlot];
    const GpuInstanceDynamic dyn = instanceDynamic.Data[instanceSlot];
    const GpuGeometryRecord geo = geometryRecords.Data[inst.GeometrySlot];
    const GpuEntityConfig cfg = entityConfigs.Data[inst.ConfigSlot];

    // BUG-254 — the point pick draws the same quad.
    const GpuPointQuadCorner corner = GpuPointQuadCornerAt(scene, dyn, geo, cfg, uint(gl_VertexIndex));
    gl_Position = corner.ClipPosition;
    vPointMode = cfg.Point.PointMode;
    vDiscUV = corner.DiscUV;
    vViewCenter = corner.ViewCenter;
    vViewRadius = corner.ViewRadius;
    vColor = ResolvePointVisualizationColor(cfg, corner.PointIndex);
    vViewNormal = corner.ViewNormal;
}
