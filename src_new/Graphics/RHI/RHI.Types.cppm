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
    // Indirect draw command  (VkDrawIndexedIndirectCommand)
    // Written by the cull compute shader into the draw-command
    // SSBO; consumed by DrawIndexedIndirectCount.
    // -------------------------------------------------------
    struct GpuDrawCommand
    {
        std::uint32_t IndexCount    = 0;
        std::uint32_t InstanceCount = 0;
        std::uint32_t FirstIndex    = 0;
        std::int32_t  VertexOffset  = 0;
        std::uint32_t FirstInstance = 0;
    };
    static_assert(sizeof(GpuDrawCommand) == 20);

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

