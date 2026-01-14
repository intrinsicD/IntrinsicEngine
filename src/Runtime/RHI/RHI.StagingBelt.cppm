module;
#include <cstddef>
#include <cstdint>
#include <memory>
#include <deque>
#include <mutex>
#include <utility>
#include "RHI.Vulkan.hpp"

export module RHI:StagingBelt;

import :Device;
import :Buffer;

export namespace RHI
{
    // A persistent, host-visible staging ring-buffer used to suballocate upload slices.
    // Lifetime is tied to timeline semaphore values (TransferToken).
    class StagingBelt
    {
    public:
        struct Allocation
        {
            VkBuffer Buffer = VK_NULL_HANDLE;
            size_t Offset = 0;
            void* MappedPtr = nullptr; // points to start of allocation (MappedBase + Offset)
            size_t Size = 0;
        };

        StagingBelt(std::shared_ptr<VulkanDevice> device, size_t capacityBytes);
        ~StagingBelt();

        StagingBelt(const StagingBelt&) = delete;
        StagingBelt& operator=(const StagingBelt&) = delete;

        // Thread-safe allocation from the ring.
        // alignment must satisfy vkCmdCopyBuffer srcOffset requirements.
        [[nodiscard]] Allocation Allocate(size_t sizeBytes, size_t alignment);

        // Marks all allocations done since the last Retire() as being in-flight until the signaled timeline value completes.
        void Retire(uint64_t retireValue);

        // Advances the head by reclaiming completed in-flight regions.
        // Must be called periodically (e.g., once per frame) with the completed timeline value.
        void GarbageCollect(uint64_t completedValue);

        [[nodiscard]] size_t Capacity() const noexcept { return m_Capacity; }

        // Allocation helper for vkCmdCopyBufferToImage paths.
        // Ensures bufferOffset satisfies Vulkan alignment requirements for linear->optimal copies.
        [[nodiscard]] Allocation AllocateForImageUpload(size_t sizeBytes, size_t texelBlockSize, size_t rowPitchBytes,
                                                        size_t optimalBufferCopyOffsetAlignment,
                                                        size_t optimalBufferCopyRowPitchAlignment);

    private:
        struct Region
        {
            size_t Begin = 0;
            size_t End = 0; // exclusive
            uint64_t RetireValue = 0;
        };

        [[nodiscard]] static size_t AlignUp(size_t value, size_t alignment);
        [[nodiscard]] bool HasSpaceContiguous(size_t alignedTail, size_t sizeBytes) const;

        std::shared_ptr<VulkanDevice> m_Device;
        std::unique_ptr<VulkanBuffer> m_Buffer;

        void* m_MappedBase = nullptr;
        size_t m_Capacity = 0;

        // Ring pointers
        size_t m_Head = 0; // oldest live byte
        size_t m_Tail = 0; // next free byte

        // Staging allocated but not yet retired (part of the currently recording batch)
        size_t m_PendingBegin = 0;
        size_t m_PendingEnd = 0;
        bool m_HasPending = false;

        std::deque<Region> m_InFlight;
        mutable std::mutex m_Mutex;
    };
}
