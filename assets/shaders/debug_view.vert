#version 450

// Fullscreen triangle (no vertex buffers)

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
    // Map clip-space [-1,1] to UV [0,1]
    // The negative-height viewport puts NDC +Y at row 0, so v runs top-down.
    vUV = vec2(0.5 * (p.x + 1.0), 0.5 * (1.0 - p.y));
}

