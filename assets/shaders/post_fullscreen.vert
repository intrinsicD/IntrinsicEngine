#version 450

// Fullscreen triangle (no vertex buffers).
// Reusable by all post-processing passes.

vec2 positions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

layout(location = 0) out vec2 vUV;

void main()
{
    vec2 p = positions[gl_VertexIndex];
    gl_Position = vec4(p, 0.0, 1.0);
    vUV = 0.5 * (p + vec2(1.0));
}
