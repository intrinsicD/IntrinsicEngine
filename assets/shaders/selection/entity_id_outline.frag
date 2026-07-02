#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-113 — outline-only EntityId fragment shader.
//
// Pairs with `selection/entity_id.vert` and writes only the `EntityId`
// R32_UINT target. Pending click picking keeps using `entity_id.frag`, which
// also writes `PrimitiveId` for primitive refinement and readback.

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) flat in uint fragEntityID;

layout(location = 0) out uint outEntityID;

void main() {
    outEntityID = fragEntityID;
}
