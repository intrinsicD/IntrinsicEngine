module;

#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <span>
#include <cstddef>
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
        explicit TransferManager(VulkanDevice& device);
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

        // Convenience helper: upload a CPU memory block into a destination buffer via staging belt.
        // - dst must be created with VK_BUFFER_USAGE_TRANSFER_DST_BIT.
        // - This is safe for GPU_ONLY buffers.
        [[nodiscard]] TransferToken UploadBuffer(VkBuffer dst,
                                                std::span<const std::byte> src,
                                                VkDeviceSize dstOffset = 0);

        // --- Batched uploads -------------------------------------------------
        // Use this when you have many small uploads and want a single Begin/Submit.
        struct UploadBatchConfig
        {
            size_t CopyAlignment = 0; // 0 => use device optimalBufferCopyOffsetAlignment (clamped to >= 16)
        };

        [[nodiscard]] VkCommandBuffer BeginUploadBatch();
        [[nodiscard]] VkCommandBuffer BeginUploadBatch(const UploadBatchConfig& cfg);

        // Enqueue a buffer upload into an already-recording transfer command buffer.
        // Returns false if staging allocation failed.
        [[nodiscard]] bool EnqueueUploadBuffer(VkCommandBuffer cmd,
                                               VkBuffer dst,
                                               std::span<const std::byte> src,
                                               VkDeviceSize dstOffset = 0,
                                               size_t copyAlignment = 0);

        // Ends+submits the batch. Equivalent to Submit(cmd).
        [[nodiscard]] TransferToken EndUploadBatch(VkCommandBuffer cmd);

    private:
        struct ThreadTransferContext {
            VkCommandPool Pool = VK_NULL_HANDLE;
            void* Owner = nullptr;
        };

        ThreadTransferContext& GetThreadContext();

        VulkanDevice& m_Device;
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