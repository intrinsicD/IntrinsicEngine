// visualization_isoline.vert — canonical default-recipe
// visualization-overlay isoline vertex shader (GRAPHICS-078 Slice C).
//
// Pulls per-vertex position + packed RGBA8 color from the host-visible
// vertex buffer that `VisualizationOverlayUploadHelper::UploadIsolines(...)`
// packs each frame. The buffer is referenced via BDA carried in push
// constants; per-draw `FirstVertex` selects which packet's
// `2 * IsoValueCount` placeholder iso-line endpoints to fetch so the
// executor can issue `Draw(2 * IsoValueCount, 1, 0, 0)` per packet with
// packet-stable `gl_VertexIndex` semantics (`LineList` topology).
//
// CPU/null path note: the renderer-side helper does not have CPU
// access to the source scalar field (its values + topology are GPU-
// side), so the CPU/null path writes zero positions and the packet
// color into each packed vertex. The CPU/null contract only validates
// the `BindPipeline + PushConstants + Draw(N, 1, 0, 0)` shape; per-
// pixel correctness on a real Vulkan device is owned by the optional
// `gpu;vulkan` smoke (GRAPHICS-078 Slice D) and the Vulkan-tuned
// helper variant that expands actual iso-contour vertices from the
// scalar field's GPU buffer.

#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Per-vertex layout the helper writes: 12-byte position followed by a
// packed RGBA8 color (16 bytes total including padding). Shared with
// the vector-field + transient-debug shaders by design so all upload
// paths can converge on a single packed-vertex contract.
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
