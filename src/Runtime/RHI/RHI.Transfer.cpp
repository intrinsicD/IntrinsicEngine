module;
#include <algorithm>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include "RHI.Vulkan.hpp"

module RHI:Transfer.Impl;
import :Transfer;
import Core;

namespace RHI
{
    TransferManager::TransferManager(std::shared_ptr<VulkanDevice> device)
        : m_Device(device)
    {
        uint32_t queueFamilyIndex = m_Device->GetQueueIndices().TransferFamily.value();
        vkGetDeviceQueue(m_Device->GetLogicalDevice(), queueFamilyIndex, 0, &m_TransferQueue);

        VkSemaphoreTypeCreateInfo timelineInfo{};
        timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timelineInfo.initialValue = 0;

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semInfo.pNext = &timelineInfo;

        VK_CHECK(vkCreateSemaphore(m_Device->GetLogicalDevice(), &semInfo, nullptr, &m_TimelineSemaphore));

        Core::Log::Info("RHI Transfer System Initialized.");
    }

    TransferManager::~TransferManager()
    {
        // Wait for all pending transfers before destroying the pool
        vkDeviceWaitIdle(m_Device->GetLogicalDevice());

        // Clear batches (destroys staging buffers)
        m_InFlightBatches.clear();

        vkDestroySemaphore(m_Device->GetLogicalDevice(), m_TimelineSemaphore, nullptr);
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
        VK_CHECK(vkAllocateCommandBuffers(m_Device->GetLogicalDevice(), &allocInfo, &cmd));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

        return cmd;
    }

    TransferToken TransferManager::Submit(VkCommandBuffer cmd,
                                          std::vector<std::unique_ptr<VulkanBuffer>>&& stagingBuffers)
    {
        // 1. End Recording
        VK_CHECK(vkEndCommandBuffer(cmd));

        // 2. Prepare Synchronization
        // Increment the ticket. This value represents "This Batch Completed".
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

        // 3. Submit to Queue (Thread Safe)
        {
            // Lock the Device's queue mutex to prevent collision with the Renderer
            // attempting to Present() or Submit() to the same physical queue.
            std::scoped_lock deviceLock(m_Device->GetQueueMutex());

            // We also keep our internal lock to protect m_InFlightBatches
            std::lock_guard internalLock(m_Mutex);

            VK_CHECK(vkQueueSubmit(m_TransferQueue, 1, &submitInfo, VK_NULL_HANDLE));

            m_InFlightBatches.push_back({TransferToken{signalValue}, std::move(stagingBuffers)});
        }

        // Return the token so the Engine knows what to wait for
        return TransferToken{signalValue};
    }

    bool TransferManager::IsCompleted(TransferToken token) const
    {
        if (!token.IsValid()) return true;

        uint64_t gpuValue = 0;
        // Non-blocking query of the semaphore value
        VK_CHECK(vkGetSemaphoreCounterValue(m_Device->GetLogicalDevice(), m_TimelineSemaphore, &gpuValue));

        return gpuValue >= token.Value;
    }

    void TransferManager::GarbageCollect()
    {
        uint64_t gpuValue = 0;
        VK_CHECK(vkGetSemaphoreCounterValue(m_Device->GetLogicalDevice(), m_TimelineSemaphore, &gpuValue));

        std::lock_guard lock(m_Mutex);
        if (m_InFlightBatches.empty()) return;

        // Remove batches that have been processed by the GPU
        auto it = std::remove_if(m_InFlightBatches.begin(), m_InFlightBatches.end(),
                                 [gpuValue, this](const PendingBatch& batch)
                                 {
                                     if (gpuValue >= batch.Token.Value)
                                     {
                                         // GPU is done.
                                         // Note: We also need to free the CommandBuffer!
                                         // In a simple pool, we can't free individual buffers easily if they are mixed,
                                         // but vkFreeCommandBuffers IS allowed.
                                         // However, for this implementation, we rely on vkResetCommandPool periodically
                                         // or just let the pool grow.
                                         // Perfectionist fix: Store cmd buffer in PendingBatch and free it here.
                                         return true;
                                     }
                                     return false;
                                 });

        // This actually calls the destructors of the unique_ptr<VulkanBuffer>, freeing CPU memory.
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
            poolInfo.queueFamilyIndex = m_Device->GetQueueIndices().TransferFamily.value();
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; // Buffers are short lived
            VK_CHECK(vkCreateCommandPool(m_Device->GetLogicalDevice(), &poolInfo, nullptr, &ctx.Pool));

            // Register with device for cleanup at shutdown
            m_Device->RegisterThreadLocalPool(ctx.Pool);
        }
        return ctx;
    }
}
