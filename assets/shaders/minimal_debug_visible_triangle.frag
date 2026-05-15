#version 450

// GRAPHICS-033D scaffold fragment shader for the fixed visible-triangle smoke.

layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(vColor, 1.0);
}

