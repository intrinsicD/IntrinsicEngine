//pick_id.frag
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) out uint outID;

layout(push_constant) uniform PickPushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    uint64_t PtrAux;
    uint     EntityID;
} push;

void main() {
    outID = push.EntityID;
}

