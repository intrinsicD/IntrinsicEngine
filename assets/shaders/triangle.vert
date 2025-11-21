#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;

layout(binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

layout(push_constant) uniform PushConsts {
    mat4 model;
} push;

void main() {
    gl_Position = camera.proj * camera.view * push.model * vec4(inPosition, 1.0);

    // Transform normal to world space (simplified, assumes uniform scale)
    fragNormal = mat3(push.model) * inNormal;
    fragTexCoord = inTexCoord;
}