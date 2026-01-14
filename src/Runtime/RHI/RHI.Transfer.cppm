module;
#include <cstdint>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include "RHI.Vulkan.hpp" // Implementation detail, kept in module fragment if possible, but needed for class members

export module RHI:Transfer;

import :Device;
import :Buffer;
import :StagingBelt;

export namespace RHI {

    // 1. Pure C++ Token (No Vulkan Header required for users of this struct)
    struct TransferToken {
        uint64_t Value = 0;
        [[nodiscard]] bool IsValid() const { return Value > 0; }
        auto operator<=>(const TransferToken&) const = default;
    };

    // 2. The Manager
    class TransferManager {
    public:
        explicit TransferManager(std::shared_ptr<VulkanDevice> device);
        ~TransferManager();

        // Returns a command buffer in the "Recording" state.
        // The caller (Loader) records copies into this.
        [[nodiscard]] VkCommandBuffer Begin();

        // Persistent staging belt allocation for buffer uploads.
        // Returns {buffer, offset, ptr} for caller to memcpy into.
        [[nodiscard]] StagingBelt::Allocation AllocateStaging(size_t sizeBytes, size_t alignment);

        // Persistent staging belt allocation for image uploads (vkCmdCopyBufferToImage).
        // texelBlockSize should be bytes per texel/block for the source format (e.g. 4 for RGBA8).
        [[nodiscard]] StagingBelt::Allocation AllocateStagingForImage(size_t sizeBytes,
                                                                      size_t texelBlockSize,
                                                                      size_t rowPitchBytes,
                                                                      size_t optimalBufferCopyOffsetAlignment,
                                                                      size_t optimalBufferCopyRowPitchAlignment);

        // Submits the work.
        // 'stagingBuffers' are kept alive internally until the GPU passes the timeline point.
        // Returns a generic token.
        [[nodiscard]] TransferToken Submit(VkCommandBuffer cmd, std::vector<std::unique_ptr<VulkanBuffer>>&& stagingBuffers);

        // Variant that retires any staging-belt allocations made since the last submission.
        [[nodiscard]] TransferToken Submit(VkCommandBuffer cmd);

        // Thread-safe check.
        [[nodiscard]] bool IsCompleted(TransferToken token) const;

        // Cleanup resources for completed tokens.
        void GarbageCollect();

    private:
        struct ThreadTransferContext {
            VkCommandPool Pool = VK_NULL_HANDLE;
            void* Owner = nullptr;
        };

        ThreadTransferContext& GetThreadContext();

        std::shared_ptr<VulkanDevice> m_Device;
        VkQueue m_TransferQueue = VK_NULL_HANDLE;
        VkSemaphore m_TimelineSemaphore = VK_NULL_HANDLE;

        // Atomic is strictly required as Loaders run on threads, but Engine checks on Main.
        std::atomic<uint64_t> m_NextTicket{1};

        std::unique_ptr<StagingBelt> m_StagingBelt;

        struct PendingBatch {
            TransferToken Token;
            std::vector<std::unique_ptr<VulkanBuffer>> Resources;
        };
        std::vector<PendingBatch> m_InFlightBatches;
        mutable std::mutex m_Mutex; // Protects m_InFlightBatches and staging retire/GC
    };
}