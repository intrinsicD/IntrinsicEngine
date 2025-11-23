#version 460

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D texSampler;

void main() {
    // 1. Define a hardcoded light source (Sunlight coming from top-right)
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 lightColor = vec3(1.0, 1.0, 1.0);

    // 2. Ambient Light (Constant base brightness so shadows aren't pitch black)
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * lightColor;

    // 3. Diffuse Light (Lambertian)
    // Calculate angle between Surface Normal and Light Direction
    vec3 norm = normalize(fragNormal);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // 4. Sample Texture
    vec4 textureColor = texture(texSampler, fragTexCoord);

    // 5. Combine
    vec3 result = (ambient + diffuse) * textureColor.rgb;

    outColor = vec4(result, 1.0);
}