//pick_id.vert
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(buffer_reference, scalar) readonly buffer PositionBuffer {
    vec3 values[];
};

// Set 0, Binding 0 is Camera UBO
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

layout(push_constant) uniform PickPushConsts {
    mat4 model;
    uint64_t ptrPos;
    uint64_t ptrNorm;
    uint64_t ptrAux;
    uint entityID;
} push;

void main() {
    PositionBuffer positions = PositionBuffer(push.ptrPos);
    vec3 inPos = positions.values[gl_VertexIndex];
    gl_Position = camera.proj * camera.view * push.model * vec4(inPos, 1.0);
}

