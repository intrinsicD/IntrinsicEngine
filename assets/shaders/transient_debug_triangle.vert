// transient_debug_triangle.vert — canonical default-recipe
// transient-debug triangle vertex shader (GRAPHICS-077 Slice B).
//
// Pulls per-vertex position + packed RGBA8 color from the host-visible
// vertex buffer the `TransientDebugUploadHelper` packs each frame. The
// buffer is referenced via BDA carried in push constants; per-draw
// `FirstVertex` selects which triangle's three vertices to fetch so the
// executor can issue `Draw(3, 1, 0, 0)` per packet with packet-stable
// `gl_VertexIndex` semantics.
//
// CPU/null contract only validates the `BindPipeline + PushConstants +
// Draw(3, 1, 0, 0)` shape; the optional `gpu;vulkan` smoke
// (GRAPHICS-077 Slice D) is the operational verification that the
// per-pixel color matches the submitted packet color.

#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Camera UBO (shared across all passes).
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
} camera;

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
    uint     Reserved;
} push;

layout(location = 0) out vec4 fragColor;

void main()
{
    VertexBuf vbuf = VertexBuf(push.VertexBufferBDA);
    Vertex vertex = vbuf.v[push.FirstVertex + gl_VertexIndex];

    gl_Position = camera.proj * camera.view * vec4(vertex.Position, 1.0);
    fragColor = unpackUnorm4x8(vertex.PackedColor);
}
