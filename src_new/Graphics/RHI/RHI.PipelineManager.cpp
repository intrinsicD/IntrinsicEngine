module;

#include <atomic>
#include <cassert>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

module Extrinsic.RHI.PipelineManager;

import Extrinsic.Core.HandleLease;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;

// ============================================================
// PipelineManager — implementation
// ============================================================
// Each slot carries TWO device handles:
//
//   ActiveHandle  — the pipeline currently bound by renderers.
//                   Valid once IsReady == true.
//   PendingHandle — the result of a Recompile() call, waiting
//                   to be promoted by CommitPending().
//                   Invalid when no recompile is in progress.
//
// Recompile() is called (potentially from a watcher thread):
//   1. Calls IDevice::CreatePipeline with the new desc.
//   2. Acquires m_PendingMutex, writes PendingHandle + PendingDesc
//      into the slot, appends to m_PendingCommits.
//
// CommitPending() runs on the render thread:
//   1. Drains m_PendingCommits under m_PendingMutex.
//   2. For each entry: destroys OldActive, promotes Pending →
//      Active, clears Pending, fires OnCompiled callback.
//
// GetDeviceHandle() is lock-free: reads ActiveHandle atomically.
// IsReady() tests ActiveHandle.IsValid() without a lock.
// ============================================================

namespace Extrinsic::RHI
{
    // -----------------------------------------------------------------
    // Internal slot
    // -----------------------------------------------------------------
    struct PipelineSlot
    {
        PipelineDesc  Desc{};         // current (or pending) descriptor
        PipelineDesc  ActiveDesc{};   // descriptor of the live pipeline

        // Active pipeline — read lock-free via atomic index trick:
        // we store the raw Index + Generation in two uint32_t atomics.
        std::atomic<std::uint32_t> ActiveIndex{std::numeric_limits<std::uint32_t>::max()};
        std::atomic<std::uint32_t> ActiveGen{0};

        // Pending pipeline (protected by PipelineManager::Impl::PendingMutex)
        PipelineHandle PendingHandle{};

        PipelineCompiledCallback OnCompiled{};

        std::atomic<std::uint32_t> RefCount{0};
        std::uint32_t              Generation = 0;
        bool                       Live       = false;

        PipelineSlot() = default;
        PipelineSlot(const PipelineSlot&)            = delete;
        PipelineSlot& operator=(const PipelineSlot&) = delete;

        // Convenience: read the active device handle atomically.
        [[nodiscard]] PipelineHandle LoadActive() const noexcept
        {
            const std::uint32_t idx = ActiveIndex.load(std::memory_order_acquire);
            const std::uint32_t gen = ActiveGen.load(std::memory_order_acquire);
            return PipelineHandle{idx, gen};
        }

        void StoreActive(PipelineHandle h) noexcept
        {
            ActiveIndex.store(h.Index,      std::memory_order_release);
            ActiveGen  .store(h.Generation, std::memory_order_release);
        }
    };

    // -----------------------------------------------------------------
    // Commit record — one per in-flight Recompile()
    // -----------------------------------------------------------------
    struct PendingCommit
    {
        std::uint32_t  PoolIndex;
        std::uint32_t  PoolGeneration;
        PipelineHandle NewDeviceHandle;  // freshly compiled, not yet promoted
        PipelineHandle OldDeviceHandle;  // to be destroyed after promotion
        PipelineDesc   NewDesc;
    };

    // -----------------------------------------------------------------
    // Impl
    // -----------------------------------------------------------------
    struct PipelineManager::Impl
    {
        IDevice&                    Device;
        std::mutex                  SlotMutex;     // guards Slots + FreeList
        std::deque<PipelineSlot>    Slots;
        std::vector<std::uint32_t>  FreeList;

        std::mutex                   PendingMutex;  // guards PendingCommits
        std::vector<PendingCommit>   PendingCommits;

        explicit Impl(IDevice& device) : Device(device) {}

        [[nodiscard]] PipelineSlot* Resolve(PipelineHandle handle) noexcept
        {
            if (!handle.IsValid()) return nullptr;
            if (handle.Index >= static_cast<std::uint32_t>(Slots.size())) return nullptr;
            PipelineSlot& slot = Slots[handle.Index];
            if (!slot.Live || slot.Generation != handle.Generation) return nullptr;
            return &slot;
        }

        [[nodiscard]] const PipelineSlot* Resolve(PipelineHandle handle) const noexcept
        {
            if (!handle.IsValid()) return nullptr;
            if (handle.Index >= static_cast<std::uint32_t>(Slots.size())) return nullptr;
            const PipelineSlot& slot = Slots[handle.Index];
            if (!slot.Live || slot.Generation != handle.Generation) return nullptr;
            return &slot;
        }
    };

    // -----------------------------------------------------------------
    // PipelineManager
    // -----------------------------------------------------------------
    PipelineManager::PipelineManager(IDevice& device)
        : m_Impl(std::make_unique<Impl>(device))
    {}

    PipelineManager::~PipelineManager()
    {
        // Drain any pending commits that were never promoted (e.g. shutdown
        // race).  Destroy the newly compiled but unswapped device pipelines.
        for (const PendingCommit& pc : m_Impl->PendingCommits)
            m_Impl->Device.DestroyPipeline(pc.NewDeviceHandle);
    }

    // -----------------------------------------------------------------
    PipelineManager::PipelineLease PipelineManager::Create(
        const PipelineDesc&      desc,
        PipelineCompiledCallback onCompiled)
    {
        // Compile synchronously on the calling (render) thread.
        PipelineHandle deviceHandle = m_Impl->Device.CreatePipeline(desc);
        if (!deviceHandle.IsValid())
            return {};

        std::uint32_t index;
        std::uint32_t generation;

        {
            std::lock_guard lock{m_Impl->SlotMutex};

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

            PipelineSlot& slot = m_Impl->Slots[index];
            slot.Desc          = desc;
            slot.ActiveDesc    = desc;
            slot.OnCompiled    = std::move(onCompiled);
            slot.Generation    = generation;
            slot.Live          = true;
            slot.StoreActive(deviceHandle);
            slot.PendingHandle = {};
            slot.RefCount.store(1, std::memory_order_relaxed);
        }

        // Fire the callback immediately for the initial compile.
        PipelineHandle poolHandle{index, generation};
        if (m_Impl->Slots[index].OnCompiled)
            m_Impl->Slots[index].OnCompiled(poolHandle);

        return PipelineLease::Adopt(*this, poolHandle);
    }

    // -----------------------------------------------------------------
    void PipelineManager::Retain(PipelineHandle handle)
    {
        PipelineSlot* slot = m_Impl->Resolve(handle);
        assert(slot && "Retain called on invalid or released handle");
        if (slot)
            slot->RefCount.fetch_add(1, std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------
    void PipelineManager::Release(PipelineHandle handle)
    {
        PipelineSlot* slot = m_Impl->Resolve(handle);
        assert(slot && "Release called on invalid or already-freed handle");
        if (!slot) return;

        const std::uint32_t prev =
            slot->RefCount.fetch_sub(1, std::memory_order_acq_rel);

        assert(prev > 0 && "Refcount underflow");

        if (prev == 1)
        {
            // Destroy both active and any pending (if shutdown races Recompile).
            m_Impl->Device.DestroyPipeline(slot->LoadActive());
            {
                std::lock_guard lock{m_Impl->PendingMutex};
                if (slot->PendingHandle.IsValid())
                    m_Impl->Device.DestroyPipeline(slot->PendingHandle);
            }

            std::lock_guard lock{m_Impl->SlotMutex};
            slot->Live = false;
            slot->Generation++;
            m_Impl->FreeList.push_back(handle.Index);
        }
    }

    // -----------------------------------------------------------------
    PipelineManager::PipelineLease PipelineManager::AcquireLease(PipelineHandle handle)
    {
        return PipelineLease::RetainNew(*this, handle);
    }

    // -----------------------------------------------------------------
    void PipelineManager::Recompile(PipelineHandle handle, const PipelineDesc& newDesc)
    {
        // --- Resolve the pool slot (lock-free read) ---
        // We read Live + Generation under SlotMutex to avoid racing with Release.
        std::uint32_t poolIndex;
        std::uint32_t poolGeneration;
        PipelineHandle oldActive;
        {
            std::lock_guard lock{m_Impl->SlotMutex};
            PipelineSlot*   slot = m_Impl->Resolve(handle);
            if (!slot) return;
            poolIndex      = handle.Index;
            poolGeneration = handle.Generation;
            oldActive      = slot->LoadActive();
            slot->Desc     = newDesc; // visible via GetDesc() immediately
        }

        // --- Compile off the hot path (no locks held) ---
        PipelineHandle newDevice = m_Impl->Device.CreatePipeline(newDesc);
        if (!newDevice.IsValid())
            return; // compilation failed; keep the old pipeline active

        // --- Stage the result ---
        std::lock_guard lock{m_Impl->PendingMutex};

        // If a previous Recompile() result hasn't been committed yet,
        // discard it (the newer compile supersedes it).
        for (PendingCommit& pc : m_Impl->PendingCommits)
        {
            if (pc.PoolIndex == poolIndex && pc.PoolGeneration == poolGeneration)
            {
                m_Impl->Device.DestroyPipeline(pc.NewDeviceHandle);
                pc.NewDeviceHandle = newDevice;
                pc.NewDesc         = newDesc;
                return;
            }
        }

        m_Impl->PendingCommits.push_back({
            .PoolIndex       = poolIndex,
            .PoolGeneration  = poolGeneration,
            .NewDeviceHandle = newDevice,
            .OldDeviceHandle = oldActive,
            .NewDesc         = newDesc,
        });
    }

    // -----------------------------------------------------------------
    void PipelineManager::CommitPending()
    {
        // Drain under PendingMutex, then do GPU work outside the lock.
        std::vector<PendingCommit> toCommit;
        {
            std::lock_guard lock{m_Impl->PendingMutex};
            toCommit.swap(m_Impl->PendingCommits);
        }

        for (PendingCommit& pc : toCommit)
        {
            PipelineHandle poolHandle{pc.PoolIndex, pc.PoolGeneration};
            PipelineSlot*  slot = m_Impl->Resolve(poolHandle);
            if (!slot)
            {
                // Slot was freed before we committed — just destroy the orphan.
                m_Impl->Device.DestroyPipeline(pc.NewDeviceHandle);
                continue;
            }

            // Promote new → active.
            slot->StoreActive(pc.NewDeviceHandle);
            slot->ActiveDesc   = pc.NewDesc;
            slot->PendingHandle = {};

            // Destroy the old pipeline now that the GPU is done with it.
            // Caller is responsible for ensuring no in-flight GPU work uses
            // the old pipeline (e.g. called after WaitIdle or with a frame fence).
            m_Impl->Device.DestroyPipeline(pc.OldDeviceHandle);

            // Notify the pass that registered for callbacks.
            if (slot->OnCompiled)
                slot->OnCompiled(poolHandle);
        }
    }

    // -----------------------------------------------------------------
    bool PipelineManager::IsReady(PipelineHandle handle) const noexcept
    {
        const PipelineSlot* slot = m_Impl->Resolve(handle);
        return slot && slot->LoadActive().IsValid();
    }

    // -----------------------------------------------------------------
    PipelineHandle PipelineManager::GetDeviceHandle(PipelineHandle handle) const noexcept
    {
        const PipelineSlot* slot = m_Impl->Resolve(handle);
        return slot ? slot->LoadActive() : PipelineHandle{};
    }

    // -----------------------------------------------------------------
    const PipelineDesc* PipelineManager::GetDesc(PipelineHandle handle) const noexcept
    {
        const PipelineSlot* slot = m_Impl->Resolve(handle);
        return slot ? &slot->Desc : nullptr;
    }

    // -----------------------------------------------------------------
    std::uint32_t PipelineManager::GetLiveCount() const noexcept
    {
        std::lock_guard lock{m_Impl->SlotMutex};
        // live count = total slots - free list
        return static_cast<std::uint32_t>(m_Impl->Slots.size() - m_Impl->FreeList.size());
    }

} // namespace Extrinsic::RHI

