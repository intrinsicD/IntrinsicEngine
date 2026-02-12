module;
#include <algorithm>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include <cstring>
#include <span>
#include <cstddef>
#include "RHI.Vulkan.hpp"

module RHI:Transfer.Impl;
import :Transfer;
import :Device;
import :Buffer;
import :StagingBelt;
import Core;

namespace RHI
{
    TransferManager::TransferManager(VulkanDevice& device)
        : m_Device(device)
    {
        uint32_t queueFamilyIndex = m_Device.GetQueueIndices().TransferFamily.value();
        vkGetDeviceQueue(m_Device.GetLogicalDevice(), queueFamilyIndex, 0, &m_TransferQueue);

        VkSemaphoreTypeCreateInfo timelineInfo{};
        timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timelineInfo.initialValue = 0;

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semInfo.pNext = &timelineInfo;

        VK_CHECK(vkCreateSemaphore(m_Device.GetLogicalDevice(), &semInfo, nullptr, &m_TimelineSemaphore));

        // Default staging belt size: large enough for typical level-load bursts.
        // If this turns out too small, we can grow or add a slow-path.
        constexpr size_t defaultBeltSize = 64ull * 1024ull * 1024ull; // 64 MiB
        m_StagingBelt = std::make_unique<StagingBelt>(m_Device, defaultBeltSize);

        Core::Log::Info("RHI Transfer System Initialized.");
    }

    TransferManager::~TransferManager()
    {
        // Wait for all pending transfers before destroying the pool
        vkDeviceWaitIdle(m_Device.GetLogicalDevice());

        // Clear batches (destroys staging buffers)
        m_InFlightBatches.clear();

        // IMPORTANT:
        // VulkanBuffer destruction is deferred via VulkanDevice::SafeDestroy()/SafeDestroyAfter(), which enqueues
        // vmaDestroyBuffer/vmaDestroyImage on a timeline-based deletion queue.
        // If we don't flush *both* the frame-slot queue and the timeline queue here, the VMA allocator may be
        // destroyed later with live allocations, triggering:
        //   "Some allocations were not freed before destruction of this memory block!"
        m_Device.FlushAllDeletionQueues();
        m_Device.FlushTimelineDeletionQueueNow();
        m_Device.FlushAllDeletionQueues();

        m_StagingBelt.reset();

        vkDestroySemaphore(m_Device.GetLogicalDevice(), m_TimelineSemaphore, nullptr);
    }

    VkCommandBuffer TransferManager::Begin()
    {
        auto& ctx = GetThreadContext();

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        allocInfo.commandPool = ctx.Pool;

        VkCommandBuffer cmd;
        VK_CHECK(vkAllocateCommandBuffers(m_Device.GetLogicalDevice(), &allocInfo, &cmd));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

        return cmd;
    }

    StagingBelt::Allocation TransferManager::AllocateStaging(size_t sizeBytes, size_t alignment)
    {
        return m_StagingBelt ? m_StagingBelt->Allocate(sizeBytes, alignment) : StagingBelt::Allocation{};
    }

    TransferToken TransferManager::Submit(VkCommandBuffer cmd,
                                          std::vector<std::unique_ptr<VulkanBuffer>>&& stagingBuffers)
    {
        // 1. End Recording
        VK_CHECK(vkEndCommandBuffer(cmd));

        // 2. Prepare Synchronization
        uint64_t signalValue = m_NextTicket.fetch_add(1);

        VkTimelineSemaphoreSubmitInfo timelineSubmit{};
        timelineSubmit.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timelineSubmit.signalSemaphoreValueCount = 1;
        timelineSubmit.pSignalSemaphoreValues = &signalValue;

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = &timelineSubmit;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &m_TimelineSemaphore;

        {
            std::scoped_lock deviceLock(m_Device.GetQueueMutex());
            std::lock_guard internalLock(m_Mutex);

            VK_CHECK(vkQueueSubmit(m_TransferQueue, 1, &submitInfo, VK_NULL_HANDLE));
            m_InFlightBatches.push_back({TransferToken{signalValue}, std::move(stagingBuffers)});

            if (m_StagingBelt)
                m_StagingBelt->Retire(signalValue);
        }

        return TransferToken{signalValue};
    }

    TransferToken TransferManager::Submit(VkCommandBuffer cmd)
    {
        std::vector<std::unique_ptr<VulkanBuffer>> none;
        return Submit(cmd, std::move(none));
    }

    bool TransferManager::IsCompleted(TransferToken token) const
    {
        if (!token.IsValid()) return true;

        uint64_t gpuValue = 0;
        VK_CHECK(vkGetSemaphoreCounterValue(m_Device.GetLogicalDevice(), m_TimelineSemaphore, &gpuValue));

        return gpuValue >= token.Value;
    }

    void TransferManager::GarbageCollect()
    {
        uint64_t gpuValue = 0;
        VK_CHECK(vkGetSemaphoreCounterValue(m_Device.GetLogicalDevice(), m_TimelineSemaphore, &gpuValue));

        std::lock_guard lock(m_Mutex);

        if (m_StagingBelt)
            m_StagingBelt->GarbageCollect(gpuValue);

        if (m_InFlightBatches.empty()) return;

        auto it = std::remove_if(m_InFlightBatches.begin(), m_InFlightBatches.end(),
                                 [gpuValue](const PendingBatch& batch)
                                 {
                                     return gpuValue >= batch.Token.Value;
                                 });

        m_InFlightBatches.erase(it, m_InFlightBatches.end());
    }

    TransferManager::ThreadTransferContext& TransferManager::GetThreadContext()
    {
        thread_local ThreadTransferContext ctx;

        if (ctx.Owner != this)
        {
            ctx.Pool = VK_NULL_HANDLE;
            ctx.Owner = this;
        }

        if (ctx.Pool == VK_NULL_HANDLE)
        {
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = m_Device.GetQueueIndices().TransferFamily.value();
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            VK_CHECK(vkCreateCommandPool(m_Device.GetLogicalDevice(), &poolInfo, nullptr, &ctx.Pool));

            m_Device.RegisterThreadLocalPool(ctx.Pool);
        }
        return ctx;
    }

    StagingBelt::Allocation TransferManager::AllocateStagingForImage(size_t sizeBytes,
                                                                     size_t texelBlockSize,
                                                                     size_t rowPitchBytes,
                                                                     size_t optimalBufferCopyOffsetAlignment,
                                                                     size_t optimalBufferCopyRowPitchAlignment)
    {
        return m_StagingBelt
            ? m_StagingBelt->AllocateForImageUpload(sizeBytes, texelBlockSize, rowPitchBytes,
                                                   optimalBufferCopyOffsetAlignment,
                                                   optimalBufferCopyRowPitchAlignment)
            : StagingBelt::Allocation{};
    }

    TransferToken TransferManager::UploadBuffer(VkBuffer dst, std::span<const std::byte> src, VkDeviceSize dstOffset)
    {
        if (dst == VK_NULL_HANDLE) return {};
        if (src.empty()) return {};

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_Device.GetPhysicalDevice(), &props);

        // Vulkan requires srcOffset and dstOffset to be multiples of optimalBufferCopyOffsetAlignment for vkCmdCopyBuffer.
        const size_t copyAlign = std::max<size_t>(16, static_cast<size_t>(props.limits.optimalBufferCopyOffsetAlignment));

        VkCommandBuffer cmd = Begin();

        auto alloc = AllocateStaging(src.size_bytes(), copyAlign);
        if (alloc.Buffer == VK_NULL_HANDLE || alloc.MappedPtr == nullptr)
        {
            Core::Log::Error("TransferManager::UploadBuffer(): staging allocation failed (size={}, align={}).", src.size_bytes(), copyAlign);
            return {};
        }

        std::memcpy(alloc.MappedPtr, src.data(), src.size_bytes());

        VkBufferCopy region{};
        region.srcOffset = static_cast<VkDeviceSize>(alloc.Offset);
        region.dstOffset = dstOffset;
        region.size = static_cast<VkDeviceSize>(src.size_bytes());

        vkCmdCopyBuffer(cmd, alloc.Buffer, dst, 1, &region);

        return Submit(cmd);
    }

    VkCommandBuffer TransferManager::BeginUploadBatch()
    {
        return Begin();
    }

    VkCommandBuffer TransferManager::BeginUploadBatch(const UploadBatchConfig&)
    {
        // For now this is identical to Begin(); config is used by EnqueueUploadBuffer.
        return Begin();
    }

    bool TransferManager::EnqueueUploadBuffer(VkCommandBuffer cmd,
                                             VkBuffer dst,
                                             std::span<const std::byte> src,
                                             VkDeviceSize dstOffset,
                                             size_t copyAlignment)
    {
        if (cmd == VK_NULL_HANDLE) return false;
        if (dst == VK_NULL_HANDLE) return false;
        if (src.empty()) return true; // no-op success

        if (copyAlignment == 0)
        {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(m_Device.GetPhysicalDevice(), &props);
            copyAlignment = std::max<size_t>(16, static_cast<size_t>(props.limits.optimalBufferCopyOffsetAlignment));
        }

        auto alloc = AllocateStaging(src.size_bytes(), copyAlignment);
        if (alloc.Buffer == VK_NULL_HANDLE || alloc.MappedPtr == nullptr)
            return false;

        std::memcpy(alloc.MappedPtr, src.data(), src.size_bytes());

        VkBufferCopy region{};
        region.srcOffset = static_cast<VkDeviceSize>(alloc.Offset);
        region.dstOffset = dstOffset;
        region.size = static_cast<VkDeviceSize>(src.size_bytes());

        vkCmdCopyBuffer(cmd, alloc.Buffer, dst, 1, &region);
        return true;
    }

    TransferToken TransferManager::EndUploadBatch(VkCommandBuffer cmd)
    {
        return Submit(cmd);
    }
}
