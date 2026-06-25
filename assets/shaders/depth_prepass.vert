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

void main()
{
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const uint instanceSlot = gl_InstanceIndex;

    if (instanceSlot >= scene.InstanceCapacity) {
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const GpuInstanceStatic inst = GpuInstanceStaticRef(scene.InstanceStaticBDA).Data[instanceSlot];
    const GpuInstanceDynamic dyn = GpuInstanceDynamicRef(scene.InstanceDynamicBDA).Data[instanceSlot];
    const GpuGeometryRecord geo = GpuGeometryRecordRef(scene.GeometryRecordBDA).Data[inst.GeometrySlot];
    // The culling indirect command supplies firstIndex + vertexOffset, so
    // gl_VertexIndex is already in managed-buffer vertex units. Channel BDAs
    // point at this geometry's first element, so fetch with the local index.
    const uint localVertexIndex = uint(gl_VertexIndex) - geo.VertexOffset;
    const vec3 localPosition = GpuReadPackedVec3(geo.VertexBufferBDA, localVertexIndex);

    gl_Position = scene.CameraViewProj * dyn.Model * vec4(localPosition, 1.0);
}
