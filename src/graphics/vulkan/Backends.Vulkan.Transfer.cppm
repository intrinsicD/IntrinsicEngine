module;

#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <span>

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Transfer;

export import Extrinsic.Core.ResourcePool;
export import Extrinsic.Core.Telemetry;
export import Extrinsic.RHI.Handles;
export import Extrinsic.RHI.Transfer;
export import Extrinsic.RHI.TransferQueue;
export import :Memory;
export import :Pipelines;
export import :Sync;

namespace Extrinsic::Backends::Vulkan
{
    export constexpr uint64_t kStagingBeltCapacity = 64ull * 1024 * 1024;

    export class VulkanDevice;

    export class StagingBelt
    {
    public:
        struct Allocation
        {
            VkBuffer Buffer     = VK_NULL_HANDLE;
            size_t   Offset     = 0;
            void*    MappedPtr  = nullptr;
            size_t   Size       = 0;
        };

        explicit StagingBelt(VkDevice device, VmaAllocator vma, size_t capacityBytes);
        ~StagingBelt();

        StagingBelt(const StagingBelt&)            = delete;
        StagingBelt& operator=(const StagingBelt&) = delete;

        [[nodiscard]] Allocation Allocate(size_t sizeBytes, size_t alignment);
        void Retire(uint64_t retireValue);
        void GarbageCollect(uint64_t completedValue);

        [[nodiscard]] size_t Capacity() const noexcept { return m_Capacity; }

    private:
        static size_t AlignUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

        struct Region { size_t Begin, End; uint64_t RetireValue; };

        VkDevice      m_Device  = VK_NULL_HANDLE;
        VmaAllocator  m_Vma     = VK_NULL_HANDLE;
        VkBuffer      m_Buffer  = VK_NULL_HANDLE;
        VmaAllocation m_Alloc   = VK_NULL_HANDLE;
        void*         m_Mapped  = nullptr;
        size_t        m_Capacity;
        size_t        m_Head    = 0;
        size_t        m_Tail    = 0;
        size_t        m_PendingBegin = 0;
        size_t        m_PendingEnd   = 0;
        bool          m_HasPending   = false;
        std::deque<Region> m_InFlight;
        std::mutex    m_Mutex;
    };

    export class VulkanTransferQueue final : public RHI::ITransferQueue
    {
    public:
        struct Config
        {
            VkDevice     Device      = VK_NULL_HANDLE;
            VmaAllocator Vma         = VK_NULL_HANDLE;
            VkQueue      Queue       = VK_NULL_HANDLE;
            uint32_t     QueueFamily = 0;
            size_t       StagingCapacity = kStagingBeltCapacity;
        };

        explicit VulkanTransferQueue(const Config& cfg);
        ~VulkanTransferQueue() override;

        [[nodiscard]] uint64_t QueryCompletedValue() const;

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle dst,
                                                       const void* data,
                                                       uint64_t size,
                                                       uint64_t offset) override;
        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle dst,
                                                       std::span<const std::byte> src,
                                                       uint64_t offset) override;
        [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle dst,
                                                        const void* data,
                                                        uint64_t dataSizeBytes,
                                                        uint32_t mipLevel,
                                                        uint32_t arrayLayer) override;
        [[nodiscard]] bool IsComplete(RHI::TransferToken token) const override;
        void CollectCompleted() override;

        friend class VulkanDevice;

    private:
        [[nodiscard]] VkCommandBuffer Begin();
        [[nodiscard]] RHI::TransferToken Submit(VkCommandBuffer cmd);

        VkDevice      m_Device        = VK_NULL_HANDLE;
        VmaAllocator  m_Vma           = VK_NULL_HANDLE;
        VkQueue       m_Queue         = VK_NULL_HANDLE;
        uint32_t      m_QueueFamily   = 0;
        VkSemaphore   m_Timeline      = VK_NULL_HANDLE;
        VkCommandPool m_CmdPool       = VK_NULL_HANDLE;
        std::atomic<uint64_t> m_NextTicket{1};
        std::unique_ptr<StagingBelt> m_Belt;
        mutable std::mutex m_Mutex;

        Core::ResourcePool<VulkanBuffer, RHI::BufferHandle,   kMaxFramesInFlight>* m_Buffers = nullptr;
        Core::ResourcePool<VulkanImage,  RHI::TextureHandle,  kMaxFramesInFlight>* m_Images  = nullptr;
    };
}

