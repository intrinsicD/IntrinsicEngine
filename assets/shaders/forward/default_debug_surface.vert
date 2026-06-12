#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-031A — canonical missing-material fallback vertex shader.
//
// BDA-only contract: the promoted Vulkan pipeline layout binds only the
// global bindless descriptor set at set 0 (`setLayoutCount = 1`), so no
// per-frame camera UBO or material SSBO descriptor is available here.
// All data is read through `GpuScenePushConstants::SceneTableBDA` and the
// chain of `buffer_reference` pointers declared in `common/gpu_scene.glsl`.
//
// Reads positions from the procedural vertex buffer authored by the
// GRAPHICS-030A Triangle packer (vec3 position + vec2 uv = 20 bytes/vertex)
// and forwards the per-instance material slot plus the scene-table
// MaterialBDA so the fragment shader can sample the material slot from a
// buffer reference instead of a descriptor set. Clip-space transforms use
// the current camera matrix published through `GpuSceneTable`.

#include "../common/gpu_scene.glsl"

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

layout(location = 0) flat out uint fragMaterialSlot;
layout(location = 1) out vec2 fragUv;

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuInstanceStatic inst = GpuInstanceStaticRef(scene.InstanceStaticBDA).Data[gl_InstanceIndex];
    const GpuInstanceDynamic dyn = GpuInstanceDynamicRef(scene.InstanceDynamicBDA).Data[gl_InstanceIndex];
    const GpuGeometryRecord geo = GpuGeometryRecordRef(scene.GeometryRecordBDA).Data[inst.GeometrySlot];

    // The culling indirect command supplies firstIndex + vertexOffset, so
    // gl_VertexIndex is already in managed-buffer vertex units.
    const ProceduralVertex v = ProceduralVertexRef(geo.VertexBufferBDA).Data[gl_VertexIndex];

    gl_Position = scene.CameraViewProj * dyn.Model * vec4(v.Position, 1.0);
    fragMaterialSlot = inst.MaterialSlot;
    fragUv = v.UV;
}
