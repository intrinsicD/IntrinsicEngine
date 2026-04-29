#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "common/gpu_scene.glsl"

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
} camera;

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

    const GpuInstanceStatic inst = instanceStatic.Data[instanceSlot];
    const GpuInstanceDynamic dyn = instanceDynamic.Data[instanceSlot];
    const GpuGeometryRecord geo = geometryRecords.Data[inst.GeometrySlot];

    const uint vertexId = uint(gl_VertexIndex);
    PackedVertexRef vertices = PackedVertexRef(geo.VertexBufferBDA);
    const PackedVertex pv = vertices.Data[geo.VertexOffset + vertexId];

    vec4 worldPos = dyn.Model * vec4(pv.px, pv.py, pv.pz, 1.0);
    gl_Position = camera.proj * camera.view * worldPos;

    vColor = vec4(1.0);
}
