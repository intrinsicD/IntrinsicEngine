#ifndef INTRINSIC_GPU_SCENE_GLSL
#define INTRINSIC_GPU_SCENE_GLSL

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
const uint GpuRender_Selectable  = 1u << 9;
const uint GpuRender_Visible     = 1u << 31;
const uint GpuInvalidGeometrySlot = 0xFFFFFFFFu;
const uint GpuCullPhase_Phase1 = 0u;
const uint GpuCullPhase_Phase2 = 1u;
const uint GpuCullFlag_HZBStaleSkip = 1u << 0;
const uint GpuCullFlag_SelectionBucketOcclusionExempt = 1u << 1;

struct GpuSceneTable {
    uint64_t InstanceStaticBDA;
    uint64_t InstanceDynamicBDA;
    uint64_t EntityConfigBDA;
    uint64_t GeometryRecordBDA;

    uint64_t BoundsBDA;
    uint64_t MaterialBDA;
    uint64_t LightBDA;
    uint64_t ClusterLightHeaderBDA;
    uint64_t ClusterLightIndexBDA;

    uint InstanceCapacity;
    uint GeometryCapacity;
    uint MaterialCapacity;
    uint LightCount;

    uint ClusterTilePx;
    uint ClusterTilesX;
    uint ClusterTilesY;
    uint ClusterSlicesZ;

    uint ClusterCellCount;
    uint ClusterMaxLightsPerCell;
    float ClusterNearZ;
    float ClusterFarZ;

    float ClusterProjectionScaleX;
    float ClusterProjectionScaleY;
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


struct GpuMaterialSlot {
    vec4 BaseColorFactor;
    float MetallicFactor;
    float RoughnessFactor;
    uint AlbedoID;
    uint NormalID;
    uint MetallicRoughnessID;
    uint EmissiveID;
    uint MaterialTypeID;
    uint Flags;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    uint _pad3;
    vec4 CustomData[4];
};

struct GpuLight {
    vec4 Position_Range;
    vec4 Direction_Type;
    vec4 Color_Intensity;
    vec4 Params;
};

struct GpuClusterLightCellHeader {
    uint Offset;
    uint Count;
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

struct GpuCullBucketOutput {
    uint64_t ArgsBDA;
    uint64_t CountBDA;
    uint Capacity;
    uint _pad0;
};

struct GpuCullBucketDiagnosticsCounters {
    uint Phase1VisibleCount;
    uint Phase1RejectedCount;
    uint Phase2RescuedCount;
    uint _pad0;
};

struct GpuCullBucketDiagnosticsOutput {
    uint64_t CountersBDA;
    uint _pad0;
    uint _pad1;
};

struct GpuCullBucketPhases {
    GpuCullBucketOutput Phase1;
    GpuCullBucketOutput Phase2;
    GpuCullBucketDiagnosticsOutput Diagnostics;
};

struct GpuCullBucketTable {
    GpuCullBucketPhases SurfaceOpaque;
    GpuCullBucketPhases SurfaceAlphaMask;
    GpuCullBucketPhases Lines;
    GpuCullBucketPhases Points;
    GpuCullBucketPhases ShadowOpaque;
    GpuCullBucketPhases SelectionSurface;
    GpuCullBucketPhases SelectionLines;
    GpuCullBucketPhases SelectionPoints;
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


layout(buffer_reference, scalar) readonly buffer GpuMaterialSlotRef {
    GpuMaterialSlot Data[];
};

layout(buffer_reference, scalar) readonly buffer GpuLightRef {
    GpuLight Data[];
};

layout(buffer_reference, scalar) readonly buffer GpuClusterLightHeaderRef {
    GpuClusterLightCellHeader Data[];
};

layout(buffer_reference, scalar) readonly buffer GpuClusterLightIndexRef {
    uint Data[];
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

layout(buffer_reference, scalar) readonly buffer GpuCullBucketTableRef {
    GpuCullBucketTable Value;
};

bool GpuClusterLightingAvailable(GpuSceneTable scene)
{
    return scene.ClusterLightHeaderBDA != uint64_t(0) &&
           scene.ClusterLightIndexBDA != uint64_t(0) &&
           scene.ClusterTilePx > 0u &&
           scene.ClusterTilesX > 0u &&
           scene.ClusterTilesY > 0u &&
           scene.ClusterSlicesZ > 0u &&
           scene.ClusterCellCount > 0u &&
           scene.ClusterMaxLightsPerCell > 0u &&
           scene.ClusterNearZ > 0.0 &&
           scene.ClusterFarZ > scene.ClusterNearZ;
}

uint GpuClusterSliceFromPositiveViewZ(float positiveViewZ, GpuSceneTable scene)
{
    if (!GpuClusterLightingAvailable(scene))
    {
        return 0u;
    }

    const float logRange = log(scene.ClusterFarZ / scene.ClusterNearZ);
    if (isnan(logRange) || isinf(logRange) || logRange <= 0.0)
    {
        return 0u;
    }

    const float clampedZ = clamp(max(positiveViewZ, scene.ClusterNearZ),
                                 scene.ClusterNearZ,
                                 scene.ClusterFarZ);
    const float normalized = log(clampedZ / scene.ClusterNearZ) / logRange;
    return min(uint(floor(normalized * float(scene.ClusterSlicesZ))),
               scene.ClusterSlicesZ - 1u);
}

float GpuPositiveViewZFromDeviceDepth(float deviceDepth, GpuSceneTable scene)
{
    if (scene.ClusterNearZ <= 0.0 || scene.ClusterFarZ <= scene.ClusterNearZ)
    {
        return 1.0;
    }

    // Reconstruct a conservative positive view-Z from a 0..1 depth value.
    // This keeps clustered-light indexing live even before the deferred
    // composer samples a depth texture for exact per-fragment reconstruction.
    const float z = clamp(deviceDepth, 0.0, 1.0);
    const float denom = max(scene.ClusterFarZ - z * (scene.ClusterFarZ - scene.ClusterNearZ),
                            0.000001);
    return (scene.ClusterNearZ * scene.ClusterFarZ) / denom;
}

uint GpuClusterCellIndex(vec2 fragCoord, float positiveViewZ, GpuSceneTable scene)
{
    if (!GpuClusterLightingAvailable(scene))
    {
        return 0u;
    }

    const uint tilePx = max(scene.ClusterTilePx, 1u);
    const uint tileX = min(uint(max(fragCoord.x, 0.0)) / tilePx, scene.ClusterTilesX - 1u);
    const uint tileY = min(uint(max(fragCoord.y, 0.0)) / tilePx, scene.ClusterTilesY - 1u);
    const uint sliceZ = GpuClusterSliceFromPositiveViewZ(positiveViewZ, scene);
    const uint cell = (sliceZ * scene.ClusterTilesY + tileY) * scene.ClusterTilesX + tileX;
    return min(cell, scene.ClusterCellCount - 1u);
}

vec3 GpuLightDebugContribution(GpuLight light)
{
    return light.Color_Intensity.xyz * light.Color_Intensity.w;
}

vec3 GpuAccumulateFullSceneLightsDebug(GpuSceneTable scene)
{
    vec3 accum = vec3(0.0);
    if (scene.LightBDA == uint64_t(0))
    {
        return accum;
    }

    GpuLightRef lights = GpuLightRef(scene.LightBDA);
    for (uint i = 0u; i < scene.LightCount; ++i)
    {
        accum += GpuLightDebugContribution(lights.Data[i]);
    }
    return accum;
}

vec3 GpuAccumulateClusteredSceneLightsDebug(GpuSceneTable scene,
                                            vec2 fragCoord,
                                            float positiveViewZ)
{
    if (!GpuClusterLightingAvailable(scene) || scene.LightBDA == uint64_t(0))
    {
        return GpuAccumulateFullSceneLightsDebug(scene);
    }

    GpuLightRef lights = GpuLightRef(scene.LightBDA);
    GpuClusterLightHeaderRef headers = GpuClusterLightHeaderRef(scene.ClusterLightHeaderBDA);
    GpuClusterLightIndexRef indices = GpuClusterLightIndexRef(scene.ClusterLightIndexBDA);

    vec3 accum = vec3(0.0);
    for (uint lightIndex = 0u; lightIndex < scene.LightCount; ++lightIndex)
    {
        const GpuLight light = lights.Data[lightIndex];
        if (uint(round(light.Direction_Type.w)) == 0u)
        {
            accum += GpuLightDebugContribution(light);
        }
    }

    const uint cell = GpuClusterCellIndex(fragCoord, positiveViewZ, scene);
    const GpuClusterLightCellHeader header = headers.Data[cell];
    const uint count = min(header.Count, scene.ClusterMaxLightsPerCell);
    for (uint i = 0u; i < count; ++i)
    {
        const uint lightIndex = indices.Data[header.Offset + i];
        if (lightIndex < scene.LightCount)
        {
            accum += GpuLightDebugContribution(lights.Data[lightIndex]);
        }
    }
    return accum;
}

#endif // INTRINSIC_GPU_SCENE_GLSL
