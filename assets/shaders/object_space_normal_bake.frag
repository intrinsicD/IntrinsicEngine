#version 460

layout(location = 0) in vec3 fragObjectNormal;
layout(location = 0) out vec4 outColor;

void main()
{
    float normalLength = length(fragObjectNormal);
    vec3 objectNormal = normalLength > 1.0e-6
        ? fragObjectNormal / normalLength
        : vec3(0.0, 0.0, 1.0);
    outColor = vec4(objectNormal * 0.5 + vec3(0.5), 1.0);
}
