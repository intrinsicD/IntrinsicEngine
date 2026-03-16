// pick_mesh.vert — MRT picking vertex shader for mesh triangles.
//
// Reads positions via BDA. Passes EntityID and PrimitiveBase to the
// fragment shader for dual-channel (EntityID + PrimitiveID) output.

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
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrAux;
    uint64_t PtrPrimitiveFaceIds;
    uint     EntityID;
    uint     PrimitiveBase;
    float    PickWidth;
    float    ViewportWidth;
    float    ViewportHeight;
    uint     _pad;
} push;

void main() {
    PositionBuffer positions = PositionBuffer(push.PtrPositions);
    vec3 inPos = positions.values[gl_VertexIndex];
    gl_Position = camera.proj * camera.view * push.Model * vec4(inPos, 1.0);
}
