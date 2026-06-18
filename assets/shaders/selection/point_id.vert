#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-074 Slice B — GpuScene-aware PointId picking vertex shader.
//
// Pairs with `selection/point_id.frag`. Mirrors `selection/entity_id.vert`
// for the position fetch chain and `assets/shaders/forward/point.vert` for
// the `gl_PointSize` write. PointIdPass dispatches against the `SelectionPoints`
// cull bucket through `DrawIndirectCount` (non-indexed), so this shader
// sees point-list draws over the managed vertex stream. The fragment
// shader writes `EncodeSelectionId(Point, pointIndex)` where `pointIndex`
// is sourced from `gl_PrimitiveID` per GRAPHICS-012Q.
//
// Point-size publication: `BuildSelectionPointIdPipelineDesc()` configures
// the pipeline with `Topology::PointList`, which requires the vertex stage
// to emit a valid `gl_PointSize` — without it Vulkan pipeline creation can
// fail point-size validation, and even when accepted the rasterizer falls
// back to 1-pixel points and picking misses every entity rendered with a
// larger configured point size. `forward/point.vert` already publishes the
// clamped configured point size, so picking must match for the
// visible point to be hittable. This shader therefore reads
// `GpuEntityConfig::Point.PointSize` through `scene.EntityConfigBDA` indexed by
// `inst.ConfigSlot` (the same chain `forward/point.vert` uses) and writes
// the same `gl_PointSize` value.
//
// Push-constant compatibility: the block below MUST mirror
// `RHI::GpuScenePushConstants` exactly. `PointIdPass::Execute` pushes
// those bytes via `cmd.PushConstants(&pc, sizeof(pc))`. Reusing the
// legacy `assets/shaders/pick_point.vert` (declares the pre-GpuScene
// push block) is a known footgun — see the "Shader push-constant
// compatibility policy" section in `src/graphics/renderer/README.md`.

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

layout(location = 0) flat out uint fragEntityID;

void main() {
    const GpuSceneTable scene = GpuSceneTableRef(pc.SceneTableBDA).Value;
    const GpuInstanceStatic inst = GpuInstanceStaticRef(scene.InstanceStaticBDA).Data[gl_InstanceIndex];
    const GpuInstanceDynamic dyn = GpuInstanceDynamicRef(scene.InstanceDynamicBDA).Data[gl_InstanceIndex];
    const GpuGeometryRecord geo = GpuGeometryRecordRef(scene.GeometryRecordBDA).Data[inst.GeometrySlot];
    const GpuEntityConfig cfg = GpuEntityConfigRef(scene.EntityConfigBDA).Data[inst.ConfigSlot];

    // The culling indirect command supplies firstVertex, so gl_VertexIndex is
    // already in managed-buffer vertex units.
    const ProceduralVertex v = ProceduralVertexRef(geo.VertexBufferBDA).Data[gl_VertexIndex];

    gl_Position = scene.CameraViewProj * dyn.Model * vec4(v.Position, 1.0);
    // Match `forward/point.vert`'s point-size publication so the picking
    // footprint covers the same pixels the visible point covers.
    gl_PointSize = clamp(cfg.Point.PointSize, 0.5, 32.0);
    fragEntityID = inst.EntityID;
}
