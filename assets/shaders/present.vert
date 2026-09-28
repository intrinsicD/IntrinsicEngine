#version 450

// GRAPHICS-076 Slice A — canonical default-recipe present shader. Emits
// a fullscreen-triangle (positions [-1,-1], [3,-1], [-1,3]) and forwards
// UVs into [0,1]. Pairs with `present.frag` which samples
// `FrameRecipe.PresentSource` and writes the imported backbuffer.

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
    // The negative-height viewport puts NDC +Y at row 0, so v runs top-down.
    vUV = vec2(0.5 * (p.x + 1.0), 0.5 * (1.0 - p.y));
}
