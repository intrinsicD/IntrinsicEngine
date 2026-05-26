module;

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

module Extrinsic.RHI.BufferManager;

import Extrinsic.Core.Error;
import Extrinsic.Core.HandleLease;
import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.BufferView;
import Extrinsic.RHI.Device;

// ============================================================
// BufferManager — implementation
// ============================================================
// Storage is a `std::unordered_map<BufferHandle, BufferSlot>` keyed by the
// IDevice-issued buffer handle. The lease published to callers IS the IDevice
// handle (deviceHandle), so a caller may pass `lease.GetHandle()` directly
// into RHI APIs such as
// `IDevice::GetTransferQueue().UploadBuffer(handle, ...)` without any
// translation step. The earlier slot-pool-and-free-list design returned a
// manager-local poolHandle that the Vulkan backend could not resolve through
// its own `m_Buffers.GetIfValid(...)` lookup, silently rejecting every
// buffer upload. See GRAPHICS-076 Slice D diagnosis for the trace.
//
// Complexity:
//   Create   — amortised O(1)  (unordered_map insert)
//   Retain   — O(1) lock-free atomic on the slot, plus an O(1) average
//              `find` on the map under the mutex
//   Release  — O(1) average; on zero, an O(1) average erase under the mutex
//   View     — O(1) average
//   GetDesc  — O(1) average
// ============================================================

namespace Extrinsic::RHI
{
    // -----------------------------------------------------------------
    // Internal slot
    // -----------------------------------------------------------------
    struct BufferSlot
    {
        BufferDesc                Desc{};
        std::atomic<std::uint32_t> RefCount{0};

        // Atomic member; node-based map storage gives stable addresses on
        // insertion/erasure so we do not need movability.
        BufferSlot() = default;
        BufferSlot(const BufferSlot&)            = delete;
        BufferSlot& operator=(const BufferSlot&) = delete;
        BufferSlot(BufferSlot&&)                 = delete;
        BufferSlot& operator=(BufferSlot&&)      = delete;
    };

    // -----------------------------------------------------------------
    // Impl
    // -----------------------------------------------------------------
    struct BufferManager::Impl
    {
        IDevice&   Device;
        std::mutex Mutex;       // guards Slots
        std::unordered_map<BufferHandle, BufferSlot,
                           Core::StrongHandleHash<BufferTag>> Slots;

        // Count of live BufferLease instances outstanding against this manager.
        // Incremented on every Create (primary lease) and every Retain
        // (additional lease via Share / AcquireLease). Decremented on every
        // Release. The destructor asserts this is zero to catch the
        // lease-outlives-manager UAF in Debug (see F2 in the code review).
        std::atomic<std::uint32_t>    LiveLeaseCount{0};

        explicit Impl(IDevice& device) : Device(device) {}

        // Returns the slot if the handle is currently registered with this
        // manager. Generation mismatches naturally produce a miss because the
        // map is keyed by the full (Index, Generation) handle published by
        // the device.
        [[nodiscard]] BufferSlot* Resolve(BufferHandle handle) noexcept
        {
            if (!handle.IsValid()) return nullptr;
            const auto it = Slots.find(handle);
            return it == Slots.end() ? nullptr : &it->second;
        }

        [[nodiscard]] const BufferSlot* Resolve(BufferHandle handle) const noexcept
        {
            if (!handle.IsValid()) return nullptr;
            const auto it = Slots.find(handle);
            return it == Slots.end() ? nullptr : &it->second;
        }
    };

    // -----------------------------------------------------------------
    // BufferManager
    // -----------------------------------------------------------------
    BufferManager::BufferManager(IDevice& device)
        : m_Impl(std::make_unique<Impl>(device))
    {}

    BufferManager::~BufferManager()
    {
        // Contract: every BufferLease issued by this manager must be destroyed
        // before this destructor runs. A lingering lease will call Release()
        // on a manager whose m_Impl has been freed — classic use-after-free.
        // The assertion catches this in Debug; in Release builds (NDEBUG), the
        // UAF will still occur but the class-level doc makes the contract
        // explicit.
        const auto alive = m_Impl->LiveLeaseCount.load(std::memory_order_acquire);
        assert(alive == 0 &&
               "BufferManager destroyed while leases are still alive — drop all "
               "BufferLease instances (and any BufferManager::BufferLease "
               "members of other objects) before destroying this manager.");
        (void)alive; // avoid unused-variable warning in NDEBUG builds
    }

    // -----------------------------------------------------------------
    Core::Expected<BufferManager::BufferLease> BufferManager::Create(const BufferDesc& desc)
    {
        // F14: short-circuit when the backend is a stub so callers get a
        // distinctive error rather than a lease wrapping a fake handle.
        if (!m_Impl->Device.IsOperational())
            return Core::Err<BufferLease>(Core::ErrorCode::DeviceNotOperational);

        // Ask the device to allocate the GPU resource.
        BufferHandle deviceHandle = m_Impl->Device.CreateBuffer(desc);
        if (!deviceHandle.IsValid())
            return Core::Err<BufferLease>(Core::ErrorCode::OutOfDeviceMemory);

        {
            std::lock_guard lock{m_Impl->Mutex};
            // try_emplace default-constructs the BufferSlot (which has a
            // non-movable atomic) in-place inside the map node. Subsequent
            // map operations (insert/erase of other keys) do not invalidate
            // pointers to this node's value per std::unordered_map's
            // reference-stability guarantee.
            const auto [it, inserted] = m_Impl->Slots.try_emplace(deviceHandle);
            assert(inserted &&
                   "BufferManager::Create — IDevice issued a duplicate live "
                   "BufferHandle. The device-side pool must increment the "
                   "handle generation on reuse; see Core::ResourcePool::Add.");
            (void)inserted;
            BufferSlot& slot = it->second;
            slot.Desc        = desc;
            slot.RefCount.store(1, std::memory_order_relaxed);
        }

        m_Impl->LiveLeaseCount.fetch_add(1, std::memory_order_relaxed);
        return BufferLease::Adopt(*this, deviceHandle);
    }

    // -----------------------------------------------------------------
    void BufferManager::Retain(BufferHandle handle)
    {
        // Fast path: no lock needed, only the slot's atomic.
        BufferSlot* slot = m_Impl->Resolve(handle);
        assert(slot && "Retain called on invalid or released handle");
        if (slot)
        {
            slot->RefCount.fetch_add(1, std::memory_order_relaxed);
            m_Impl->LiveLeaseCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // -----------------------------------------------------------------
    void BufferManager::Release(BufferHandle handle)
    {
        // Snapshot the device handle under the map lookup. We cannot hold a
        // raw `BufferSlot*` across the final `Slots.erase(handle)` below
        // because erase invalidates the node's address.
        std::uint32_t prev = 0;
        bool          shouldDestroy = false;
        {
            std::lock_guard lock{m_Impl->Mutex};
            const auto it = m_Impl->Slots.find(handle);
            assert(it != m_Impl->Slots.end() &&
                   "Release called on invalid or already-freed handle");
            if (it == m_Impl->Slots.end()) return;

            BufferSlot& slot = it->second;
            prev = slot.RefCount.fetch_sub(1, std::memory_order_acq_rel);
            assert(prev > 0 && "Refcount underflow");
            m_Impl->LiveLeaseCount.fetch_sub(1, std::memory_order_acq_rel);
            if (prev == 1)
            {
                shouldDestroy = true;
                m_Impl->Slots.erase(it);
            }
        }

        if (shouldDestroy)
        {
            // Last reference — destroy the GPU resource. The handle was the
            // device handle from the start, so we can pass it through.
            m_Impl->Device.DestroyBuffer(handle);
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








