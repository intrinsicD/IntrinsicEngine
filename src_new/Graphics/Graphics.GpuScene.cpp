module;

#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

module Extrinsic.Graphics.GpuScene;

import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.BufferManager;
import Extrinsic.Core.StrongHandle;

// ============================================================
// GpuScene — implementation notes
//
// Slot pool
//   A plain free-list (deque-style vector) of recycled uint32_t indices.
//   No generation counter here — GpuSceneSlot::CullingSlotIndex carries
//   the raw index only; the lifecycle system is responsible for zeroing
//   the component before or after FreeSlot().
//
// Static geometry suballocator
//   Bump pointer (linear allocator) into two device-local buffers.
//   Resetting the bump pointer is NOT supported — the static geometry
//   model is load-once, persist-for-scene.  A scene reload goes through
//   Shutdown() → Initialize().
//
// Dynamic geometry buffers
//   Each call to AllocateDynamicBuffer() drives BufferManager::Create()
//   (HostVisible + Storage).  The resulting lease is stored in
//   m_DynamicLeases keyed by the raw BufferHandle.  FreeDynamicBuffer()
//   erases the lease, decrementing the refcount to zero and letting
//   BufferManager destroy the underlying VkBuffer.
// ============================================================

namespace Extrinsic::Graphics
{
    // -------------------------------------------------------
    // Impl
    // -------------------------------------------------------
    struct GpuScene::Impl
    {
        RHI::IDevice*       Device     = nullptr;
        RHI::BufferManager* BufferMgr  = nullptr;
        bool                Initialized = false;

        // --- Static geometry buffers ---
        RHI::BufferManager::BufferLease StaticVBLease;
        RHI::BufferManager::BufferLease StaticIBLease;
        uint64_t StaticVBCapacity = 0;   // bytes allocated
        uint64_t StaticIBCapacity = 0;
        uint64_t StaticVBOffset   = 0;   // next free byte offset (bump ptr)
        uint64_t StaticIBOffset   = 0;

        // --- Dynamic geometry buffer lease table ---
        // Key: raw handle value packed as uint64_t to avoid including
        // StrongHandleHash in the header (implementation detail only).
        using DynamicLeaseMap = std::unordered_map<
            RHI::BufferHandle,
            RHI::BufferManager::BufferLease,
            Core::StrongHandleHash<RHI::BufferTag>>;
        DynamicLeaseMap DynamicLeases;

        // --- Slot pool ---
        uint32_t SlotCapacity = 0;
        uint32_t LiveCount    = 0;
        std::vector<uint32_t> FreeList;  // recycled slot indices
        uint32_t NextFreshSlot = 0;      // highest never-issued slot index

        // -------------------------------------------------------
        bool AllocateStaticBuffers(uint64_t vbBytes, uint64_t ibBytes)
        {
            {
                auto res = BufferMgr->Create({
                    .SizeBytes   = vbBytes,
                    .Usage       = RHI::BufferUsage::Storage
                                 | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName   = "GpuScene.StaticVertexBuffer",
                });
                if (!res.has_value()) return false;
                StaticVBLease    = std::move(*res);
                StaticVBCapacity = vbBytes;
            }
            {
                auto res = BufferMgr->Create({
                    .SizeBytes   = ibBytes,
                    .Usage       = RHI::BufferUsage::Index
                                 | RHI::BufferUsage::Storage
                                 | RHI::BufferUsage::TransferDst,
                    .HostVisible = false,
                    .DebugName   = "GpuScene.StaticIndexBuffer",
                });
                if (!res.has_value()) return false;
                StaticIBLease    = std::move(*res);
                StaticIBCapacity = ibBytes;
            }
            return true;
        }
    };

    // -------------------------------------------------------
    // Construction / destruction
    // -------------------------------------------------------
    GpuScene::GpuScene()
        : m_Impl(std::make_unique<Impl>())
    {}

    GpuScene::~GpuScene()
    {
        // Belt-and-suspenders: Shutdown() should have been called explicitly,
        // but we clean up if the caller forgets.
        if (m_Impl && m_Impl->Initialized)
            Shutdown();
    }

    // -------------------------------------------------------
    // Initialize / Shutdown
    // -------------------------------------------------------
    bool GpuScene::Initialize(RHI::IDevice&       device,
                               RHI::BufferManager& bufferMgr,
                               uint64_t            staticVertexBytes,
                               uint64_t            staticIndexBytes,
                               uint32_t            slotCapacity)
    {
        assert(!m_Impl->Initialized && "GpuScene::Initialize called twice");

        m_Impl->Device    = &device;
        m_Impl->BufferMgr = &bufferMgr;

        if (!device.IsOperational())
        {
            // Null backend — skip GPU allocation but mark as initialised
            // so the slot pool is available for headless tests.
            m_Impl->SlotCapacity = slotCapacity;
            m_Impl->Initialized  = true;
            return true;
        }

        if (!m_Impl->AllocateStaticBuffers(staticVertexBytes, staticIndexBytes))
            return false;

        m_Impl->SlotCapacity = slotCapacity;
        m_Impl->FreeList.reserve(64);
        m_Impl->Initialized = true;
        return true;
    }

    void GpuScene::Shutdown()
    {
        if (!m_Impl->Initialized) return;

        // Release all dynamic buffer leases first.
        m_Impl->DynamicLeases.clear();

        // Drop static buffer leases (BufferManager ref-count → 0 → destroy).
        m_Impl->StaticVBLease = {};
        m_Impl->StaticIBLease = {};

        m_Impl->StaticVBOffset   = 0;
        m_Impl->StaticVBCapacity = 0;
        m_Impl->StaticIBOffset   = 0;
        m_Impl->StaticIBCapacity = 0;

        m_Impl->FreeList.clear();
        m_Impl->NextFreshSlot = 0;
        m_Impl->LiveCount     = 0;
        m_Impl->SlotCapacity  = 0;

        m_Impl->Device      = nullptr;
        m_Impl->BufferMgr   = nullptr;
        m_Impl->Initialized = false;
    }

    bool GpuScene::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }

    // -------------------------------------------------------
    // Slot management
    // -------------------------------------------------------
    uint32_t GpuScene::AllocateSlot()
    {
        assert(m_Impl->Initialized && "AllocateSlot called before Initialize()");

        uint32_t slot;
        if (!m_Impl->FreeList.empty())
        {
            slot = m_Impl->FreeList.back();
            m_Impl->FreeList.pop_back();
        }
        else
        {
            assert(m_Impl->NextFreshSlot < m_Impl->SlotCapacity
                   && "GpuScene slot capacity exceeded — increase slotCapacity in Initialize()");
            slot = m_Impl->NextFreshSlot++;
        }

        ++m_Impl->LiveCount;
        return slot;
    }

    void GpuScene::FreeSlot(uint32_t slot)
    {
        assert(m_Impl->Initialized && "FreeSlot called before Initialize()");
        assert(slot < m_Impl->NextFreshSlot && "FreeSlot: slot index was never issued");
        assert(m_Impl->LiveCount > 0u);

        m_Impl->FreeList.push_back(slot);
        --m_Impl->LiveCount;
    }

    // -------------------------------------------------------
    // Static geometry suballocation
    // -------------------------------------------------------
    uint64_t GpuScene::UploadStaticVertices(std::span<const std::byte> data)
    {
        assert(m_Impl->Initialized && "UploadStaticVertices called before Initialize()");

        const uint64_t size = data.size_bytes();
        if (size == 0) return 0u;

        if (m_Impl->StaticVBOffset + size > m_Impl->StaticVBCapacity)
        {
            assert(false && "GpuScene: static vertex buffer overflow — increase staticVertexBytes");
            return UINT64_MAX;
        }

        const uint64_t byteOffset = m_Impl->StaticVBOffset;

        if (m_Impl->Device->IsOperational())
        {
            m_Impl->Device->WriteBuffer(
                m_Impl->StaticVBLease.GetHandle(),
                data.data(), size, byteOffset);
        }

        m_Impl->StaticVBOffset += size;
        return byteOffset;
    }

    uint64_t GpuScene::UploadStaticIndices(std::span<const std::byte> data)
    {
        assert(m_Impl->Initialized && "UploadStaticIndices called before Initialize()");

        const uint64_t size = data.size_bytes();
        if (size == 0) return 0u;

        if (m_Impl->StaticIBOffset + size > m_Impl->StaticIBCapacity)
        {
            assert(false && "GpuScene: static index buffer overflow — increase staticIndexBytes");
            return UINT64_MAX;
        }

        const uint64_t byteOffset = m_Impl->StaticIBOffset;

        if (m_Impl->Device->IsOperational())
        {
            m_Impl->Device->WriteBuffer(
                m_Impl->StaticIBLease.GetHandle(),
                data.data(), size, byteOffset);
        }

        m_Impl->StaticIBOffset += size;
        return byteOffset;
    }

    // -------------------------------------------------------
    // Dynamic geometry buffers
    // -------------------------------------------------------
    RHI::BufferHandle GpuScene::AllocateDynamicBuffer(uint32_t    sizeBytes,
                                                       const char* debugName)
    {
        assert(m_Impl->Initialized && "AllocateDynamicBuffer called before Initialize()");
        assert(sizeBytes > 0u);

        if (!m_Impl->Device->IsOperational())
            return {};

        auto res = m_Impl->BufferMgr->Create({
            .SizeBytes   = sizeBytes,
            .Usage       = RHI::BufferUsage::Storage,
            .HostVisible = true,
            .DebugName   = debugName ? debugName : "GpuScene.DynamicBuffer",
        });
        if (!res.has_value()) return {};

        const RHI::BufferHandle handle = res->GetHandle();
        m_Impl->DynamicLeases.emplace(handle, std::move(*res));
        return handle;
    }

    void GpuScene::UpdateDynamicBuffer(RHI::BufferHandle          handle,
                                        std::span<const std::byte> data)
    {
        assert(m_Impl->Initialized && "UpdateDynamicBuffer called before Initialize()");
        if (!handle.IsValid() || data.empty()) return;
        assert(m_Impl->DynamicLeases.count(handle) &&
               "UpdateDynamicBuffer: handle not owned by this GpuScene");

        if (m_Impl->Device->IsOperational())
            m_Impl->Device->WriteBuffer(handle, data.data(), data.size_bytes(), 0);
    }

    void GpuScene::UpdateDynamicBufferRange(RHI::BufferHandle          handle,
                                             uint64_t                   byteOffset,
                                             std::span<const std::byte> data)
    {
        assert(m_Impl->Initialized && "UpdateDynamicBufferRange called before Initialize()");
        if (!handle.IsValid() || data.empty()) return;
        assert(m_Impl->DynamicLeases.count(handle) &&
               "UpdateDynamicBufferRange: handle not owned by this GpuScene");

        if (m_Impl->Device->IsOperational())
            m_Impl->Device->WriteBuffer(handle, data.data(), data.size_bytes(), byteOffset);
    }

    void GpuScene::FreeDynamicBuffer(RHI::BufferHandle handle)
    {
        assert(m_Impl->Initialized && "FreeDynamicBuffer called before Initialize()");
        if (!handle.IsValid()) return;

        // Erasing the lease drops the refcount to zero → BufferManager::Release()
        // → IDevice::DestroyBuffer().  Must be called on the render thread.
        [[maybe_unused]] const auto erased = m_Impl->DynamicLeases.erase(handle);
        assert(erased && "FreeDynamicBuffer: handle not owned by this GpuScene");
    }

    // -------------------------------------------------------
    // Accessors
    // -------------------------------------------------------
    RHI::BufferHandle GpuScene::StaticVertexBuffer() const noexcept
    {
        return m_Impl->StaticVBLease.GetHandle();
    }

    RHI::BufferHandle GpuScene::StaticIndexBuffer() const noexcept
    {
        return m_Impl->StaticIBLease.GetHandle();
    }

    uint32_t GpuScene::AllocatedSlotCount() const noexcept
    {
        return m_Impl->LiveCount;
    }

    uint64_t GpuScene::StaticVertexBytesUsed() const noexcept
    {
        return m_Impl->StaticVBOffset;
    }

    uint64_t GpuScene::StaticIndexBytesUsed() const noexcept
    {
        return m_Impl->StaticIBOffset;
    }

} // namespace Extrinsic::Graphics

