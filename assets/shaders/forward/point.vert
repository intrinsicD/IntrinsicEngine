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

float ResolvePointSizePx(GpuEntityConfig cfg, uint pointElementId) {
    float pointSizePx = cfg.Point.PointSize;
    if (cfg.Point.PointSizeBDA != uint64_t(0)) {
        pointSizePx = GpuFloatBufferRef(cfg.Point.PointSizeBDA).Data[pointElementId];
    }
    return clamp(pointSizePx, 0.5, 32.0);
}

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

    const uint firstPointQuadVertex = geo.PointFirstVertex * 6u;
    const uint vertexIndex = uint(gl_VertexIndex) - firstPointQuadVertex;
    const uint sourceVertexIndex = vertexIndex / 6u;
    const uint vertexInQuad = vertexIndex % 6u;
    const vec3 pointPosition = GpuReadPackedVec3(geo.VertexBufferBDA, sourceVertexIndex);

    const uint cornerIndex = uint[6](0u, 1u, 2u, 0u, 2u, 3u)[vertexInQuad];
    const vec2 localOffset = vec2[4](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0))[cornerIndex];

    const vec4 localPos = vec4(pointPosition, 1.0);
    const vec4 worldPos = dyn.Model * localPos;
    const vec4 viewCenter4 = scene.CameraView * worldPos;
    const vec4 centerClip = scene.CameraViewProj * dyn.Model * localPos;

    const float pointSizePx = ResolvePointSizePx(cfg, sourceVertexIndex);
    const float radiusPx = pointSizePx * 0.5;
    const float viewportWidth = max(scene.CameraViewportWidth, 1.0);
    const float viewportHeight = max(scene.CameraViewportHeight, 1.0);
    const vec2 clipOffset = localOffset *
        vec2((2.0 * radiusPx) / viewportWidth, (2.0 * radiusPx) / viewportHeight) *
        centerClip.w;

    gl_Position = centerClip + vec4(clipOffset, 0.0, 0.0);
    vPointMode = cfg.Point.PointMode;
    vDiscUV = localOffset;
    vViewCenter = viewCenter4.xyz;

    const float projectionScaleY = abs(scene.CameraProj[1][1]);
    const float depth = max(abs(viewCenter4.z), 1.0e-4);
    const float ndcRadiusY = (2.0 * radiusPx) / viewportHeight;
    vViewRadius = projectionScaleY > 1.0e-6
        ? max((ndcRadiusY * depth) / projectionScaleY, 1.0e-5)
        : 1.0e-3;

    vColor = ResolvePointVisualizationColor(cfg, sourceVertexIndex);
    vViewNormal = vec3(0.0, 0.0, 1.0);
}
