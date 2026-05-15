#version 450

// GRAPHICS-033D scaffold: fixed reference-triangle vertex shader for the
// opt-in MinimalDebug Vulkan smoke. This intentionally avoids BDA/descriptor
// reads so it can validate swapchain/backbuffer rendering before the default
// recipe owns the final visible-triangle path.

layout(location = 0) out vec3 vColor;

const vec2 kPositions[3] = vec2[3](
    vec2(-0.5, -0.5),
    vec2( 0.5, -0.5),
    vec2( 0.0,  0.5)
);

void main()
{
    gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
    vColor = vec3(0.55, 0.20, 0.85);
}

