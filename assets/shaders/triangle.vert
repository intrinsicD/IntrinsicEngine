//triangle.vert
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Set 0, Binding 0 is Camera UBO
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

layout(buffer_reference, scalar) readonly buffer PosBuf  { vec3 v[]; };
layout(buffer_reference, scalar) readonly buffer NormBuf { vec3 v[]; };
layout(buffer_reference, scalar) readonly buffer AuxBuf  { vec4 v[]; };

struct InstanceData {
    mat4 Model;
    uint TextureID;
    uint EntityID;
    uint Pad0;
    uint Pad1;
};

// Stage 1: CPU uploads a full instance array + identity visible remap.
layout(std430, set = 2, binding = 0) readonly buffer AllInstances {
    InstanceData Data[];
} instances;

layout(std430, set = 2, binding = 1) readonly buffer Visibility {
    uint VisibleRemap[];
} visibility;

layout(push_constant) uniform PushConsts {
    mat4 _unusedModel;
    uint64_t ptrPos;
    uint64_t ptrNorm;
    uint64_t ptrAux;
    uint _unusedInstanceID; // Stage 2: unused; use gl_InstanceIndex instead
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint fragTexID;

void main() {
    PosBuf  pBuf = PosBuf(push.ptrPos);
    NormBuf nBuf = NormBuf(push.ptrNorm);
    AuxBuf  aBuf = AuxBuf(push.ptrAux);

    // Read SoA
    vec3 inPos = pBuf.v[gl_VertexIndex];
    vec3 inNorm = nBuf.v[gl_VertexIndex];
    vec2 inUV = aBuf.v[gl_VertexIndex].xy;

    // Stage 2: firstInstance is written by the CPU into VkDrawIndexedIndirectCommand.
    // Vulkan defines gl_InstanceIndex = firstInstance + localInstance.
    uint globalInstanceID = visibility.VisibleRemap[gl_InstanceIndex];
    InstanceData inst = instances.Data[globalInstanceID];

    gl_Position = camera.proj * camera.view * inst.Model * vec4(inPos, 1.0);
    fragNormal = mat3(inst.Model) * inNorm;
    fragTexCoord = inUV;
    fragTexID = inst.TextureID;
}