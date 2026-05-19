#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-074 Slice A — GpuScene-aware EntityId picking fragment shader.
//
// Pairs with `selection/entity_id.vert`. Writes two R32_UINT color outputs
// matching the recipe's `PickingPass` color-target declarations
// (`Graphics.FrameRecipe.cpp`, `EnablePicking` branch):
//   - location 0 -> EntityId    (R32_UINT): the per-instance stable entity ID
//     forwarded from `fragEntityID` (vertex shader read `inst.EntityID` from
//     the GpuScene table; value `0` is reserved for "no hit").
//   - location 1 -> PrimitiveId (R32_UINT): the entity-domain encoded value
//     `EncodeSelectionId(SelectionPrimitiveDomain::Entity, 0)` per
//     `GRAPHICS-012Q` so a hit with no sub-element refinement still carries
//     the entity domain bits (high 4 bits = 1, low 28 bits = 0). Slice B's
//     Face/Edge/Point shaders will use the same domain encoding with their
//     own payloads.
//
// Push-constant compatibility: the block below MUST mirror
// `RHI::GpuScenePushConstants` exactly even though the fragment never reads
// it directly; declaring it keeps the pipeline-layout push-constant block
// consistent across vertex + fragment stages on backends that validate
// push-constant range continuity.

layout(push_constant, scalar) uniform ScenePC {
    uint64_t SceneTableBDA;
    uint FrameIndex;
    uint DrawBucket;
    uint DebugMode;
    uint _pad0;
} pc;

layout(location = 0) flat in uint fragEntityID;

layout(location = 0) out uint outEntityID;
layout(location = 1) out uint outPrimitiveID;

// Matches EncodedSelectionId in src/graphics/renderer/Graphics.SelectionSystem.cppm:
//   DomainShift = 28, Domain::Entity = 1 -> high nibble = 0x1.
const uint kSelectionDomainEntity = 1u << 28u;

void main() {
    outEntityID = fragEntityID;
    outPrimitiveID = kSelectionDomainEntity;
}
