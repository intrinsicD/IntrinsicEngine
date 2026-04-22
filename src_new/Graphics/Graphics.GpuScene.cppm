module;

#include <cstdint>
#include <memory>
#include <span>

export module Extrinsic.Graphics.GpuScene;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.BufferManager;

export namespace Extrinsic::Graphics
{
    // GpuScene is the geometry suballocator for the GPU scene.
    //
    // Responsibility (narrow):
    //   - Owns static vertex + index buffers; suballocates into them per entity.
    //   - Owns per-entity dynamic geometry buffers (host-visible, updated on dirty).
    //   - Hands out CullingSlotIndex handles via AllocateSlot() / FreeSlot().
    //     The slot index is used by CullingSystem (rendering bounds) and
    //     TransformSyncSystem (transform SSBO) to address their own per-slot arrays.
    //
    // NOT responsible for:
    //   - Bounding spheres / BoundsBuffer     →  CullingSystem   (rendering frustum cull)
    //   - Collision bounds / ConvexHull        →  Culling.Proxy ECS component + PhysicsSystem
    //   - World transforms / TransformsBuffer  →  TransformSyncSystem
    //   - Materials / MaterialsBuffer          →  MaterialRegistry
    //   - Lights / LightBuffer                 →  LightSystem
    //   - Shadow maps                          →  ShadowSystem
    //   - Culling output (DrawCommandBuffer)   →  CullingSystem
    class GpuScene
    {
    public:
        GpuScene(RHI::IDevice& device, RHI::BufferManager& bufferManager);
        ~GpuScene();

        GpuScene(const GpuScene&)            = delete;
        GpuScene& operator=(const GpuScene&) = delete;

        // --- Slot management ---
        // Lifecycle systems call AllocateSlot() when an entity enters the GPU scene.
        // The returned index is stored on GpuSceneSlot::CullingSlotIndex.
        // FreeSlot() must be called from the on_destroy hook.
        [[nodiscard]] uint32_t AllocateSlot();
        void                   FreeSlot(uint32_t slot);

        // --- Static geometry suballocation ---
        // Returns the byte offset into m_StaticVertexBuffer where data was written.
        // Caller stores {offset, count} in its own GpuSceneSlot NamedBuffer entry.
        [[nodiscard]] uint32_t UploadStaticVertices(std::span<const std::byte> data);
        [[nodiscard]] uint32_t UploadStaticIndices(std::span<const std::byte> data);

        // --- Dynamic geometry ---
        // Creates or updates a named per-entity buffer (host-visible, updated on dirty).
        // The resulting BufferHandle is stored in GpuSceneSlot::Buffers by the caller.
        [[nodiscard]] RHI::BufferHandle AllocateDynamicBuffer(uint32_t sizeBytes);
        void                            UpdateDynamicBuffer(RHI::BufferHandle handle,
                                                            std::span<const std::byte> data);
        void                            FreeDynamicBuffer(RHI::BufferHandle handle);

        [[nodiscard]] RHI::BufferHandle StaticVertexBuffer() const noexcept;
        [[nodiscard]] RHI::BufferHandle StaticIndexBuffer()  const noexcept;

        [[nodiscard]] uint32_t AllocatedSlotCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
