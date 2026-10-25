#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-074 Slice B — GpuScene-aware PointId picking vertex shader.
//
// Pairs with `selection/point_id.frag`. PointIdPass dispatches against the
// `SelectionPoints` cull bucket through `DrawIndirectCount` (non-indexed,
// six vertices per point like `Points`). BUG-254 — the quad comes from the
// same helper as `forward/point.vert` (uniform or per-point size), so the
// pick covers the pixels the visible point covers; the fragment shader
// applies the visible point's coverage and depth.
//
// Push-constant compatibility: the block below MUST mirror
// `RHI::GpuScenePushConstants` exactly. `PointIdPass::Execute` pushes
// those bytes via `cmd.PushConstants(&pc, sizeof(pc))`. Do not
// reintroduce the retired pre-GpuScene pick push block — see the "Shader
// push-constant compatibility policy" section in
// `src/graphics/renderer/README.md`.

#include "../common/gpu_scene.glsl"
#include "../common/point_quad.glsl"

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) flat out uint fragEntityID;
layout(location = 1) flat out uint fragPointIndex;
layout(location = 2) flat out uint fragPointMode;
layout(location = 3) out vec2 fragDiscUV;
layout(location = 4) out vec3 fragViewCenter;
layout(location = 5) out float fragViewRadius;
layout(location = 6) out vec3 fragViewNormal;

invariant gl_Position;

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuInstanceStatic inst = GpuInstanceStaticRef(scene.InstanceStaticBDA).Data[gl_InstanceIndex];
    const GpuInstanceDynamic dyn = GpuInstanceDynamicRef(scene.InstanceDynamicBDA).Data[gl_InstanceIndex];
    const GpuGeometryRecord geo = GpuGeometryRecordRef(scene.GeometryRecordBDA).Data[inst.GeometrySlot];
    const GpuEntityConfig cfg = GpuEntityConfigRef(scene.EntityConfigBDA).Data[inst.ConfigSlot];

    const GpuPointQuadCorner corner = GpuPointQuadCornerAt(scene, dyn, geo, cfg, uint(gl_VertexIndex));
    gl_Position = corner.ClipPosition;
    fragEntityID = inst.EntityID;
    fragPointIndex = corner.PointIndex;
    fragPointMode = cfg.Point.PointMode;
    fragDiscUV = corner.DiscUV;
    fragViewCenter = corner.ViewCenter;
    fragViewRadius = corner.ViewRadius;
    fragViewNormal = corner.ViewNormal;
}
