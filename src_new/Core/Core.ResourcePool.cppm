module;

#include <concepts>
#include <cstdint>
#include <deque>
#include <expected>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>

export module Extrinsic.Core.ResourcePool;

import Extrinsic.Core.Error;

// -----------------------------------------------------------------------
// Extrinsic::Core::ResourcePool<T, Handle, RetirementFrames>
// -----------------------------------------------------------------------
// Thread-safe generational pool with deferred (GPU-safe) slot reclamation.
//
// RetirementFrames: slots deleted via Remove() are not immediately recycled.
// ProcessDeletions(frameNumber) must be called each frame; a slot is only
// returned to the free-list after `RetirementFrames` frames have elapsed.
// For GPU-facing pools set RetirementFrames to the swapchain frames-in-flight
// count (typically 2–3); for CPU-only pools use 0 for immediate reclamation.
//
// All public methods are thread-safe: Add/Get/GetIfValid/Remove share a
// std::shared_mutex (readers concurrent, writers exclusive).
// ProcessDeletions and Clear must be called from a single maintenance thread.
// -----------------------------------------------------------------------

export namespace Extrinsic::Core
{
    template <typename H>
    concept GenerationalHandle = requires(H h)
    {
        { h.Index }      -> std::convertible_to<uint32_t>;
        { h.Generation } -> std::convertible_to<uint32_t>;
    };

    template <typename T, GenerationalHandle Handle, uint32_t RetirementFrames = 2>
    class ResourcePool
    {
    public:
        ResourcePool() = default;
        ResourcePool(const ResourcePool&) = delete;
        ResourcePool& operator=(const ResourcePool&) = delete;
        ResourcePool(ResourcePool&&) noexcept = default;
        ResourcePool& operator=(ResourcePool&&) noexcept = default;

        // --- Write path ------------------------------------------------

        Handle Add(T resource)
        {
            std::unique_lock lock(m_Mutex);
            const uint32_t index = AllocSlot();
            Slot& slot = m_Slots[index];
            slot.Data.emplace(std::move(resource));
            ++slot.Generation;
            slot.IsActive = true;
            return Handle{index, slot.Generation};
        }

        template <typename... Args>
        Handle Emplace(Args&&... args)
        {
            return Add(T(std::forward<Args>(args)...));
        }

        // Soft-delete: deactivates the slot immediately (Get() returns error)
        // but defers memory reclamation by RetirementFrames.
        void Remove(Handle handle, uint64_t globalFrameNumber)
        {
            std::unique_lock lock(m_Mutex);
            if (handle.Index >= m_Slots.size())
                return;
            Slot& slot = m_Slots[handle.Index];
            if (slot.IsActive && slot.Generation == handle.Generation)
            {
                slot.IsActive = false;
                m_PendingKill.push_back({handle.Index, handle.Generation, globalFrameNumber});
            }
        }

        // Must be called once per frame on the maintenance thread.
        void ProcessDeletions(uint64_t globalFrameNumber)
        {
            if (m_PendingKill.empty())
                return;
            std::unique_lock lock(m_Mutex);
            std::erase_if(m_PendingKill, [&](const PendingKill& k)
            {
                if (globalFrameNumber <= k.KillFrame + RetirementFrames)
                    return false;
                if (k.SlotIndex < m_Slots.size())
                {
                    Slot& slot = m_Slots[k.SlotIndex];
                    if (!slot.IsActive && slot.Generation == k.Generation)
                    {
                        slot.Data.reset();
                        m_Free.push_back(k.SlotIndex);
                    }
                }
                return true;
            });
        }

        void Clear()
        {
            std::unique_lock lock(m_Mutex);
            m_PendingKill.clear();
            m_Slots.clear();
            m_Free.clear();
        }

        // --- Read path -------------------------------------------------

        // Returns Err(ResourceNotFound) for invalid / stale / inactive handles.
        [[nodiscard]] Core::Expected<T*> Get(Handle handle) const
        {
            std::shared_lock lock(m_Mutex);
            if (handle.Index >= m_Slots.size())
                return std::unexpected(Core::ErrorCode::ResourceNotFound);
            const Slot& slot = m_Slots[handle.Index];
            if (!slot.IsActive || slot.Generation != handle.Generation || !slot.Data)
                return std::unexpected(Core::ErrorCode::ResourceNotFound);
            return const_cast<T*>(&*slot.Data);
        }

        // Hot-path variant: returns nullptr on invalid handle (no Expected overhead).
        [[nodiscard]] T* GetIfValid(Handle handle) const noexcept
        {
            std::shared_lock lock(m_Mutex);
            if (handle.Index < m_Slots.size())
            {
                const Slot& slot = m_Slots[handle.Index];
                if (slot.IsActive && slot.Generation == handle.Generation && slot.Data)
                    return const_cast<T*>(&*slot.Data);
            }
            return nullptr;
        }

        // Raw index access — skips generation check. Used for bulk GPU uploads.
        [[nodiscard]] const T* GetByIndex(uint32_t index) const noexcept
        {
            std::shared_lock lock(m_Mutex);
            if (index >= m_Slots.size())
                return nullptr;
            const Slot& slot = m_Slots[index];
            if (!slot.IsActive || !slot.Data)
                return nullptr;
            return &*slot.Data;
        }

        [[nodiscard]] std::size_t Capacity() const
        {
            std::shared_lock lock(m_Mutex);
            return m_Slots.size();
        }

    private:
        uint32_t AllocSlot()
        {
            if (!m_Free.empty())
            {
                const uint32_t idx = m_Free.front();
                m_Free.pop_front();
                return idx;
            }
            const uint32_t idx = static_cast<uint32_t>(m_Slots.size());
            m_Slots.emplace_back();
            return idx;
        }

        struct Slot
        {
            std::optional<T> Data;
            uint32_t Generation = 0;
            bool IsActive = false;
        };

        struct PendingKill
        {
            uint32_t SlotIndex;
            uint32_t Generation;
            uint64_t KillFrame;
        };

        mutable std::shared_mutex m_Mutex;
        std::deque<Slot>     m_Slots;
        std::deque<uint32_t> m_Free;
        std::vector<PendingKill> m_PendingKill;
    };
}

