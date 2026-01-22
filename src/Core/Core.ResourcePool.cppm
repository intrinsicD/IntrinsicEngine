module;

#include <mutex>
#include <shared_mutex>
#include <vector>
#include <deque>
#include <expected>
#include <memory>
#include <concepts>

export module Core:ResourcePool;
import :Error; // For ErrorCode, etc.

export namespace Core
{
    // Concept to ensure the Handle type fits the engine's requirements
    template<typename H>
    concept GenerationalHandle = requires(H h) {
        { h.Index } -> std::convertible_to<uint32_t>;
        { h.Generation } -> std::convertible_to<uint32_t>;
    };

    template <typename T, GenerationalHandle Handle>
    class ResourcePool
    {
    public:
        ResourcePool() = default;

        // Prevent copying, allow moving
        ResourcePool(const ResourcePool&) = delete;
        ResourcePool& operator=(const ResourcePool&) = delete;
        ResourcePool(ResourcePool&&) noexcept = default;
        ResourcePool& operator=(ResourcePool&&) noexcept = default;

        void Initialize(const uint32_t framesInFlight)
        {
            m_FramesInFlight = framesInFlight;
        }

        // Accepts unique_ptr to enforce ownership transfer
        Handle Add(std::unique_ptr<T> resource)
        {
            std::unique_lock lock(m_Mutex);

            uint32_t index;
            if (!m_FreeIndices.empty())
            {
                index = m_FreeIndices.front();
                m_FreeIndices.pop_front();
            }
            else
            {
                index = static_cast<uint32_t>(m_Slots.size());
                m_Slots.emplace_back();
            }

            Slot& slot = m_Slots[index];
            // POINTER STABILITY: We move the unique_ptr. The resource heap address remains constant
            // even if m_Slots vector reallocates.
            slot.Data = std::move(resource);
            ++slot.Generation;
            slot.IsActive = true;

            return {index, slot.Generation};
        }

        // Convenience overload for creating in-place
        template<typename... Args>
        Handle Create(Args&&... args) {
            return Add(std::make_unique<T>(std::forward<Args>(args)...));
        }

        void Remove(Handle handle, uint64_t currentFrameNumber)
        {
            std::unique_lock lock(m_Mutex);

            if (handle.Index >= m_Slots.size()) return;

            Slot& slot = m_Slots[handle.Index];
            // Check generation to prevent double-free of reused slots
            if (slot.IsActive && slot.Generation == handle.Generation)
            {
                slot.IsActive = false; // Soft delete immediately so Get() fails

                // Defer hard delete
                m_PendingKillList.push_back({
                    .SlotIndex = handle.Index,
                    .Generation = handle.Generation,
                    .KillFrameNumber = currentFrameNumber
                });
            }
        }

        void ProcessDeletions(uint64_t currentFrameNumber)
        {
            // Quick check without lock first
            if (m_PendingKillList.empty()) return;

            std::unique_lock lock(m_Mutex);

            std::erase_if(m_PendingKillList, [&](const PendingKill& item)
            {
                // Wait for FramesInFlight to pass
                if (currentFrameNumber <= item.KillFrameNumber + m_FramesInFlight)
                    return false;

                // Validate slot is still in the expected state
                if (item.SlotIndex < m_Slots.size())
                {
                    Slot& slot = m_Slots[item.SlotIndex];
                    if (!slot.IsActive && slot.Generation == item.Generation)
                    {
                        slot.Data.reset(); // Actually free memory here
                        m_FreeIndices.push_back(item.SlotIndex);
                    }
                }
                return true;
            });
        }

        [[nodiscard]] Core::Expected<T*> Get(Handle handle) const
        {
            std::shared_lock lock(m_Mutex);

            if (handle.Index >= m_Slots.size())
                return std::unexpected(Core::ErrorCode::ResourceNotFound);

            const Slot& slot = m_Slots[handle.Index];

            if (!slot.IsActive || slot.Generation != handle.Generation)
                return std::unexpected(Core::ErrorCode::ResourceNotFound);

            return slot.Data.get();
        }

        // Hot-path access.
        // WARNING: Only use if you are sure the handle is valid and the resource is alive.
        [[nodiscard]] T* GetUnchecked(Handle handle) const
        {
            std::shared_lock lock(m_Mutex);
            // We still do bounds/generation check because the cost is negligible compared to a cache miss,
            // and safety is preferred. If you need ABSOLUTE speed, remove the 'if'.
            if (handle.Index < m_Slots.size())
            {
                const Slot& slot = m_Slots[handle.Index];
                if (slot.IsActive && slot.Generation == handle.Generation)
                {
                    return slot.Data.get();
                }
            }
            return nullptr;
        }

        void Clear()
        {
            std::unique_lock lock(m_Mutex);
            m_PendingKillList.clear();
            m_Slots.clear();
            m_FreeIndices.clear();
        }

        [[nodiscard]] size_t Capacity() const {
            std::shared_lock lock(m_Mutex);
            return m_Slots.size();
        }

    private:
        struct Slot
        {
            std::unique_ptr<T> Data; // Unique Ptr ensures pointer stability
            uint32_t Generation = 0;
            bool IsActive = false;
        };

        struct PendingKill
        {
            uint32_t SlotIndex;
            uint32_t Generation;
            uint64_t KillFrameNumber;
        };

        std::vector<Slot> m_Slots;
        std::deque<uint32_t> m_FreeIndices;
        std::vector<PendingKill> m_PendingKillList;

        mutable std::shared_mutex m_Mutex;
        uint32_t m_FramesInFlight = 2;
    };
}
