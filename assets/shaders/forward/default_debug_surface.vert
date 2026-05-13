#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-031A — canonical missing-material fallback vertex shader.
//
// Reads the scene-table BDA push constant, walks Instance/Geometry records,
// and fetches positions from the procedural vertex buffer authored by the
// GRAPHICS-030A Triangle packer (vec3 position + vec2 uv = 20 bytes/vertex).
// Forwards an interpolated vertex colour and the per-instance material slot
// so the fragment shader can sample MaterialBuffer at the canonical
// set = 3, binding = 0 SSBO.

#include "../common/gpu_scene.glsl"

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 View;
    mat4 Proj;
    mat4 ViewProj;
} camera;

struct ProceduralVertex {
    vec3 Position;
    vec2 UV;
};

layout(buffer_reference, scalar) readonly buffer ProceduralVertexRef {
    ProceduralVertex Data[];
};

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) out vec4 fragVertexColor;
layout(location = 1) flat out uint fragMaterialSlot;

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuInstanceStatic inst = GpuInstanceStaticRef(scene.InstanceStaticBDA).Data[gl_InstanceIndex];
    const GpuInstanceDynamic dyn = GpuInstanceDynamicRef(scene.InstanceDynamicBDA).Data[gl_InstanceIndex];
    const GpuGeometryRecord geo = GpuGeometryRecordRef(scene.GeometryRecordBDA).Data[inst.GeometrySlot];

    const ProceduralVertex v = ProceduralVertexRef(geo.VertexBufferBDA).Data[geo.VertexOffset + gl_VertexIndex];

    gl_Position = camera.ViewProj * dyn.Model * vec4(v.Position, 1.0);
    fragVertexColor = vec4(1.0);
    fragMaterialSlot = inst.MaterialSlot;
}
