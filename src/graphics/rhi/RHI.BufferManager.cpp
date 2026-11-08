module;

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

module Extrinsic.RHI.BufferManager;

import Extrinsic.Core.Error;
import Extrinsic.Core.HandleLease;
import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.BufferView;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.TransferQueue;

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
//   Retain   — O(1) average `find` under the mutex (a concurrent insert
//              or erase may rehash the map)
//   Release  — O(1) average; on zero, an O(1) average erase under the mutex
//   View     — O(1) average
//   GetDesc  — O(1) average
//   CreateScratch — O(parked) scan of the idle list (bounded by the budget)
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
        std::uint64_t ScratchClassBytes{}; // 0: a plain Create() buffer

        // Atomic member; node-based map storage gives stable addresses on
        // insertion/erasure so we do not need movability.
        BufferSlot() = default;
        BufferSlot(const BufferSlot&)            = delete;
        BufferSlot& operator=(const BufferSlot&) = delete;
        BufferSlot(BufferSlot&&)                 = delete;
        BufferSlot& operator=(BufferSlot&&)      = delete;
    };

    // A released scratch buffer. It keeps only host-owned values (no caller DebugName, callback or
    // deleter), so a parked buffer never refers into an unloaded plugin.
    struct ParkedScratch
    {
        BufferHandle Handle{};
        BufferUsage Usage{};
        bool HostVisible{}, HostReadback{};
        std::uint64_t ClassBytes{};
        RetirementStamp Stamp{};
        std::uint64_t LastUse{};
    };

    RetirementStamp StampRetirement(IDevice& device)
    {
        return {.SafeFrame = device.GetGlobalFrameNumber() + device.GetFramesInFlight() + 1u,
            .ComputeFence = device.GetTransferQueue().ComputeFence()};
    }

    bool IsRetired(IDevice& device, const RetirementStamp& stamp)
    {
        return device.GetGlobalFrameNumber() >= stamp.SafeFrame &&
            device.GetTransferQueue().IsComplete(stamp.ComputeFence);
    }

    // -----------------------------------------------------------------
    // Impl
    // -----------------------------------------------------------------
    extern "C++"
    {
        struct BufferManager::Impl
        {
            IDevice& Device;
            std::mutex Mutex;       // guards Slots
            std::unordered_map<BufferHandle, BufferSlot, Core::StrongHandleHash<BufferTag>> Slots;

        // Count of live BufferLease instances outstanding against this manager.
        // Incremented on every Create (primary lease) and every Retain
        // (additional lease via Share / AcquireLease). Decremented on every
        // Release. The destructor asserts this is zero to catch the
        // lease-outlives-manager UAF in Debug (see F2 in the code review).
            std::atomic<std::uint32_t> LiveLeaseCount{0};

            // Scratch cache, guarded by Mutex. Bytes are class bytes; Stats.PendingBytes covers Returned and Pending.
            std::vector<ParkedScratch> Returned, Pending, Idle;
            ScratchStats Stats{.BudgetBytes = ~std::uint64_t{0}};
            std::uint64_t UseClock{};
            bool Drained{};

            explicit Impl(IDevice& device) : Device(device) {}

            // Returns the slot if the handle is currently registered with this
            // manager. Generation mismatches naturally produce a miss because the
            // map is keyed by the full (Index, Generation) handle published by
            // the device.
            [[nodiscard]] BufferSlot* Resolve(BufferHandle handle) noexcept
            {
                if (!handle.IsValid())
                    return nullptr;
                const auto it = Slots.find(handle);
                return it == Slots.end() ? nullptr : &it->second;
            }

            [[nodiscard]] const BufferSlot* Resolve(BufferHandle handle) const noexcept
            {
                if (!handle.IsValid())
                    return nullptr;
                const auto it = Slots.find(handle);
                return it == Slots.end() ? nullptr : &it->second;
            }
        };

        // -----------------------------------------------------------------
        // BufferManager
        // -----------------------------------------------------------------
        BufferManager::BufferManager(IDevice& device) : m_Impl(std::make_unique<Impl>(device)) {}

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
            DrainScratch();
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
                slot.Desc = desc;
                slot.RefCount.store(1, std::memory_order_relaxed);
            }

            m_Impl->LiveLeaseCount.fetch_add(1, std::memory_order_relaxed);
            return BufferLease::Adopt(*this, deviceHandle);
        }

        // -----------------------------------------------------------------
        void BufferManager::Retain(BufferHandle handle)
        {
            // The lookup holds the mutex: another thread's last release or a Create may erase or
            // insert (and rehash) concurrently.
            std::lock_guard lock{m_Impl->Mutex};
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
            bool shouldDestroy = false;
            auto& s = *m_Impl;
            {
                std::lock_guard lock{m_Impl->Mutex};
                const auto it = m_Impl->Slots.find(handle);
                assert(it != m_Impl->Slots.end() && "Release called on invalid or already-freed handle");
                if (it == m_Impl->Slots.end())
                    return;

                BufferSlot& slot = it->second;
                prev = slot.RefCount.fetch_sub(1, std::memory_order_acq_rel);
                assert(prev > 0 && "Refcount underflow");
                m_Impl->LiveLeaseCount.fetch_sub(1, std::memory_order_acq_rel);
                if (prev == 1)
                {
                    const BufferDesc desc = slot.Desc;
                    const std::uint64_t classBytes = slot.ScratchClassBytes;
                    m_Impl->Slots.erase(it);
                    shouldDestroy = classBytes == 0u || s.Drained;
                    if (classBytes != 0u)
                    {
                        s.Stats.ActiveBytes -= classBytes;
                        if (!s.Drained)
                        {
                            s.Returned.push_back({.Handle = handle,
                                .Usage = desc.Usage,
                                .HostVisible = desc.HostVisible,
                                .HostReadback = desc.HostReadback,
                                .ClassBytes = classBytes});
                            s.Stats.PendingBytes += classBytes;
                        }
                    }
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
        BufferView BufferManager::View(BufferHandle handle, std::uint64_t offset, std::uint64_t size) const noexcept
        {
            std::lock_guard lock{m_Impl->Mutex};
            const BufferSlot* slot = m_Impl->Resolve(handle);
            if (!slot)
                return {};

            const std::uint64_t capacity = slot->Desc.SizeBytes;

            if (offset >= capacity)
                return {};

            const std::uint64_t clampedSize =
                (size == WholeBuffer) ? (capacity - offset) : std::min(size, capacity - offset);

            return BufferView{.Buffer = handle, .Offset = offset, .Size = clampedSize};
        }

        // -----------------------------------------------------------------
        const BufferDesc* BufferManager::GetDesc(BufferHandle handle) const noexcept
        {
            // The node stays put while a lease holds it; the lock only guards the lookup.
            std::lock_guard lock{m_Impl->Mutex};
            const BufferSlot* slot = m_Impl->Resolve(handle);
            return slot ? &slot->Desc : nullptr;
        }

        // -----------------------------------------------------------------
        Core::Expected<BufferManager::BufferLease> BufferManager::CreateScratch(const BufferDesc& desc)
        {
            auto& s = *m_Impl;
            if (!s.Device.IsOperational())
                return Core::Err<BufferLease>(Core::ErrorCode::DeviceNotOperational);
            const std::uint64_t classBytes = ScratchClassBytes(desc.SizeBytes);
            const auto adopt = [&](const BufferHandle handle)
            {
                // Under the mutex; the caller already counted classBytes as active.
                const auto [it, inserted] = s.Slots.try_emplace(handle);
                assert(inserted && "BufferManager::CreateScratch — duplicate live BufferHandle");
                (void)inserted;
                it->second.Desc = desc;
                it->second.ScratchClassBytes = classBytes;
                it->second.RefCount.store(1, std::memory_order_relaxed);
                s.LiveLeaseCount.fetch_add(1, std::memory_order_relaxed);
                return BufferLease::Adopt(*this, handle);
            };
            std::vector<ParkedScratch> evicted;
            {
                std::lock_guard lock{s.Mutex};
                const auto refuse = [&](const Core::ErrorCode code)
                {
                    ++s.Stats.Refusals;
                    s.Stats.LastRefusedBytes = classBytes;
                    s.Stats.LastRefusalCommittedBytes = s.Stats.ActiveBytes + s.Stats.PendingBytes;
                    return Core::Err<BufferLease>(code);
                };
                if (classBytes == 0u)
                    return refuse(Core::ErrorCode::OutOfRange);
                // The most recently used idle buffer of the same class and memory kind.
                auto hit = s.Idle.end();
                for (auto it = s.Idle.begin(); it != s.Idle.end(); ++it)
                    if (it->ClassBytes == classBytes && it->Usage == desc.Usage &&
                        it->HostVisible == desc.HostVisible && it->HostReadback == desc.HostReadback &&
                        (hit == s.Idle.end() || it->LastUse > hit->LastUse))
                        hit = it;
                if (hit != s.Idle.end())
                {
                    const BufferHandle handle = hit->Handle;
                    s.Idle.erase(hit);
                    s.Stats.IdleBytes -= classBytes;
                    s.Stats.ActiveBytes += classBytes;
                    ++s.Stats.Hits;
                    return adopt(handle);
                }
                // Only idle buffers are evictable: active and not-yet-retired bytes stay committed.
                const std::uint64_t committed = s.Stats.ActiveBytes + s.Stats.PendingBytes;
                if (committed > s.Stats.BudgetBytes || classBytes > s.Stats.BudgetBytes - committed)
                    return refuse(Core::ErrorCode::ResourceBudgetExceeded);
                while (s.Stats.IdleBytes > s.Stats.BudgetBytes - committed - classBytes)
                {
                    const auto oldest = std::ranges::min_element(s.Idle, {}, &ParkedScratch::LastUse);
                    s.Stats.IdleBytes -= oldest->ClassBytes;
                    ++s.Stats.Evictions;
                    evicted.push_back(*oldest);
                    s.Idle.erase(oldest);
                }
                s.Stats.ActiveBytes += classBytes; // reserved while the device allocates
                ++s.Stats.Misses;
            }
            for (const auto& parked : evicted)
                s.Device.DestroyBuffer(parked.Handle);
            BufferDesc physical = desc;
            physical.SizeBytes = classBytes;
            const BufferHandle handle = s.Device.CreateBuffer(physical);
            std::lock_guard lock{s.Mutex};
            if (!handle.IsValid())
            {
                s.Stats.ActiveBytes -= classBytes;
                return Core::Err<BufferLease>(Core::ErrorCode::OutOfDeviceMemory);
            }
            return adopt(handle);
        }

        // -----------------------------------------------------------------
        bool BufferManager::EnsureScratch(BufferLease& lease, const BufferDesc& desc)
        {
            if (const BufferDesc* current = lease.IsValid() ? GetDesc(lease.GetHandle()) : nullptr; current &&
                current->SizeBytes >= desc.SizeBytes && current->Usage == desc.Usage &&
                current->HostVisible == desc.HostVisible && current->HostReadback == desc.HostReadback)
                return true;
            lease = {};
            auto created = CreateScratch(desc);
            if (!created)
                return false;
            lease = std::move(*created);
            return true;
        }

        // -----------------------------------------------------------------
        void BufferManager::SetScratchBudget(const std::uint64_t bytes) noexcept
        {
            std::lock_guard lock{m_Impl->Mutex};
            m_Impl->Stats.BudgetBytes = bytes;
        }

        // -----------------------------------------------------------------
        void BufferManager::CollectScratch(const RetirementStamp& returned)
        {
            // Pending is touched only on the owner thread; releases append to Returned concurrently. The
            // retirement query runs outside the mutex.
            auto& s = *m_Impl;
            std::vector<ParkedScratch> pending;
            {
                std::lock_guard lock{s.Mutex};
                for (auto& parked : s.Returned)
                {
                    parked.Stamp = returned;
                    s.Pending.push_back(parked);
                }
                s.Returned.clear();
                pending.swap(s.Pending);
            }
            const auto waiting = std::ranges::stable_partition(pending,
                [&](const ParkedScratch& parked)
                {
                    return IsRetired(s.Device, parked.Stamp);
                });
            std::lock_guard lock{s.Mutex};
            for (auto it = pending.begin(); it != waiting.begin(); ++it)
            {
                it->LastUse = ++s.UseClock;
                s.Stats.PendingBytes -= it->ClassBytes;
                s.Stats.IdleBytes += it->ClassBytes;
                s.Idle.push_back(*it);
            }
            s.Pending.assign(waiting.begin(), waiting.end());
        }

        // -----------------------------------------------------------------
        void BufferManager::DrainScratch()
        {
            auto& s = *m_Impl;
            std::vector<ParkedScratch> parked;
            {
                std::lock_guard lock{s.Mutex};
                s.Drained = true;
                for (auto* list : {&s.Returned, &s.Pending, &s.Idle})
                {
                    parked.insert(parked.end(), list->begin(), list->end());
                    list->clear();
                }
                s.Stats.PendingBytes = s.Stats.IdleBytes = 0u;
            }
            for (const auto& buffer : parked)
                s.Device.DestroyBuffer(buffer.Handle);
        }

        // -----------------------------------------------------------------
        ScratchStats BufferManager::GetScratchStats() const
        {
            std::lock_guard lock{m_Impl->Mutex};
            return m_Impl->Stats;
        }

    } // extern "C++"
} // namespace Extrinsic::RHI
