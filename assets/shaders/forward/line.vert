#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "common/gpu_scene.glsl"

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

struct PackedVertex {
    float px;
    float py;
    float pz;
    float u;
    float v;
};
layout(buffer_reference, scalar) readonly buffer PackedVertexRef { PackedVertex Data[]; };

layout(location = 0) out vec4 vColor;

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

    PackedVertexRef vertices = PackedVertexRef(geo.VertexBufferBDA);
    GpuUIntBufferRef indices = GpuUIntBufferRef(geo.IndexBufferBDA);

    const uint vertexIndex = uint(gl_VertexIndex);
    const uint segmentIndex = vertexIndex / 6u;
    const uint vertexInQuad = vertexIndex % 6u;
    const uint cornerIndex = uint[6](0u, 1u, 2u, 0u, 2u, 3u)[vertexInQuad];
    const uint lineIndexBase = segmentIndex * 2u;
    const uint endpointA = indices.Data[lineIndexBase] + geo.VertexOffset;
    const uint endpointB = indices.Data[lineIndexBase + 1u] + geo.VertexOffset;
    const PackedVertex a = vertices.Data[endpointA];
    const PackedVertex b = vertices.Data[endpointB];

    const vec4 clipA = scene.CameraViewProj * dyn.Model * vec4(a.px, a.py, a.pz, 1.0);
    const vec4 clipB = scene.CameraViewProj * dyn.Model * vec4(b.px, b.py, b.pz, 1.0);
    const bool useEnd = cornerIndex >= 2u;
    const float side = (cornerIndex == 0u || cornerIndex == 3u) ? -1.0 : 1.0;
    const vec4 centerClip = useEnd ? clipB : clipA;

    const vec2 ndcA = clipA.xy / max(abs(clipA.w), 1.0e-6);
    const vec2 ndcB = clipB.xy / max(abs(clipB.w), 1.0e-6);
    const vec2 direction = ndcB - ndcA;
    const float directionLength = length(direction);
    const vec2 tangent = directionLength > 1.0e-6 ? direction / directionLength : vec2(1.0, 0.0);
    const vec2 normal = vec2(-tangent.y, tangent.x);
    const vec2 viewport = max(vec2(scene.CameraViewportWidth, scene.CameraViewportHeight), vec2(1.0));
    const float halfWidthPx = 0.5;
    const vec2 clipOffset = normal * side * vec2((2.0 * halfWidthPx) / viewport.x,
                                                 (2.0 * halfWidthPx) / viewport.y) * centerClip.w;

    gl_Position = centerClip + vec4(clipOffset, 0.0, 0.0);

    vColor = (cfg.ColorSourceMode == GpuColorSource_UniformColor) ? cfg.UniformColor : vec4(1.0);
}
