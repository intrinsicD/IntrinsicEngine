#version 460

layout(location = 0) in vec3 inPosition;

// Set 0, Binding 0 is Camera UBO
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

layout(push_constant) uniform PickPushConsts {
    mat4 model;
    uint entityID;
} push;

void main() {
    gl_Position = camera.proj * camera.view * push.model * vec4(inPosition, 1.0);
}

