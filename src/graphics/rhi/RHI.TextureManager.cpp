module;

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

module Extrinsic.RHI.TextureManager;

import Extrinsic.Core.Error;
import Extrinsic.Core.HandleLease;
import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Device;

// ============================================================
// TextureManager — implementation
// ============================================================
// Storage is a `std::unordered_map<TextureHandle, TextureSlot>` keyed by the
// IDevice-issued texture handle. The lease published to callers IS the
// IDevice handle (deviceHandle), so a caller may pass `lease.GetHandle()`
// directly into RHI APIs such as
// `IDevice::GetTransferQueue().UploadTexture(handle, ...)` without any
// translation step. The earlier slot-pool-and-free-list design returned a
// manager-local poolHandle that the Vulkan backend could not resolve through
// its own `m_Images.GetIfValid(...)` lookup, silently rejecting every
// texture upload. See GRAPHICS-076 Slice D diagnosis for the trace.
//
// Complexity remains amortised O(1) for Create/Retain/Release because
// `std::unordered_map` insert/find/erase are average O(1). The map's node
// allocation also gives us stable addresses for the per-slot atomic refcount
// without needing the previous deque-based stable-address trick.
// ============================================================

namespace Extrinsic::RHI
{
    // -----------------------------------------------------------------
    // Internal slot
    // -----------------------------------------------------------------
    struct TextureSlot
    {
        TextureDesc                Desc{};
        SamplerHandle              Sampler{};                  // stored for Reupload; not owned
        BindlessIndex              BindlessSlot = kInvalidBindlessIndex;
        // Updated by Reupload to point at a new IDevice texture (e.g. mip
        // streaming); distinct from the slot key, which stays at the
        // originally-published deviceHandle so existing leases keep working.
        TextureHandle              CurrentDeviceHandle{};
        std::atomic<std::uint32_t> RefCount{0};

        // Atomic member; node-based map storage gives stable addresses on
        // insertion/erasure so we do not need movability.
        TextureSlot() = default;
        TextureSlot(const TextureSlot&)            = delete;
        TextureSlot& operator=(const TextureSlot&) = delete;
        TextureSlot(TextureSlot&&)                 = delete;
        TextureSlot& operator=(TextureSlot&&)      = delete;
    };

    // -----------------------------------------------------------------
    // Impl
    // -----------------------------------------------------------------
    struct TextureManager::Impl
    {
        IDevice&       Device;
        IBindlessHeap& Bindless;
        std::mutex     Mutex;       // guards Slots
        std::unordered_map<TextureHandle, TextureSlot,
                           Core::StrongHandleHash<TextureTag>> Slots;

        // Outstanding TextureLease instances. See BufferManager.cpp for the
        // F2 rationale — the destructor asserts this is zero to catch
        // lease-outlives-manager use-after-free in Debug.
        std::atomic<std::uint32_t> LiveLeaseCount{0};

        Impl(IDevice& device, IBindlessHeap& bindless)
            : Device(device), Bindless(bindless) {}

        // Returns the slot if the handle is currently registered with this
        // manager. Generation mismatches naturally produce a miss because the
        // map is keyed by the full (Index, Generation) handle published by
        // the device.
        [[nodiscard]] TextureSlot* Resolve(TextureHandle handle) noexcept
        {
            if (!handle.IsValid()) return nullptr;
            const auto it = Slots.find(handle);
            return it == Slots.end() ? nullptr : &it->second;
        }

        [[nodiscard]] const TextureSlot* Resolve(TextureHandle handle) const noexcept
        {
            if (!handle.IsValid()) return nullptr;
            const auto it = Slots.find(handle);
            return it == Slots.end() ? nullptr : &it->second;
        }
    };

    // -----------------------------------------------------------------
    // TextureManager
    // -----------------------------------------------------------------
    TextureManager::TextureManager(IDevice& device, IBindlessHeap& bindlessHeap)
        : m_Impl(std::make_unique<Impl>(device, bindlessHeap))
    {}

    TextureManager::~TextureManager()
    {
        // Every TextureLease issued by this manager must be destroyed before
        // the manager itself. A lingering lease will call Release() on a
        // freed m_Impl (UAF). See BufferManager::~BufferManager for the same
        // pattern and the class-level Lifetime contract paragraph.
        const auto alive = m_Impl->LiveLeaseCount.load(std::memory_order_acquire);
        assert(alive == 0 &&
               "TextureManager destroyed while leases are still alive — drop "
               "all TextureLease instances before destroying this manager.");
        (void)alive;
    }

    // -----------------------------------------------------------------
    Core::Expected<TextureManager::TextureLease> TextureManager::Create(
        const TextureDesc& desc,
        SamplerHandle       sampler)
    {
        // F14: short-circuit on stub backends.
        if (!m_Impl->Device.IsOperational())
            return Core::Err<TextureLease>(Core::ErrorCode::DeviceNotOperational);

        TextureHandle deviceHandle = m_Impl->Device.CreateTexture(desc);
        if (!deviceHandle.IsValid())
            return Core::Err<TextureLease>(Core::ErrorCode::OutOfDeviceMemory);

        // Register into the bindless heap before we publish the lease so that
        // GetBindlessIndex() is always valid once the lease is returned.
        BindlessIndex bindlessSlot = kInvalidBindlessIndex;
        if (sampler.IsValid())
            bindlessSlot = m_Impl->Bindless.AllocateTextureSlot(deviceHandle, sampler);

        {
            std::lock_guard lock{m_Impl->Mutex};
            // try_emplace default-constructs the TextureSlot (which has a
            // non-movable atomic) in-place inside the map node. Subsequent
            // map operations (insert/erase of other keys) do not invalidate
            // pointers to this node's value per std::unordered_map's
            // reference-stability guarantee.
            const auto [it, inserted] = m_Impl->Slots.try_emplace(deviceHandle);
            assert(inserted &&
                   "TextureManager::Create — IDevice issued a duplicate live "
                   "TextureHandle. The device-side pool must increment the "
                   "handle generation on reuse; see Core::ResourcePool::Add.");
            (void)inserted;
            TextureSlot& slot         = it->second;
            slot.Desc                 = desc;
            slot.Sampler              = sampler;
            slot.BindlessSlot         = bindlessSlot;
            slot.CurrentDeviceHandle  = deviceHandle;
            slot.RefCount.store(1, std::memory_order_relaxed);
        }

        m_Impl->LiveLeaseCount.fetch_add(1, std::memory_order_relaxed);
        return TextureLease::Adopt(*this, deviceHandle);
    }

    // -----------------------------------------------------------------
    void TextureManager::Retain(TextureHandle handle)
    {
        TextureSlot* slot = m_Impl->Resolve(handle);
        assert(slot && "Retain called on invalid or released handle");
        if (slot)
        {
            slot->RefCount.fetch_add(1, std::memory_order_relaxed);
            m_Impl->LiveLeaseCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // -----------------------------------------------------------------
    void TextureManager::Release(TextureHandle handle)
    {
        // Snapshot the bindless slot + current device handle under the map
        // lookup, then drop them in the same shape as the previous slot-pool
        // implementation. We cannot hold a raw `TextureSlot*` across the
        // final `Slots.erase(handle)` below because erase invalidates the
        // node's address.
        std::uint32_t prev = 0;
        BindlessIndex bindlessSlot = kInvalidBindlessIndex;
        TextureHandle deviceHandleToDestroy{};
        bool          shouldDestroy = false;
        {
            std::lock_guard lock{m_Impl->Mutex};
            const auto it = m_Impl->Slots.find(handle);
            assert(it != m_Impl->Slots.end() &&
                   "Release called on invalid or already-freed handle");
            if (it == m_Impl->Slots.end()) return;

            TextureSlot& slot = it->second;
            prev = slot.RefCount.fetch_sub(1, std::memory_order_acq_rel);
            assert(prev > 0 && "Refcount underflow");
            m_Impl->LiveLeaseCount.fetch_sub(1, std::memory_order_acq_rel);
            if (prev == 1)
            {
                bindlessSlot          = slot.BindlessSlot;
                deviceHandleToDestroy = slot.CurrentDeviceHandle;
                shouldDestroy         = true;
                m_Impl->Slots.erase(it);
            }
        }

        if (shouldDestroy)
        {
            // Free the bindless slot first so the GPU heap reverts to the
            // default/error descriptor before the texture VkImage is destroyed.
            if (bindlessSlot != kInvalidBindlessIndex)
                m_Impl->Bindless.FreeSlot(bindlessSlot);

            // Destroy the GPU resource.  Sampler is NOT destroyed — it is
            // externally owned and may be shared across many textures.
            m_Impl->Device.DestroyTexture(deviceHandleToDestroy);
        }
    }

    // -----------------------------------------------------------------
    TextureManager::TextureLease TextureManager::AcquireLease(TextureHandle handle)
    {
        return TextureLease::RetainNew(*this, handle);
    }

    // -----------------------------------------------------------------
    const TextureDesc* TextureManager::GetDesc(TextureHandle handle) const noexcept
    {
        const TextureSlot* slot = m_Impl->Resolve(handle);
        return slot ? &slot->Desc : nullptr;
    }

    // -----------------------------------------------------------------
    BindlessIndex TextureManager::GetBindlessIndex(TextureHandle handle) const noexcept
    {
        const TextureSlot* slot = m_Impl->Resolve(handle);
        return slot ? slot->BindlessSlot : kInvalidBindlessIndex;
    }

    // -----------------------------------------------------------------
    void TextureManager::Reupload(TextureHandle handle,
                                  TextureHandle newDeviceHandle,
                                  SamplerHandle newSampler)
    {
        TextureSlot* slot = m_Impl->Resolve(handle);
        if (!slot) return;
        if (slot->BindlessSlot == kInvalidBindlessIndex) return;

        // UpdateTextureSlot is thread-safe per the IBindlessHeap contract;
        // it queues the descriptor update and applies it on FlushPending().
        m_Impl->Bindless.UpdateTextureSlot(slot->BindlessSlot, newDeviceHandle, newSampler);
        slot->CurrentDeviceHandle = newDeviceHandle;
        slot->Sampler             = newSampler;
    }

} // namespace Extrinsic::RHI

