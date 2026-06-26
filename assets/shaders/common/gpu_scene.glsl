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
const uint GpuMaterialType_StandardPBR = 0u;
const uint GpuMaterialType_SciVis = 1u;
const uint GpuMaterialType_DefaultDebugSurface = 2u;
const uint GpuMaterialType_DefaultDebugUVs = 3u;
const uint GpuMaterialFlag_Unlit = 1u << 3;
const uint GpuMaterialFlag_ObjectSpaceNormalMap = 1u << 5;
// Single lit/unlit authority (GpuMaterialSlot.ShadingModel). The legacy
// GpuMaterialFlag_Unlit bit is honored as a transitional alias until its
// remaining writers migrate to ShadingModel.
const uint GpuShadingModel_Lit = 0u;
const uint GpuShadingModel_Unlit = 1u;
const uint GpuColorSource_Material = 0u;
const uint GpuColorSource_UniformColor = 1u;
const uint GpuColorSource_ScalarField = 2u;
const uint GpuColorSource_PerElementRgba = 3u;
const uint GpuVisualizationDomain_Vertex = 0u;
const uint GpuVisualizationDomain_Face = 1u;
const uint GpuVisualizationDomain_Edge = 2u;
const uint GpuInvalidBindlessIndex = 0u;

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

    mat4 CameraView;
    mat4 CameraProj;
    mat4 CameraViewProj;
    mat4 CameraInvView;
    mat4 CameraInvProj;
    vec4 CameraPosition;
    vec4 CameraDirection;

    float CameraViewportWidth;
    float CameraViewportHeight;
    float CameraNearPlane;
    float CameraFarPlane;

    uint CameraFrameIndex;
    uint CameraCullingFlags;
    uint CameraPad0;
    uint CameraPad1;
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
    uint64_t TexcoordBufferBDA;
    uint64_t NormalBufferBDA;

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
    uint64_t ColorBufferBDA;
};

struct GpuEntityPointConfig {
    float PointSize;
    uint PointMode;
    uint64_t PointSizeBDA;
};

struct GpuEntityLineConfig {
    float LineWidth;
    uint _pad0;
    uint64_t LineWidthBDA;
};

struct GpuEntityConfig {
    uint64_t VertexNormalBDA;
    uint64_t ScalarBDA;
    uint64_t ColorBDA;
    float ScalarRangeMin;
    float ScalarRangeMax;
    uint ColormapID;
    uint BinCount;

    float IsolineCount;
    float IsolineWidth;
    float VisualizationAlpha;
    uint VisDomain;
    uint ColorSourceMode;
    uint ElementCount;

    vec4 IsolineColor;

    GpuEntityPointConfig Point;
    GpuEntityLineConfig Line;
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
    uint ShadingModel;
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
    GpuCullBucketPhases LineQuads;
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

layout(buffer_reference, scalar) readonly buffer GpuFloatBufferRef {
    float Data[];
};

layout(buffer_reference, scalar) readonly buffer GpuVec4BufferRef {
    vec4 Data[];
};

layout(buffer_reference, scalar) readonly buffer GpuUIntBufferRef {
    uint Data[];
};

layout(buffer_reference, scalar) readonly buffer GpuBoundsRef {
    GpuBounds Data[];
};

vec2 GpuReadPackedVec2(uint64_t bda, uint elementId)
{
    GpuFloatBufferRef values = GpuFloatBufferRef(bda);
    const uint base = elementId * 2u;
    return vec2(values.Data[base], values.Data[base + 1u]);
}

vec3 GpuReadPackedVec3(uint64_t bda, uint elementId)
{
    GpuFloatBufferRef values = GpuFloatBufferRef(bda);
    const uint base = elementId * 3u;
    return vec3(values.Data[base], values.Data[base + 1u], values.Data[base + 2u]);
}


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

bool GpuVisualizationHasElement(GpuEntityConfig cfg, uint elementId)
{
    return elementId < cfg.ElementCount;
}

bool GpuVisualizationHasValidBindless(uint id)
{
    return id != GpuInvalidBindlessIndex;
}

float GpuVisualizationReadScalar(GpuEntityConfig cfg, uint elementId, float fallback)
{
    if (cfg.ScalarBDA == uint64_t(0) || !GpuVisualizationHasElement(cfg, elementId)) {
        return fallback;
    }
    return GpuFloatBufferRef(cfg.ScalarBDA).Data[elementId];
}

vec4 GpuVisualizationReadColor(GpuEntityConfig cfg, uint elementId, vec4 fallback)
{
    if (cfg.ColorBDA == uint64_t(0) || !GpuVisualizationHasElement(cfg, elementId)) {
        return fallback;
    }
    return GpuVec4BufferRef(cfg.ColorBDA).Data[elementId];
}

float GpuVisualizationNormalizedScalar(GpuEntityConfig cfg, float scalarValue)
{
    const float range = cfg.ScalarRangeMax - cfg.ScalarRangeMin;
    if (abs(range) <= 1.0e-20) {
        return 0.0;
    }

    float t = clamp((scalarValue - cfg.ScalarRangeMin) / range, 0.0, 1.0);
    if (cfg.BinCount > 0u) {
        const float bins = float(cfg.BinCount);
        t = (floor(t * bins) + 0.5) / bins;
        t = clamp(t, 0.0, 1.0);
    }
    return t;
}

vec4 GpuVisualizationApplyIsolines(GpuEntityConfig cfg, float t, vec4 color)
{
    if (cfg.IsolineCount <= 0.0 || cfg.IsolineWidth <= 0.0) {
        return color;
    }

    const float phase = abs(fract(t * cfg.IsolineCount) - 0.5);
    const float width = clamp(cfg.IsolineWidth / 128.0, 0.001, 0.5);
    const float lineMix = 1.0 - smoothstep(0.0, width, phase);
    color.rgb = mix(color.rgb, cfg.IsolineColor.rgb, lineMix * cfg.IsolineColor.a);
    return color;
}

vec4 GpuResolveVisualizationColorFallback(GpuEntityConfig cfg,
                                          vec4 perElementColor,
                                          vec4 baseColor)
{
    if (cfg.ColorSourceMode == GpuColorSource_UniformColor) {
        return cfg.UniformColor;
    }
    if (cfg.ColorSourceMode == GpuColorSource_PerElementRgba) {
        vec4 color = perElementColor;
        color.a *= cfg.VisualizationAlpha;
        return color;
    }
    return baseColor;
}

vec4 GpuResolveVisualizationColorWithColormap(GpuEntityConfig cfg,
                                              float scalarValue,
                                              vec4 perElementColor,
                                              vec4 baseColor,
                                              sampler2D colormapLut)
{
    if (cfg.ColorSourceMode != GpuColorSource_ScalarField) {
        return GpuResolveVisualizationColorFallback(cfg, perElementColor, baseColor);
    }

    const float t = GpuVisualizationNormalizedScalar(cfg, scalarValue);
    vec4 color = texture(colormapLut, vec2(t, 0.5));
    color.a = 1.0;
    color = GpuVisualizationApplyIsolines(cfg, t, color);
    color.a *= cfg.VisualizationAlpha;
    return color;
}

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
