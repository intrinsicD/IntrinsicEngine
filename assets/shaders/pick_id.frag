#version 460

layout(location = 0) out uint outID;

layout(push_constant) uniform PickPushConsts {
    mat4 model;
    uint entityID;
} push;

void main() {
    outID = push.entityID;
}

