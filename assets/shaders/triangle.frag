#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

// Binding 0 = Camera (UBO), Binding 1 = Bindless Array
// Note: We don't declare Binding 0 here if we don't use it in Frag,
// but usually it's good practice to keep set layouts consistent.
layout(set = 1, binding = 0) uniform sampler2D globalTextures[];

layout(push_constant) uniform PushConsts {
    mat4 model;
    uint textureID;
} push;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 lightColor = vec3(1.0, 1.0, 1.0);
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * lightColor;

    vec3 norm = normalize(fragNormal);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    const bool selected = (push.textureID & 0x80000000u) != 0u;
    const uint texIndex = push.textureID & 0x7fffffffu;

    // BINDLESS SAMPLE
    // nonuniformEXT helps the driver handle divergent indexing within a warp/wavefront
    vec4 textureColor = texture(globalTextures[nonuniformEXT(texIndex)], fragTexCoord);

    vec3 result = (ambient + diffuse) * textureColor.rgb;

    if (selected)
    {
        // A simple "editor highlight" tint toward orange.
        // Keep shading but bias the albedo so the selection is obvious.
        const vec3 highlight = vec3(1.0, 0.55, 0.0);
        result = mix(result, highlight, 0.55);
    }

    outColor = vec4(result, 1.0);
}