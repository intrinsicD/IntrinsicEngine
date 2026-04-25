module;

#include <cstdint>
#include <glm/glm.hpp>

export module Extrinsic.RHI.Types;

// ============================================================
// RHI.Types — std430-compatible GPU data layout structs.
//
// These types cross the CPU/GPU boundary via UBOs and SSBOs.
// Layout rules:
//   - alignas(16) for vec3/vec4/mat4 members (std140/std430 base alignment).
//   - No virtual functions, no destructors — plain-old data.
//   - Fields ordered by descending alignment to avoid implicit padding.
// ============================================================

export namespace Extrinsic::RHI
{

    enum class IndexType : std::uint8_t
    {
        Uint16,
        Uint32,
    };

    enum GpuRenderFlags : std::uint32_t
    {
        GpuRender_None        = 0,
        GpuRender_Surface     = 1u << 0,
        GpuRender_Line        = 1u << 1,
        GpuRender_Point       = 1u << 2,
        GpuRender_CastShadow  = 1u << 3,
        GpuRender_Opaque      = 1u << 4,
        GpuRender_AlphaMask   = 1u << 5,
        GpuRender_Transparent = 1u << 6,
        GpuRender_Unlit       = 1u << 7,
        GpuRender_FlatShading = 1u << 8,
        GpuRender_Visible     = 1u << 31,
    };

    enum class GpuDrawBucketKind : std::uint32_t
    {
        SurfaceOpaque = 0,
        SurfaceAlphaMask,
        Lines,
        Points,
        ShadowOpaque,
        Count
    };

    struct alignas(16) GpuGeometryRecord
    {
        std::uint64_t VertexBufferBDA = 0;
        std::uint64_t IndexBufferBDA  = 0;
        std::uint32_t VertexOffset = 0;
        std::uint32_t VertexCount  = 0;
        std::uint32_t SurfaceFirstIndex = 0;
        std::uint32_t SurfaceIndexCount = 0;
        std::uint32_t LineFirstIndex = 0;
        std::uint32_t LineIndexCount = 0;
        std::uint32_t PointFirstVertex = 0;
        std::uint32_t PointVertexCount = 0;
        std::uint32_t BufferID = 0;
        std::uint32_t Flags = 0;
    };
    static_assert(sizeof(GpuGeometryRecord) == 64);

    struct alignas(16) GpuInstanceStatic
    {
        static constexpr std::uint32_t InvalidGeometrySlot = 0xFFFF'FFFFu;
        std::uint32_t GeometrySlot = InvalidGeometrySlot;
        std::uint32_t MaterialSlot = 0;
        std::uint32_t EntityID     = 0;
        std::uint32_t RenderFlags  = 0;
        std::uint32_t VisibilityMask = 0xFFFF'FFFFu;
        std::uint32_t Layer          = 0;
        std::uint32_t ConfigSlot     = 0;
        std::uint32_t _pad0          = 0;
    };
    static_assert(sizeof(GpuInstanceStatic) == 32);

    struct alignas(16) GpuInstanceDynamic
    {
        alignas(16) glm::mat4 Model{1.f};
        alignas(16) glm::mat4 PrevModel{1.f};
    };
    static_assert(sizeof(GpuInstanceDynamic) == 128);

    struct alignas(16) GpuEntityConfig
    {
        std::uint64_t VertexNormalBDA = 0;
        std::uint64_t ScalarBDA       = 0;
        std::uint64_t ColorBDA        = 0;
        std::uint64_t PointSizeBDA    = 0;
        float ScalarRangeMin = 0.f;
        float ScalarRangeMax = 1.f;
        std::uint32_t ColormapID = 0;
        std::uint32_t BinCount = 0;
        float IsolineCount = 0.f;
        float IsolineWidth = 0.f;
        float VisualizationAlpha = 1.f;
        std::uint32_t VisDomain = 0;
        alignas(16) glm::vec4 IsolineColor{0.f, 0.f, 0.f, 1.f};
        float PointSize = 1.f;
        std::uint32_t PointMode = 0;
        std::uint32_t ColorSourceMode = 0;
        std::uint32_t ElementCount = 0;
        alignas(16) glm::vec4 UniformColor{1.f};
    };
    static_assert(sizeof(GpuEntityConfig) == 128);

    struct alignas(16) GpuBounds
    {
        alignas(16) glm::vec4 LocalSphere{0.f};
        alignas(16) glm::vec4 WorldSphere{0.f};
        alignas(16) glm::vec4 WorldAabbMin{0.f};
        alignas(16) glm::vec4 WorldAabbMax{0.f};
    };
    static_assert(sizeof(GpuBounds) == 64);

    struct alignas(16) GpuLight
    {
        alignas(16) glm::vec4 Position_Range{0.f};
        alignas(16) glm::vec4 Direction_Type{0.f};
        alignas(16) glm::vec4 Color_Intensity{1.f};
        alignas(16) glm::vec4 Params{0.f};
    };
    static_assert(sizeof(GpuLight) == 64);

    struct alignas(16) GpuSceneTable
    {
        std::uint64_t InstanceStaticBDA  = 0;
        std::uint64_t InstanceDynamicBDA = 0;
        std::uint64_t EntityConfigBDA    = 0;
        std::uint64_t GeometryRecordBDA  = 0;
        std::uint64_t BoundsBDA          = 0;
        std::uint64_t MaterialBDA        = 0;
        std::uint64_t LightBDA           = 0;
        std::uint64_t _padBDA            = 0;
        std::uint32_t InstanceCapacity   = 0;
        std::uint32_t GeometryCapacity   = 0;
        std::uint32_t MaterialCapacity   = 0;
        std::uint32_t LightCount         = 0;
    };
    static_assert(sizeof(GpuSceneTable) == 80);

    struct alignas(16) GpuScenePushConstants
    {
        std::uint64_t SceneTableBDA = 0;
        std::uint32_t FrameIndex = 0;
        std::uint32_t DrawBucket = 0;
        std::uint32_t DebugMode = 0;
        std::uint32_t _pad0 = 0;
    };
    static_assert(sizeof(GpuScenePushConstants) <= 128);

    struct alignas(16) GpuCullPushConstants
    {
        alignas(16) glm::vec4 FrustumPlanes[6];
        std::uint64_t SceneTableBDA = 0;
        std::uint64_t CullBucketTableBDA = 0;
        std::uint32_t InstanceCapacity = 0;
        std::uint32_t _pad0 = 0;
    };
    static_assert(sizeof(GpuCullPushConstants) <= 128);

    struct alignas(8) GpuCullBucketOutput
    {
        std::uint64_t ArgsBDA = 0;
        std::uint64_t CountBDA = 0;
        std::uint32_t Capacity = 0;
        std::uint32_t _pad0 = 0;
    };
    static_assert(sizeof(GpuCullBucketOutput) == 24);

    static_assert(alignof(GpuCullBucketOutput) == 8);

    struct alignas(8) GpuCullBucketTable
    {
        GpuCullBucketOutput SurfaceOpaque{};
        GpuCullBucketOutput SurfaceAlphaMask{};
        GpuCullBucketOutput Lines{};
        GpuCullBucketOutput Points{};
        GpuCullBucketOutput ShadowOpaque{};
    };
    static_assert(sizeof(GpuCullBucketTable) == 120);
    static_assert(alignof(GpuCullBucketTable) == 8);
    // -------------------------------------------------------
    // Per-frame camera + lighting UBO  (set 0, binding 0)
    // Matches the global descriptor set consumed by every lit pass.
    // -------------------------------------------------------
    struct alignas(16) CameraUBO
    {
        alignas(16) glm::mat4 View{1.f};
        alignas(16) glm::mat4 Proj{1.f};
        alignas(16) glm::mat4 ViewProj{1.f};
        alignas(16) glm::mat4 InvView{1.f};
        alignas(16) glm::mat4 InvProj{1.f};

        // Camera geometry
        alignas(16) glm::vec4 CameraPosition{0.f}; // xyz = world pos, w = unused
        alignas(16) glm::vec4 CameraDirection{0.f}; // xyz = forward vec, w = unused

        // Directional light  (xyz = normalised direction, w = intensity)
        alignas(16) glm::vec4 LightDirAndIntensity{0.f, -1.f, 0.f, 1.f};
        alignas(16) glm::vec4 LightColor{1.f};            // xyz = RGB, w = unused
        alignas(16) glm::vec4 AmbientColorAndIntensity{0.2f, 0.2f, 0.2f, 1.f};

        // Viewport
        float ViewportWidth  = 0.f;
        float ViewportHeight = 0.f;
        float NearPlane      = 0.01f;
        float FarPlane       = 1000.f;

        // Frame counters (useful for temporal effects / jitter)
        std::uint32_t FrameIndex  = 0;
        std::uint32_t _pad0 = 0, _pad1 = 0, _pad2 = 0;
    };

    // -------------------------------------------------------
    // Per-instance GPU data  (SSBO, set 2, binding 0)
    // One entry per draw call in the GPU scene.
    // std430: mat4 = 64 bytes (16-byte aligned), uint32 = 4 bytes.
    // -------------------------------------------------------
    struct GpuInstanceData
    {
        alignas(16) glm::mat4 Model{1.f};  // world-space model matrix — 64 bytes
        std::uint32_t MaterialSlot = 0;    // index into MaterialSSBO (set 3)
        std::uint32_t EntityID     = 0;    // entt entity id for GPU picking
        std::uint32_t GeometryID   = 0;    // index into geometry pool
        std::uint32_t Flags        = 0;    // reserved (visibility, LOD, shadow caster, etc.)
    };

    // -------------------------------------------------------
    // Per-material GPU data  (SSBO, set 3, binding 0)
    // 48-byte std430 struct.  Matches `GpuMaterialData` in src/.
    // -------------------------------------------------------
    struct GpuMaterialData
    {
        alignas(16) glm::vec4 BaseColorFactor{1.f}; // RGBA tint (16 bytes)
        float MetallicFactor   = 0.f;               //  4 bytes
        float RoughnessFactor  = 0.5f;              //  4 bytes
        std::uint32_t AlbedoID            = 0;      //  4 bytes — bindless texture index
        std::uint32_t NormalID            = 0;      //  4 bytes — bindless texture index
        std::uint32_t MetallicRoughnessID = 0;      //  4 bytes — bindless texture index
        std::uint32_t EmissiveID          = 0;      //  4 bytes — bindless texture index
        std::uint32_t Flags               = 0;      //  4 bytes — alpha mode, double-sided, etc.
        std::uint32_t _pad0               = 0;      //  4 bytes — keeps total = 48 bytes
    };
    static_assert(sizeof(GpuMaterialData) == 48, "GpuMaterialData must be 48 bytes (std430)");

    // -------------------------------------------------------
    // Extended material slot  (MaterialSystem SSBO, set 3, binding 0)
    // 128-byte std430 struct used by Graphics::MaterialSystem.
    //
    // Layout:
    //   Bytes   0–47  — standard PBR fields (superset of GpuMaterialData)
    //   Bytes  48–51  — MaterialTypeID   (shader branches on this)
    //   Bytes  52–63  — padding to next vec4 boundary
    //   Bytes  64–127 — CustomData[4]   (4 × vec4, user-defined per-type)
    //
    // Shaders index this SSBO by MaterialSlot from GpuInstanceData.
    // Custom material types write their params into CustomData and
    // switch on MaterialTypeID to select the shading model.
    // -------------------------------------------------------
    struct GpuMaterialSlot
    {
        // Standard PBR  (bytes 0–47, matches GpuMaterialData layout)
        alignas(16) glm::vec4 BaseColorFactor{1.f};
        float        MetallicFactor        = 0.f;
        float        RoughnessFactor       = 0.5f;
        std::uint32_t AlbedoID            = 0;   // BindlessIndex
        std::uint32_t NormalID            = 0;   // BindlessIndex
        std::uint32_t MetallicRoughnessID = 0;   // BindlessIndex
        std::uint32_t EmissiveID          = 0;   // BindlessIndex
        std::uint32_t MaterialTypeID      = 0;   // registered type index
        std::uint32_t Flags               = 0;   // MaterialFlags bitmask

        // Padding to align CustomData to byte 64
        std::uint32_t _pad0 = 0;
        std::uint32_t _pad1 = 0;
        std::uint32_t _pad2 = 0;
        std::uint32_t _pad3 = 0;

        // Custom params  (bytes 64–127)
        alignas(16) glm::vec4 CustomData[4]{};
    };
    static_assert(sizeof(GpuMaterialSlot) == 128, "GpuMaterialSlot must be 128 bytes (std430)");

    // -------------------------------------------------------
    // Bounding sphere for GPU-side culling
    // -------------------------------------------------------
    struct BoundingSphere
    {
        alignas(16) glm::vec4 CenterAndRadius{0.f}; // xyz = center, w = radius
    };

    // -------------------------------------------------------
    // Indirect indexed draw command  (VkDrawIndexedIndirectCommand)
    // Written by the cull compute shader into indexed draw-command
    // SSBO; consumed by DrawIndexedIndirectCount.
    // -------------------------------------------------------
    struct GpuDrawIndexedCommand
    {
        std::uint32_t IndexCount    = 0;
        std::uint32_t InstanceCount = 0;
        std::uint32_t FirstIndex    = 0;
        std::int32_t  VertexOffset  = 0;
        std::uint32_t FirstInstance = 0;
    };
    static_assert(sizeof(GpuDrawIndexedCommand) == 20);

    // -------------------------------------------------------
    // Indirect non-indexed draw command  (VkDrawIndirectCommand)
    // Used for point visualization buckets consumed via DrawIndirectCount.
    // -------------------------------------------------------
    struct GpuDrawCommand
    {
        std::uint32_t VertexCount   = 0;
        std::uint32_t InstanceCount = 0;
        std::uint32_t FirstVertex   = 0;
        std::uint32_t FirstInstance = 0;
    };
    static_assert(sizeof(GpuDrawCommand) == 16);

    // -------------------------------------------------------
    // Culling compute push constants
    // Exactly 128 bytes — fills the guaranteed Vulkan limit.
    //
    // Frustum planes: Gribb-Hartmann extraction from ViewProj.
    //   plane.xyz = normal (not necessarily unit length),
    //   plane.w   = signed distance from origin.
    //   Point p is inside if dot(plane.xyz, p) + plane.w >= 0.
    //
    // BDAs let the compute shader read/write all three buffers
    // via push constants alone — zero descriptor-set binding.
    // -------------------------------------------------------
    struct CullPushConstants
    {
        alignas(16) glm::vec4 FrustumPlanes[6]; //  96 bytes  (6 planes)
        std::uint32_t         DrawCount = 0;     //   4 bytes  (total entries)
        std::uint32_t         _pad      = 0;     //   4 bytes
        std::uint64_t         CullDataBDA       = 0; //  8 bytes  (input  GpuCullData[])
        std::uint64_t         DrawCommandBDA    = 0; //  8 bytes  (output GpuDrawCommand[])
        std::uint64_t         VisibilityCountBDA= 0; //  8 bytes  (output uint  atomic counter)
    };
    static_assert(sizeof(CullPushConstants) == 128);

    // -------------------------------------------------------
    // GPU Culling instance (indirect draw input)
    // -------------------------------------------------------
    struct GpuCullData
    {
        BoundingSphere Sphere{};
        std::uint32_t InstanceID = 0; // index into GpuInstanceData SSBO
        std::uint32_t DrawID     = 0; // index into indirect draw command buffer
        std::uint32_t _pad0 = 0, _pad1 = 0;
    };
}
