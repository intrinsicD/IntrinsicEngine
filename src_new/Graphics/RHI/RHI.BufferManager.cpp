module;

#include <atomic>
#include <cassert>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

module Extrinsic.RHI.BufferManager;

import Extrinsic.Core.Error;
import Extrinsic.Core.HandleLease;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.BufferView;
import Extrinsic.RHI.Device;

// ============================================================
// BufferManager — implementation
// ============================================================
// Slot pool with a free list.  Each slot stores:
//   - the creation descriptor (for View() / GetDesc())
//   - an atomic refcount
//   - a generation counter (incremented on free, so stale
//     handles fail IsValid() checks gracefully)
//
// Complexity:
//   Create   — amortised O(1)  (free list pop or push_back)
//   Retain   — O(1) lock-free
//   Release  — O(1) lock-free; O(1) locked on zero (free-list push)
//   View     — O(1) lock-free
//   GetDesc  — O(1) lock-free
// ============================================================

namespace Extrinsic::RHI
{
    // -----------------------------------------------------------------
    // Internal slot
    // -----------------------------------------------------------------
    struct BufferSlot
    {
        BufferDesc                Desc{};
        BufferHandle              DeviceHandle{};  // opaque cookie for IDevice::DestroyBuffer
        std::atomic<std::uint32_t> RefCount{0};
        std::uint32_t              Generation = 0;
        bool                       Live       = false;

        // Slots are non-movable because atomics cannot be moved after
        // construction — the vector is reserved once at startup.
        BufferSlot() = default;
        BufferSlot(const BufferSlot&) = delete;
        BufferSlot& operator=(const BufferSlot&) = delete;
    };

    // -----------------------------------------------------------------
    // Impl
    // -----------------------------------------------------------------
    struct BufferManager::Impl
    {
        IDevice&                      Device;
        std::mutex                    Mutex;       // guards Slots + FreeList
        // std::deque provides stable addresses on push_back — atomics inside
        // BufferSlot cannot be moved, so vector reallocation is forbidden.
        std::deque<BufferSlot>        Slots;
        std::vector<std::uint32_t>    FreeList;

        explicit Impl(IDevice& device) : Device(device) {}

        // Returns the slot if the handle is live and generation matches.
        [[nodiscard]] BufferSlot* Resolve(BufferHandle handle) noexcept
        {
            if (!handle.IsValid()) return nullptr;
            if (handle.Index >= static_cast<std::uint32_t>(Slots.size())) return nullptr;
            BufferSlot& slot = Slots[handle.Index];
            if (!slot.Live || slot.Generation != handle.Generation) return nullptr;
            return &slot;
        }

        [[nodiscard]] const BufferSlot* Resolve(BufferHandle handle) const noexcept
        {
            if (!handle.IsValid()) return nullptr;
            if (handle.Index >= static_cast<std::uint32_t>(Slots.size())) return nullptr;
            const BufferSlot& slot = Slots[handle.Index];
            if (!slot.Live || slot.Generation != handle.Generation) return nullptr;
            return &slot;
        }
    };

    // -----------------------------------------------------------------
    // BufferManager
    // -----------------------------------------------------------------
    BufferManager::BufferManager(IDevice& device)
        : m_Impl(std::make_unique<Impl>(device))
    {}

    BufferManager::~BufferManager() = default;

    // -----------------------------------------------------------------
    Core::Expected<BufferManager::BufferLease> BufferManager::Create(const BufferDesc& desc)
    {
        // Ask the device to allocate the GPU resource.
        BufferHandle deviceHandle = m_Impl->Device.CreateBuffer(desc);
        if (!deviceHandle.IsValid())
            return Core::Err<BufferLease>(Core::ErrorCode::OutOfDeviceMemory);

        std::uint32_t index;
        std::uint32_t generation;

        {
            std::lock_guard lock{m_Impl->Mutex};

            if (!m_Impl->FreeList.empty())
            {
                index = m_Impl->FreeList.back();
                m_Impl->FreeList.pop_back();
                generation = m_Impl->Slots[index].Generation; // already bumped on last free
            }
            else
            {
                // deque::push_back does not invalidate existing element addresses.
                index      = static_cast<std::uint32_t>(m_Impl->Slots.size());
                generation = 0;
                m_Impl->Slots.emplace_back();
            }

            BufferSlot& slot   = m_Impl->Slots[index];
            slot.Desc          = desc;
            slot.DeviceHandle  = deviceHandle; // stored for DestroyBuffer
            slot.Generation    = generation;
            slot.Live          = true;
            slot.RefCount.store(1, std::memory_order_relaxed);
        }

        // Issue our own pool handle — callers never see the device-internal handle.
        BufferHandle poolHandle{index, generation};
        return BufferLease::Adopt(*this, poolHandle);
    }

    // -----------------------------------------------------------------
    void BufferManager::Retain(BufferHandle handle)
    {
        // Fast path: no lock needed, only the slot's atomic.
        BufferSlot* slot = m_Impl->Resolve(handle);
        assert(slot && "Retain called on invalid or released handle");
        if (slot)
            slot->RefCount.fetch_add(1, std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------
    void BufferManager::Release(BufferHandle handle)
    {
        BufferSlot* slot = m_Impl->Resolve(handle);
        assert(slot && "Release called on invalid or already-freed handle");
        if (!slot) return;

        const std::uint32_t prev =
            slot->RefCount.fetch_sub(1, std::memory_order_acq_rel);

        assert(prev > 0 && "Refcount underflow");

        if (prev == 1)
        {
            // Last reference — destroy the GPU resource, then recycle the slot.
            m_Impl->Device.DestroyBuffer(slot->DeviceHandle);

            std::lock_guard lock{m_Impl->Mutex};
            slot->Live = false;
            slot->Generation++; // invalidates any copies of the old handle
            m_Impl->FreeList.push_back(handle.Index);
        }
    }

    // -----------------------------------------------------------------
    BufferManager::BufferLease BufferManager::AcquireLease(BufferHandle handle)
    {
        // Lease::Share() delegates here so the manager controls atomicity.
        return BufferLease::RetainNew(*this, handle);
    }

    // -----------------------------------------------------------------
    BufferView BufferManager::View(BufferHandle handle,
                                   std::uint64_t  offset,
                                   std::uint64_t  size) const noexcept
    {
        const BufferSlot* slot = m_Impl->Resolve(handle);
        if (!slot) return {};

        const std::uint64_t capacity = slot->Desc.SizeBytes;

        if (offset >= capacity) return {};

        const std::uint64_t clampedSize =
            (size == WholeBuffer) ? (capacity - offset)
                                  : std::min(size, capacity - offset);

        return BufferView{.Buffer = handle, .Offset = offset, .Size = clampedSize};
    }

    // -----------------------------------------------------------------
    const BufferDesc* BufferManager::GetDesc(BufferHandle handle) const noexcept
    {
        const BufferSlot* slot = m_Impl->Resolve(handle);
        return slot ? &slot->Desc : nullptr;
    }

} // namespace Extrinsic::RHI








