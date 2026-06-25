#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-074 Slice B — GpuScene-aware EdgeId picking vertex shader.
//
// Pairs with `selection/edge_id.frag`. Mirrors `selection/entity_id.vert`
// exactly so the EdgeId selection pipeline reuses the same GpuScene-aware
// vertex-fetch chain. EdgeIdPass dispatches against the `Lines` cull
// bucket through `DrawIndexedIndirectCount`, so this shader sees
// line-list indexed draws over the same managed vertex stream as the
// forward line pipeline. The fragment shader writes
// `EncodeSelectionId(Edge, edgeIndex)` where `edgeIndex` is sourced from
// `gl_PrimitiveID` per GRAPHICS-012Q.
//
// Push-constant compatibility: the block below MUST mirror
// `RHI::GpuScenePushConstants` exactly. `EdgeIdPass::Execute` pushes those
// bytes via `cmd.PushConstants(&pc, sizeof(pc))`. Reusing the legacy
// `assets/shaders/pick_line.vert` (declares the pre-GpuScene push block)
// is a known footgun — see the "Shader push-constant compatibility
// policy" section in `src/graphics/renderer/README.md`.

#include "../common/gpu_scene.glsl"

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) flat out uint fragEntityID;

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuInstanceStatic inst = GpuInstanceStaticRef(scene.InstanceStaticBDA).Data[gl_InstanceIndex];
    const GpuInstanceDynamic dyn = GpuInstanceDynamicRef(scene.InstanceDynamicBDA).Data[gl_InstanceIndex];
    const GpuGeometryRecord geo = GpuGeometryRecordRef(scene.GeometryRecordBDA).Data[inst.GeometrySlot];

    // The culling indirect command supplies firstIndex + vertexOffset, so
    // gl_VertexIndex is already in managed-buffer vertex units. Channel BDAs
    // point at this geometry's first element, so fetch with the local index.
    const uint localVertexIndex = uint(gl_VertexIndex) - geo.VertexOffset;
    const vec3 localPosition = GpuReadPackedVec3(geo.VertexBufferBDA, localVertexIndex);

    gl_Position = scene.CameraViewProj * dyn.Model * vec4(localPosition, 1.0);
    fragEntityID = inst.EntityID;
}
