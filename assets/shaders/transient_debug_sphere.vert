#version 460
// Transient debug points shaded as spheres (UI-067): each point is a view-space billboard of
// its world radius (six vertices), and the fragment ray-casts the sphere with the forward
// point pass's shading. One draw covers a run of points sharing a radius.
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#include "common/transient_debug_sphere.glsl"

struct Vertex
{
    vec3 Position;
    uint PackedColor;
};
layout(buffer_reference, scalar) readonly buffer VertexBuf { Vertex v[]; };

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragViewCenter;

const vec2 kCorners[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                                vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

void main()
{
    const Vertex vertex = VertexBuf(push.VertexBufferBDA).v[push.FirstVertex + uint(gl_VertexIndex) / 6u];
    const vec2 corner = kCorners[uint(gl_VertexIndex) % 6u];
    const vec3 center = push.View * vec4(vertex.Position, 1.0);
    gl_Position = push.Projection * vec4(center + vec3(corner * push.Radius, 0.0), 1.0);
    fragColor = unpackUnorm4x8(vertex.PackedColor);
    fragUV = corner;
    fragViewCenter = center;
}
