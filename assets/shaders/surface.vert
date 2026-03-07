//surface.vert
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

// Per-vertex color buffer (optional BDA — when ptrVertexAttr != 0).
layout(buffer_reference, scalar) readonly buffer VertexAttrBuf { uint color[]; };

struct InstanceData {
    mat4 Model;
    uint TextureID;
    uint EntityID;
    uint GeometryID;
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
    uint VisibilityBase; // Base offset into VisibleRemap[] for this geometry batch
    float PointSizePx;  // Used when drawing VK_PRIMITIVE_TOPOLOGY_POINT_LIST via the Forward pass.
    uint64_t ptrFaceAttr; // BDA to per-face packed ABGR colors (0 = standard shading)
    uint64_t ptrVertexAttr; // BDA to per-vertex packed ABGR colors (0 = no per-vertex colors)
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint fragTexID;
layout(location = 3) out vec4 fragVertexColor;

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
    // For multi-geometry batching, we add VisibilityBase to index the correct slice.
    uint globalInstanceID = visibility.VisibleRemap[push.VisibilityBase + gl_InstanceIndex];
    InstanceData inst = instances.Data[globalInstanceID];

    gl_Position = camera.proj * camera.view * inst.Model * vec4(inPos, 1.0);

    // Forward pass point rendering: fixed pixel-size points.
    // The pipeline variant is created with POINT_LIST topology.
    // Note: This is independent of PointCloudRenderPass splat modes.
    gl_PointSize = max(push.PointSizePx, 1.0);

    // Correct normal transform under non-uniform scale / shear.
    mat3 normalMatrix = transpose(inverse(mat3(inst.Model)));
    vec3 transformedNorm = normalMatrix * inNorm;
    float nLen = length(transformedNorm);
    if (nLen > 1e-6) {
        fragNormal = transformedNorm / nLen;
    } else {
        // Fallback to camera-facing basis (view forward in world space).
        fragNormal = -vec3(camera.view[0][2], camera.view[1][2], camera.view[2][2]);
    }

    // Per-vertex color: read from BDA buffer when ptrVertexAttr != 0.
    // Interpolated across the triangle to the fragment shader.
    if (push.ptrVertexAttr != 0ul)
    {
        VertexAttrBuf vaBuf = VertexAttrBuf(push.ptrVertexAttr);
        fragVertexColor = unpackUnorm4x8(vaBuf.color[gl_VertexIndex]);
    }
    else
    {
        fragVertexColor = vec4(0.0);
    }

    fragTexCoord = inUV;
    fragTexID = inst.TextureID;
}
