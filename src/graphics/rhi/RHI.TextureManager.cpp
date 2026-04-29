module;

#include <atomic>
#include <cassert>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

module Extrinsic.RHI.TextureManager;

import Extrinsic.Core.Error;
import Extrinsic.Core.HandleLease;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Device;

// ============================================================
// TextureManager — implementation
// ============================================================
// Same slot-pool / free-list / deque layout as BufferManager.
// Two extra fields per slot:
//   DeviceHandle  — the IDevice-issued cookie for DestroyTexture
//   BindlessSlot  — the IBindlessHeap slot (kInvalidBindlessIndex if none)
//
// Complexity: identical to BufferManager (all O(1)).
// ============================================================

namespace Extrinsic::RHI
{
    // -----------------------------------------------------------------
    // Internal slot
    // -----------------------------------------------------------------
    struct TextureSlot
    {
        TextureDesc               Desc{};
        TextureHandle             DeviceHandle{};             // IDevice cookie
        SamplerHandle             Sampler{};                  // stored for Reupload; not owned
        BindlessIndex             BindlessSlot = kInvalidBindlessIndex;
        std::atomic<std::uint32_t> RefCount{0};
        std::uint32_t              Generation  = 0;
        bool                       Live        = false;

        // Non-movable — atomic member + deque gives stable addresses.
        TextureSlot() = default;
        TextureSlot(const TextureSlot&)            = delete;
        TextureSlot& operator=(const TextureSlot&) = delete;
    };

    // -----------------------------------------------------------------
    // Impl
    // -----------------------------------------------------------------
    struct TextureManager::Impl
    {
        IDevice&                   Device;
        IBindlessHeap&             Bindless;
        std::mutex                 Mutex;       // guards Slots + FreeList
        std::deque<TextureSlot>    Slots;       // stable addresses on growth
        std::vector<std::uint32_t> FreeList;

        // Outstanding TextureLease instances. See BufferManager.cpp for the
        // F2 rationale — the destructor asserts this is zero to catch
        // lease-outlives-manager use-after-free in Debug.
        std::atomic<std::uint32_t> LiveLeaseCount{0};

        Impl(IDevice& device, IBindlessHeap& bindless)
            : Device(device), Bindless(bindless) {}

        [[nodiscard]] TextureSlot* Resolve(TextureHandle handle) noexcept
        {
            if (!handle.IsValid()) return nullptr;
            if (handle.Index >= static_cast<std::uint32_t>(Slots.size())) return nullptr;
            TextureSlot& slot = Slots[handle.Index];
            if (!slot.Live || slot.Generation != handle.Generation) return nullptr;
            return &slot;
        }

        [[nodiscard]] const TextureSlot* Resolve(TextureHandle handle) const noexcept
        {
            if (!handle.IsValid()) return nullptr;
            if (handle.Index >= static_cast<std::uint32_t>(Slots.size())) return nullptr;
            const TextureSlot& slot = Slots[handle.Index];
            if (!slot.Live || slot.Generation != handle.Generation) return nullptr;
            return &slot;
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

        // Register into the bindless heap before we publish the pool handle
        // so that GetBindlessIndex() is always valid once the lease is returned.
        BindlessIndex bindlessSlot = kInvalidBindlessIndex;
        if (sampler.IsValid())
            bindlessSlot = m_Impl->Bindless.AllocateTextureSlot(deviceHandle, sampler);

        std::uint32_t index;
        std::uint32_t generation;

        {
            std::lock_guard lock{m_Impl->Mutex};

            if (!m_Impl->FreeList.empty())
            {
                index      = m_Impl->FreeList.back();
                m_Impl->FreeList.pop_back();
                generation = m_Impl->Slots[index].Generation;
            }
            else
            {
                index      = static_cast<std::uint32_t>(m_Impl->Slots.size());
                generation = 0;
                m_Impl->Slots.emplace_back();
            }

            TextureSlot& slot  = m_Impl->Slots[index];
            slot.Desc          = desc;
            slot.DeviceHandle  = deviceHandle;
            slot.Sampler       = sampler;
            slot.BindlessSlot  = bindlessSlot;
            slot.Generation    = generation;
            slot.Live          = true;
            slot.RefCount.store(1, std::memory_order_relaxed);
        }

        TextureHandle poolHandle{index, generation};
        m_Impl->LiveLeaseCount.fetch_add(1, std::memory_order_relaxed);
        return TextureLease::Adopt(*this, poolHandle);
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
        TextureSlot* slot = m_Impl->Resolve(handle);
        assert(slot && "Release called on invalid or already-freed handle");
        if (!slot) return;

        const std::uint32_t prev =
            slot->RefCount.fetch_sub(1, std::memory_order_acq_rel);

        assert(prev > 0 && "Refcount underflow");

        // Decrement manager-wide live-lease counter, paired with the
        // destructor's acquire-load.
        m_Impl->LiveLeaseCount.fetch_sub(1, std::memory_order_acq_rel);

        if (prev == 1)
        {
            // Free the bindless slot first so the GPU heap reverts to the
            // default/error descriptor before the texture VkImage is destroyed.
            if (slot->BindlessSlot != kInvalidBindlessIndex)
                m_Impl->Bindless.FreeSlot(slot->BindlessSlot);

            // Destroy the GPU resource.  Sampler is NOT destroyed — it is
            // externally owned and may be shared across many textures.
            m_Impl->Device.DestroyTexture(slot->DeviceHandle);

            std::lock_guard lock{m_Impl->Mutex};
            slot->Live = false;
            slot->Generation++;
            m_Impl->FreeList.push_back(handle.Index);
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
        slot->DeviceHandle = newDeviceHandle;
        slot->Sampler      = newSampler;
    }

} // namespace Extrinsic::RHI

