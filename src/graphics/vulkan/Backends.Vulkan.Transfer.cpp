module;

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import :Transfer;

namespace Extrinsic::Backends::Vulkan
{

// =============================================================================
// §8  VulkanTransferQueue
// =============================================================================

VulkanTransferQueue::VulkanTransferQueue(const Config& cfg)
    : m_Device(cfg.Device), m_Vma(cfg.Vma), m_Queue(cfg.Queue),
      m_QueueFamily(cfg.QueueFamily)
{
    // Timeline semaphore.
    VkSemaphoreTypeCreateInfo typeCI{};
    typeCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeCI.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeCI.initialValue  = 0;
    VkSemaphoreCreateInfo semCI{};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semCI.pNext = &typeCI;
    VK_CHECK_FATAL(vkCreateSemaphore(m_Device, &semCI, nullptr, &m_Timeline));

    // Command pool for transfer commands.
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = m_QueueFamily;
    poolCI.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
                            | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK_FATAL(vkCreateCommandPool(m_Device, &poolCI, nullptr, &m_CmdPool));

    m_Belt = std::make_unique<StagingBelt>(m_Device, m_Vma, cfg.StagingCapacity);
}

VulkanTransferQueue::~VulkanTransferQueue()
{
    if (m_CmdPool   != VK_NULL_HANDLE) vkDestroyCommandPool(m_Device, m_CmdPool, nullptr);
    if (m_Timeline  != VK_NULL_HANDLE) vkDestroySemaphore(m_Device, m_Timeline, nullptr);
}

uint64_t VulkanTransferQueue::QueryCompletedValue() const
{
    uint64_t val = 0;
    VK_CHECK_WARN(vkGetSemaphoreCounterValue(m_Device, m_Timeline, &val));
    return val;
}

VkCommandBuffer VulkanTransferQueue::Begin()
{
    VkCommandBufferAllocateInfo allocCI{};
    allocCI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocCI.commandPool        = m_CmdPool;
    allocCI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocCI.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK_FATAL(vkAllocateCommandBuffers(m_Device, &allocCI, &cmd));
    VkCommandBufferBeginInfo beginCI{};
    beginCI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginCI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK_FATAL(vkBeginCommandBuffer(cmd, &beginCI));
    return cmd;
}

RHI::TransferToken VulkanTransferQueue::Submit(VkCommandBuffer cmd)
{
    VK_CHECK_FATAL(vkEndCommandBuffer(cmd));
    const uint64_t ticket = m_NextTicket.fetch_add(1, std::memory_order_relaxed);

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;
    VkSemaphoreSubmitInfo sigInfo{};
    sigInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    sigInfo.semaphore = m_Timeline;
    sigInfo.value     = ticket;
    sigInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount   = 1;
    submit.pCommandBufferInfos      = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos    = &sigInfo;

    {
        std::scoped_lock lock{m_Mutex};
        VK_CHECK_FATAL(vkQueueSubmit2(m_Queue, 1, &submit, VK_NULL_HANDLE));
        m_Belt->Retire(ticket);
    }

    vkFreeCommandBuffers(m_Device, m_CmdPool, 1, &cmd);
    return RHI::TransferToken{ticket};
}

RHI::TransferToken VulkanTransferQueue::UploadBuffer(RHI::BufferHandle dst,
                                                      const void* data,
                                                      uint64_t size,
                                                      uint64_t offset)
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::UploadBufferRaw", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::UploadBufferRaw")};
    if (!m_Buffers) return {};
    auto* buf = m_Buffers->GetIfValid(dst);
    if (!buf) return {};

    auto staging = m_Belt->Allocate(static_cast<size_t>(size), 4);
    if (!staging.MappedPtr) return {};
    std::memcpy(staging.MappedPtr, data, size);

    VkCommandBuffer cmd = Begin();
    VkBufferCopy region{staging.Offset, offset, size};
    vkCmdCopyBuffer(cmd, staging.Buffer, buf->Buffer, 1, &region);
    return Submit(cmd);
}

RHI::TransferToken VulkanTransferQueue::UploadBuffer(RHI::BufferHandle dst,
                                                      std::span<const std::byte> src,
                                                      uint64_t offset)
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::UploadBufferSpan", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::UploadBufferSpan")};
    return UploadBuffer(dst, src.data(), static_cast<uint64_t>(src.size_bytes()), offset);
}

RHI::TransferToken VulkanTransferQueue::UploadTexture(RHI::TextureHandle dst,
                                                       const void* data,
                                                       uint64_t dataSizeBytes,
                                                       uint32_t mipLevel,
                                                       uint32_t arrayLayer)
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::UploadTexture", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::UploadTexture")};
    if (!m_Images) return {};
    auto* img = m_Images->GetIfValid(dst);
    if (!img) return {};

    auto staging = m_Belt->Allocate(static_cast<size_t>(dataSizeBytes), 4);
    if (!staging.MappedPtr) return {};
    std::memcpy(staging.MappedPtr, data, dataSizeBytes);

    VkCommandBuffer cmd = Begin();

    // Transition: Undefined → TransferDst
    VkImageMemoryBarrier2 toXfer{};
    toXfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toXfer.srcStageMask        = VK_PIPELINE_STAGE_2_NONE;
    toXfer.srcAccessMask       = 0;
    toXfer.dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toXfer.dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toXfer.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    toXfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toXfer.image               = img->Image;
    toXfer.subresourceRange    = {AspectFromFormat(img->Format), mipLevel, 1, arrayLayer, 1};
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &toXfer;
    vkCmdPipelineBarrier2(cmd, &dep);

    const uint32_t mipW = std::max(1u, img->Width  >> mipLevel);
    const uint32_t mipH = std::max(1u, img->Height >> mipLevel);
    VkBufferImageCopy region{};
    region.bufferOffset      = staging.Offset;
    region.imageSubresource  = {AspectFromFormat(img->Format), mipLevel, arrayLayer, 1};
    region.imageExtent       = {mipW, mipH, 1};
    vkCmdCopyBufferToImage(cmd, staging.Buffer, img->Image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition: TransferDst → ShaderReadOnly
    VkImageMemoryBarrier2 toRead = toXfer;
    toRead.srcStageMask   = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toRead.srcAccessMask  = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toRead.dstStageMask   = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toRead.dstAccessMask  = VK_ACCESS_2_SHADER_READ_BIT;
    toRead.oldLayout      = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout      = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dep.pImageMemoryBarriers = &toRead;
    vkCmdPipelineBarrier2(cmd, &dep);

    return Submit(cmd);
}

bool VulkanTransferQueue::IsComplete(RHI::TransferToken token) const
{
    return token.Value <= QueryCompletedValue();
}

void VulkanTransferQueue::CollectCompleted()
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::CollectCompleted", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::CollectCompleted")};
    const uint64_t done = QueryCompletedValue();
    m_Belt->GarbageCollect(done);
}

} // namespace Extrinsic::Backends::Vulkan

