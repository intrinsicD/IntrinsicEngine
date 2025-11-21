#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

// Set 0, Binding 0: Camera Data (Changes once per frame)
layout(binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

// Push Constant: Per-Object Data (Changes every draw call)
layout(push_constant) uniform PushConsts {
    mat4 model;
} push;

void main() {
    // Combine: Proj * View * Model * Pos
    gl_Position = camera.proj * camera.view * push.model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}