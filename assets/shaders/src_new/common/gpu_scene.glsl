#ifndef INTRINSIC_SRC_NEW_GPU_SCENE_GLSL
#define INTRINSIC_SRC_NEW_GPU_SCENE_GLSL

#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

const uint GpuRender_None        = 0u;
const uint GpuRender_Surface     = 1u << 0;
const uint GpuRender_Line        = 1u << 1;
const uint GpuRender_Point       = 1u << 2;
const uint GpuRender_CastShadow  = 1u << 3;
const uint GpuRender_Opaque      = 1u << 4;
const uint GpuRender_AlphaMask   = 1u << 5;
const uint GpuRender_Transparent = 1u << 6;
const uint GpuRender_Unlit       = 1u << 7;
const uint GpuRender_FlatShading = 1u << 8;
const uint GpuRender_Visible     = 1u << 31;

struct GpuSceneTable {
    uint64_t InstanceStaticBDA;
    uint64_t InstanceDynamicBDA;
    uint64_t EntityConfigBDA;
    uint64_t GeometryRecordBDA;

    uint64_t BoundsBDA;
    uint64_t MaterialBDA;
    uint64_t LightBDA;
    uint64_t _padBDA;

    uint InstanceCapacity;
    uint GeometryCapacity;
    uint MaterialCapacity;
    uint LightCount;
};

struct GpuInstanceStatic {
    uint GeometrySlot;
    uint MaterialSlot;
    uint EntityID;
    uint RenderFlags;

    uint VisibilityMask;
    uint Layer;
    uint ConfigSlot;
    uint _pad0;
};

struct GpuInstanceDynamic {
    mat4 Model;
    mat4 PrevModel;
};

struct GpuGeometryRecord {
    uint64_t VertexBufferBDA;
    uint64_t IndexBufferBDA;

    uint VertexOffset;
    uint VertexCount;

    uint SurfaceFirstIndex;
    uint SurfaceIndexCount;

    uint LineFirstIndex;
    uint LineIndexCount;

    uint PointFirstVertex;
    uint PointVertexCount;

    uint BufferID;
    uint Flags;
};

struct GpuEntityConfig {
    uint64_t VertexNormalBDA;
    uint64_t ScalarBDA;
    uint64_t ColorBDA;
    uint64_t PointSizeBDA;

    float ScalarRangeMin;
    float ScalarRangeMax;
    uint ColormapID;
    uint BinCount;

    float IsolineCount;
    float IsolineWidth;
    float VisualizationAlpha;
    uint VisDomain;

    vec4 IsolineColor;

    float PointSize;
    uint PointMode;
    uint ColorSourceMode;
    uint ElementCount;

    vec4 UniformColor;
};

struct GpuBounds {
    vec4 LocalSphere;
    vec4 WorldSphere;
    vec4 WorldAabbMin;
    vec4 WorldAabbMax;
};

struct GpuDrawIndexedCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

struct GpuDrawCommand {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};

layout(buffer_reference, scalar) readonly buffer GpuSceneTableRef {
    GpuSceneTable Value;
};

layout(buffer_reference, scalar) readonly buffer GpuInstanceStaticRef {
    GpuInstanceStatic Data[];
};

layout(buffer_reference, scalar) readonly buffer GpuInstanceDynamicRef {
    GpuInstanceDynamic Data[];
};

layout(buffer_reference, scalar) readonly buffer GpuGeometryRecordRef {
    GpuGeometryRecord Data[];
};

layout(buffer_reference, scalar) readonly buffer GpuEntityConfigRef {
    GpuEntityConfig Data[];
};

layout(buffer_reference, scalar) readonly buffer GpuBoundsRef {
    GpuBounds Data[];
};

layout(buffer_reference, scalar) buffer GpuDrawIndexedCommandRef {
    GpuDrawIndexedCommand Data[];
};

layout(buffer_reference, scalar) buffer GpuDrawCommandRef {
    GpuDrawCommand Data[];
};

layout(buffer_reference, scalar) buffer GpuCounterRef {
    uint Value[];
};

#endif // INTRINSIC_SRC_NEW_GPU_SCENE_GLSL
