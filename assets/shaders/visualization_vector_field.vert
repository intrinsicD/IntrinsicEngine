// visualization_vector_field.vert — canonical default-recipe
// visualization-overlay vector-field vertex shader (GRAPHICS-078
// Slice B).
//
// Pulls per-vertex position + packed RGBA8 color from the host-visible
// vertex buffer that `VisualizationOverlayUploadHelper::UploadVectorFields(...)`
// packs each frame. The buffer is referenced via BDA carried in push
// constants; per-draw `FirstVertex` selects which packet's
// `2 * ElementCount` glyph endpoints to fetch so the executor can
// issue `Draw(2 * ElementCount, 1, 0, 0)` per packet with packet-
// stable `gl_VertexIndex` semantics.
//
// CPU/null path note: the renderer-side helper does not have CPU access to
// `VectorFieldOverlayPacket::PositionBufferBDA` / `VectorBufferBDA` (those are
// GPU pointers), so it writes deterministic placeholder glyph segments and the
// packet color into each packed vertex. GRAPHICS-078E validates those
// placeholders with opt-in Vulkan pixel-readback; actual source-BDA endpoint
// expansion remains future work.

#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Per-vertex layout the helper writes: 12-byte position followed by a
// packed RGBA8 color (16 bytes total including padding). Shared with
// the transient-debug shaders by design so both upload paths can
// converge on a single packed-vertex contract.
struct Vertex
{
    vec3 Position;
    uint PackedColor;
};
layout(buffer_reference, scalar) readonly buffer VertexBuf { Vertex v[]; };

layout(push_constant) uniform PushConsts {
    uint64_t VertexBufferBDA;
    uint     FirstVertex;
    uint     Reserved;
} push;

layout(location = 0) out vec4 fragColor;

void main()
{
    VertexBuf vbuf = VertexBuf(push.VertexBufferBDA);
    Vertex vertex = vbuf.v[push.FirstVertex + gl_VertexIndex];

    gl_Position = vec4(vertex.Position, 1.0);
    fragColor = unpackUnorm4x8(vertex.PackedColor);
}
