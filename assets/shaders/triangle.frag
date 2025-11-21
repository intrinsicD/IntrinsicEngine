#version 460

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D texSampler;

void main() {
    // Debug View: Show Normals as Color
    // Map [-1, 1] to [0, 1]
    vec3 normColor = normalize(fragNormal) * 0.5 + 0.5;

    vec4 textureColor = texture(texSampler, fragTexCoord);

    // Blend Texture with Normal map look
    outColor = vec4(normColor, 1.0) * textureColor;
    outColor = vec4(normColor, 1.0);
}