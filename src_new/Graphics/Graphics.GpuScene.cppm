module;

#include <cstdint>
#include <memory>
#include <span>

export module Extrinsic.Graphics.GpuScene;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.BufferManager;

// ============================================================
// GpuScene — legacy compatibility wrapper.
//
// New renderer code must use GpuWorld for instance/geometry ownership.
// This module remains only to preserve older tests and migration callsites.
//
// Responsibility (narrow):
//   - Owns one large static vertex buffer + one static index buffer;
//     suballocates into them via a bump pointer.  Callers store the
//     returned byte offset.
//   - Owns per-entity dynamic geometry buffers (host-visible, Storage).
//     Leases are held internally; callers hold only the BufferHandle.
//   - Dispenses shared CullingSlotIndex values via AllocateSlot() /
//     FreeSlot().  These indices are the single authoritative slot space
//     shared by CullingSystem (bounds SSBO) and TransformSyncSystem
//     (transform SSBO).
//
// Lifecycle: default-construct, then call Initialize() before any other
//   method.  Call Shutdown() before destruction.  Mirrors CullingSystem.
//
// NOT responsible for:
//   - Bounding-sphere data / frustum cull     →  CullingSystem
//   - World-transform SSBO                    →  TransformSyncSystem
//   - Material SSBO                           →  MaterialSystem
//   - Lights, shadows, post-process           →  their respective Systems
//   - DrawCommandBuffer / visibility counter  →  CullingSystem
// ============================================================

export namespace Extrinsic::Graphics
{
    class GpuScene
    {
    public:
        // Default static-buffer capacities.  Override via Initialize().
        static constexpr uint64_t kDefaultStaticVertexBytes = 64u * 1024u * 1024u;  // 64 MiB
        static constexpr uint64_t kDefaultStaticIndexBytes  = 32u * 1024u * 1024u;  // 32 MiB
        static constexpr uint32_t kDefaultSlotCapacity      = 4096u;

        // -----------------------------------------------------------------
        // Lifecycle
        // -----------------------------------------------------------------
        GpuScene();
        ~GpuScene();

        GpuScene(const GpuScene&)            = delete;
        GpuScene& operator=(const GpuScene&) = delete;

        /// Allocate static buffers and the slot table.
        /// Returns false if GPU allocation fails (logs diagnostics).
        bool Initialize(RHI::IDevice&      device,
                        RHI::BufferManager& bufferMgr,
                        uint64_t staticVertexBytes = kDefaultStaticVertexBytes,
                        uint64_t staticIndexBytes  = kDefaultStaticIndexBytes,
                        uint32_t slotCapacity      = kDefaultSlotCapacity);

        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept;

        // -----------------------------------------------------------------
        // Slot management
        // -----------------------------------------------------------------
        // Lifecycle systems call AllocateSlot() when an entity enters the
        // GPU scene.  The returned index is stored on GpuSceneSlot::CullingSlotIndex
        // and is also used to address the CullingSystem / TransformSyncSystem SSBOs.
        // FreeSlot() must be called from the on_destroy hook BEFORE releasing
        // any geometry buffers stored in the same GpuSceneSlot.
        [[nodiscard]] uint32_t AllocateSlot();
        void                   FreeSlot(uint32_t slot);

        // -----------------------------------------------------------------
        // Static geometry suballocation (bump pointer, device-local)
        // -----------------------------------------------------------------
        // Returns the byte offset (uint64_t — matches BufferDesc::SizeBytes)
        // within the static buffer where data was written.
        // Caller stores {offset, elementCount, stride} in GpuSceneSlot::Buffers.
        // Returns UINT64_MAX on overflow.
        //
        // Static buffers are write-once.  There is intentionally no update
        // path: geometry that changes after load (skinning, morph targets,
        // simulation) belongs in a dynamic buffer, not a static one.
        [[nodiscard]] uint64_t UploadStaticVertices(std::span<const std::byte> data);
        [[nodiscard]] uint64_t UploadStaticIndices (std::span<const std::byte> data);

        // -----------------------------------------------------------------
        // Dynamic (host-visible, per-entity) geometry buffers
        // -----------------------------------------------------------------
        // AllocateDynamicBuffer creates a host-visible Storage buffer.
        // GpuScene owns the internal lease; the caller MUST call
        // FreeDynamicBuffer(handle) to release it.
        //
        // UpdateDynamicBuffer       — full overwrite from offset 0.
        // UpdateDynamicBufferRange  — partial overwrite at a given byte offset.
        //   Use the range variant when only one attribute stream changed
        //   (e.g. colors updated but positions unchanged) to avoid
        //   re-uploading the whole buffer.
        [[nodiscard]] RHI::BufferHandle AllocateDynamicBuffer(uint32_t    sizeBytes,
                                                               const char* debugName = nullptr);
        void UpdateDynamicBuffer     (RHI::BufferHandle          handle,
                                      std::span<const std::byte> data);
        void UpdateDynamicBufferRange(RHI::BufferHandle          handle,
                                      uint64_t                   byteOffset,
                                      std::span<const std::byte> data);
        void FreeDynamicBuffer(RHI::BufferHandle handle);

        // -----------------------------------------------------------------
        // Accessors
        // -----------------------------------------------------------------
        [[nodiscard]] RHI::BufferHandle StaticVertexBuffer() const noexcept;
        [[nodiscard]] RHI::BufferHandle StaticIndexBuffer()  const noexcept;

        [[nodiscard]] uint32_t AllocatedSlotCount()    const noexcept;
        [[nodiscard]] uint64_t StaticVertexBytesUsed() const noexcept;
        [[nodiscard]] uint64_t StaticIndexBytesUsed()  const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
