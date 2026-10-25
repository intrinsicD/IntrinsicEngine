#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "common/gpu_scene.glsl"
#include "common/line_quad.glsl"

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) flat out uint vConfigSlot;
layout(location = 1) out float vVisualizationScalar;
layout(location = 2) out vec4 vVisualizationColor;

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

    // BUG-254 — the edge pick draws the same quad.
    const GpuLineQuadCorner corner = GpuLineQuadCornerAt(scene, dyn, geo, cfg, uint(gl_VertexIndex));
    gl_Position = corner.ClipPosition;

    const uint elementId = cfg.VisDomain == GpuVisualizationDomain_Vertex
        ? corner.EndpointVertex
        : corner.SegmentIndex;
    vConfigSlot = inst.ConfigSlot;
    vVisualizationScalar = GpuVisualizationReadScalar(cfg, elementId, cfg.ScalarRangeMin);
    vVisualizationColor = GpuVisualizationReadColor(cfg, elementId, vec4(1.0));
}
