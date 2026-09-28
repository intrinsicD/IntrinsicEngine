// transient_debug_point.vert — canonical default-recipe transient-debug
// point vertex shader (GRAPHICS-077 Slice C).
//
// Mirrors `transient_debug_triangle.vert`: per-vertex position + packed
// RGBA8 color are pulled from a host-visible vertex buffer that
// `TransientDebugUploadHelper::UploadPoints(...)` packs each frame.
// The buffer is referenced via BDA carried in push constants; per-draw
// `FirstVertex` selects which point's single vertex to fetch so the
// executor can issue `Draw(1, 1, 0, 0)` per packet with packet-stable
// `gl_VertexIndex` semantics.
//
// The CPU/null contract only validates the `BindPipeline +
// PushConstants + Draw(1, 1, 0, 0)` shape; the optional `gpu;vulkan`
// smoke (GRAPHICS-077 Slice D) is the operational verification that
// the per-pixel color matches the submitted packet color. Points are
// round sprites whose size follows the packet's world-space `Radius`.

#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Per-vertex layout the helper writes: 12-byte position followed by a
// packed RGBA8 color (16 bytes total including padding).
struct Vertex
{
    vec3 Position;
    uint PackedColor;
};
layout(buffer_reference, scalar) readonly buffer VertexBuf { Vertex v[]; };

layout(push_constant) uniform PushConsts {
    uint64_t VertexBufferBDA;
    uint     FirstVertex;
    float    Radius;         // world units
    mat4     ViewProjection; // packet coordinates are world space
    float    PixelsPerUnit;  // Projection[1][1] * viewport height / 2; 0 keeps 1-pixel points
} push;

layout(location = 0) out vec4 fragColor;

void main()
{
    VertexBuf vbuf = VertexBuf(push.VertexBufferBDA);
    Vertex vertex = vbuf.v[push.FirstVertex + gl_VertexIndex];

    gl_Position = push.ViewProjection * vec4(vertex.Position, 1.0);
    // Diameter in pixels of a sphere of `Radius` at this depth, clamped to what point
    // sprites reliably support.
    const float w = max(abs(gl_Position.w), 1e-6);
    gl_PointSize = clamp(2.0 * push.Radius * push.PixelsPerUnit / w, 1.0, 64.0);
    fragColor = unpackUnorm4x8(vertex.PackedColor);
}
