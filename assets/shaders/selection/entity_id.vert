#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-074 Slice A — GpuScene-aware EntityId picking vertex shader.
//
// Pairs with `selection/entity_id.frag` so the EntityId selection pipeline
// reuses the same GpuScene-aware vertex-fetch chain that the forward/deferred
// default-debug-surface pipelines use. The vertex shader reads positions and
// the per-instance stable entity ID through `GpuScenePushConstants::SceneTableBDA`
// and forwards `inst.EntityID` to the fragment as a flat varying so the
// fragment can write it verbatim into the R32_UINT EntityId target.
//
// Push-constant compatibility: the block below MUST mirror
// `RHI::GpuScenePushConstants` exactly. `EntityIdPass::Execute` pushes those
// bytes via `cmd.PushConstants(&pc, sizeof(pc))`. Reusing the legacy
// `assets/shaders/pick_id.vert` (declares `mat4 Model + PtrPositions + ... +
// uint EntityID`) is a known footgun — see the "Shader push-constant
// compatibility policy" section in `src/graphics/renderer/README.md`.

#include "../common/gpu_scene.glsl"

struct SurfaceVertex {
    vec3 Position;
    vec2 UV;
    vec3 Normal;
};

layout(buffer_reference, scalar) readonly buffer SurfaceVertexRef {
    SurfaceVertex Data[];
};

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
    // gl_VertexIndex is already in managed-buffer vertex units.
    const SurfaceVertex v = SurfaceVertexRef(geo.VertexBufferBDA).Data[gl_VertexIndex];

    gl_Position = scene.CameraViewProj * dyn.Model * vec4(v.Position, 1.0);
    fragEntityID = inst.EntityID;
}
