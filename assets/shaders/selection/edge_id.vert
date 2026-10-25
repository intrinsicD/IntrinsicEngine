#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-074 Slice B — GpuScene-aware EdgeId picking vertex shader.
//
// Pairs with `selection/edge_id.frag`. EdgeIdPass dispatches against the
// `LineQuads` cull bucket through `DrawIndirectCount` (non-indexed, six
// vertices per segment). BUG-254 — the quad comes from the same helper as
// `forward/line.vert` (uniform or per-edge width), so the pick covers the
// pixels the visible line covers. The segment index is passed flat: the
// fragment shader writes `EncodeSelectionId(Edge, segmentIndex)`.
//
// Push-constant compatibility: the block below MUST mirror
// `RHI::GpuScenePushConstants` exactly. `EdgeIdPass::Execute` pushes those
// bytes via `cmd.PushConstants(&pc, sizeof(pc))`. Do not reintroduce the
// retired pre-GpuScene pick push block — see the "Shader push-constant
// compatibility policy" section in `src/graphics/renderer/README.md`.

#include "../common/gpu_scene.glsl"
#include "../common/line_quad.glsl"

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) flat out uint fragEntityID;
layout(location = 1) flat out uint fragEdgeIndex;

invariant gl_Position;

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuInstanceStatic inst = GpuInstanceStaticRef(scene.InstanceStaticBDA).Data[gl_InstanceIndex];
    const GpuInstanceDynamic dyn = GpuInstanceDynamicRef(scene.InstanceDynamicBDA).Data[gl_InstanceIndex];
    const GpuGeometryRecord geo = GpuGeometryRecordRef(scene.GeometryRecordBDA).Data[inst.GeometrySlot];
    const GpuEntityConfig cfg = GpuEntityConfigRef(scene.EntityConfigBDA).Data[inst.ConfigSlot];

    const GpuLineQuadCorner corner = GpuLineQuadCornerAt(scene, dyn, geo, cfg, uint(gl_VertexIndex));
    gl_Position = corner.ClipPosition;
    fragEntityID = inst.EntityID;
    fragEdgeIndex = corner.SegmentIndex;
}
