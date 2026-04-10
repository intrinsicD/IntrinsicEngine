//surface.vert
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Set 0, Binding 0 is Camera UBO (includes shadow cascade data)
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
    mat4 shadowCascadeMatrices[4];
    vec4 shadowCascadeSplitsAndCount;   // x/y/z/w = split distances
    vec4 shadowBiasAndFilter;           // x = depth bias, y = normal bias, z = PCF radius, w = cascade count
} camera;

layout(buffer_reference, scalar) readonly buffer PosBuf  { vec3 v[]; };
layout(buffer_reference, scalar) readonly buffer NormBuf { vec3 v[]; };
layout(buffer_reference, scalar) readonly buffer AuxBuf  { vec4 v[]; };

// Per-vertex color buffer (optional BDA — when PtrVertexAttr != 0).
layout(buffer_reference, scalar) readonly buffer VertexAttrBuf { uint color[]; };

struct InstanceData {
    mat4 Model;
    uint MaterialSlot;
    uint EntityID;
    uint GeometryID;
    uint Pad1;
};

struct MaterialData {
    vec4  BaseColorFactor;
    float MetallicFactor;
    float RoughnessFactor;
    uint  AlbedoID;
    uint  NormalID;
    uint  MetallicRoughnessID;
    uint  Flags;
    uint  _pad0;
    uint  _pad1;
};

// Stage 1: CPU uploads a full instance array + identity visible remap.
layout(std430, set = 2, binding = 0) readonly buffer AllInstances {
    InstanceData Data[];
} instances;

layout(std430, set = 2, binding = 1) readonly buffer Visibility {
    uint VisibleRemap[];
} visibility;

// Material SSBO: per-material PBR data indexed by InstanceData.MaterialSlot.
layout(std430, set = 3, binding = 0) readonly buffer MaterialBuffer {
    MaterialData Materials[];
} materials;

layout(push_constant) uniform PushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    uint64_t PtrAux;
    uint     VisibilityBase; // Base offset into VisibleRemap[] for this geometry batch
    float    PointSizePx;    // Used when drawing VK_PRIMITIVE_TOPOLOGY_POINT_LIST via the Forward pass.
    uint64_t PtrFaceAttr;    // BDA to per-face packed ABGR colors (0 = standard shading)
    uint64_t PtrVertexAttr;  // BDA to per-vertex packed ABGR colors or labels (0 = none)
    uint64_t PtrIndices;     // BDA to index buffer (uint32[]) — enables nearest-vertex label rendering
    uint64_t PtrCentroids;   // BDA to centroid buffer ({vec3 pos, uint color}[]) — centroid Voronoi
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint fragTexID;
layout(location = 3) out vec4 fragVertexColor;
layout(location = 4) out vec3 fragObjectPos;
layout(location = 5) out vec3 fragWorldPos;
layout(location = 6) flat out uint fragMaterialSlot;

void main() {
    PosBuf  pBuf = PosBuf(push.PtrPositions);
    NormBuf nBuf = NormBuf(push.PtrNormals);
    AuxBuf  aBuf = AuxBuf(push.PtrAux);

    // Read SoA
    vec3 inPos = pBuf.v[gl_VertexIndex];
    vec3 inNorm = nBuf.v[gl_VertexIndex];
    vec2 inUV = aBuf.v[gl_VertexIndex].xy;

    // Stage 2: firstInstance is written by the CPU into VkDrawIndexedIndirectCommand.
    // Vulkan defines gl_InstanceIndex = firstInstance + localInstance.
    // For multi-geometry batching, we add VisibilityBase to index the correct slice.
    uint globalInstanceID = visibility.VisibleRemap[push.VisibilityBase + gl_InstanceIndex];
    InstanceData inst = instances.Data[globalInstanceID];

    vec4 worldPos = inst.Model * vec4(inPos, 1.0);
    gl_Position = camera.proj * camera.view * worldPos;

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

    // Per-vertex color: read from BDA buffer when PtrVertexAttr != 0.
    // Interpolated across the triangle to the fragment shader.
    // When PtrCentroids != 0, PtrVertexAttr holds labels (not colors) —
    // skip interpolation; the fragment shader reads labels via BDA directly.
    if (push.PtrVertexAttr != 0ul && push.PtrCentroids == 0ul)
    {
        VertexAttrBuf vaBuf = VertexAttrBuf(push.PtrVertexAttr);
        fragVertexColor = unpackUnorm4x8(vaBuf.color[gl_VertexIndex]);
    }
    else
    {
        fragVertexColor = vec4(0.0);
    }

    // Object-space position for nearest-vertex Voronoi rendering.
    fragObjectPos = inPos;

    // World-space position for shadow coordinate computation.
    fragWorldPos = worldPos.xyz;

    fragTexCoord = inUV;
    fragMaterialSlot = inst.MaterialSlot;
    MaterialData mat = materials.Materials[inst.MaterialSlot];
    fragTexID = mat.AlbedoID;
}
