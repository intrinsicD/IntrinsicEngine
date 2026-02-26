#version 460

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragDiscUV;

layout(location = 0) out vec4 outColor;

void main()
{
    float r2 = dot(fragDiscUV, fragDiscUV);
    if (r2 > 1.0) discard;

    float alpha = 1.0 - smoothstep(0.85, 1.0, sqrt(r2));
    outColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
