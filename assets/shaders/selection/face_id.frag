#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// GRAPHICS-074 Slice B — GpuScene-aware FaceId picking fragment shader.
//
// Pairs with `selection/face_id.vert`. Writes two R32_UINT color outputs
// matching the recipe's `PickingPass` color-target declarations
// (`Graphics.FrameRecipe.cpp`, `EnablePicking` branch):
//   - location 0 -> EntityId    (R32_UINT): the per-instance stable entity ID
//     forwarded from `fragEntityID` (vertex shader read `inst.EntityID` from
//     the GpuScene table; value `0` is reserved for "no hit").
//   - location 1 -> PrimitiveId (R32_UINT): `EncodeSelectionId(Face,
//     faceIndex)` per GRAPHICS-012Q, where `faceIndex` is taken from
//     `gl_PrimitiveID` — the per-draw-call triangle index over the
//     `SurfaceOpaque` cull bucket. High 4 bits encode `Face = 2`, low 28
//     bits carry the face index masked to `PayloadMask`.
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
//   DomainShift = 28, Domain::Face = 2 -> high nibble = 0x2.
const uint kSelectionDomainFace = 2u << 28u;
const uint kSelectionPayloadMask = (1u << 28u) - 1u;

void main() {
    outEntityID = fragEntityID;
    outPrimitiveID = kSelectionDomainFace | (uint(gl_PrimitiveID) & kSelectionPayloadMask);
}
