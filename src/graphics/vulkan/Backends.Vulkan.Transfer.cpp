module;

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import :Transfer;
import Extrinsic.Core.Logging;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.TextureUpload;

namespace Extrinsic::Backends::Vulkan
{

// =============================================================================
// §8  VulkanTransferQueue
// =============================================================================

VulkanTransferQueue::VulkanTransferQueue(const Config& cfg)
    : m_Device(cfg.Device), m_Vma(cfg.Vma), m_Queue(cfg.Queue),
      m_QueueFamily(cfg.QueueFamily)
{
    if (m_Device == VK_NULL_HANDLE || m_Vma == VK_NULL_HANDLE || m_Queue == VK_NULL_HANDLE)
    {
        Core::Log::Error("[VulkanTransferQueue] Cannot create transfer queue without valid device/VMA/queue handles");
        return;
    }

    // Timeline semaphore.
    VkSemaphoreTypeCreateInfo typeCI{};
    typeCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeCI.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeCI.initialValue  = 0;
    VkSemaphoreCreateInfo semCI{};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semCI.pNext = &typeCI;
    VkResult result = vkCreateSemaphore(m_Device, &semCI, nullptr, &m_Timeline);
    if (result != VK_SUCCESS || m_Timeline == VK_NULL_HANDLE)
    {
        Core::Log::Error("[VulkanTransferQueue] vkCreateSemaphore failed; transfer service unavailable");
        m_Timeline = VK_NULL_HANDLE;
        return;
    }

    // Command pool for transfer commands.
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = m_QueueFamily;
    poolCI.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
                            | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    result = vkCreateCommandPool(m_Device, &poolCI, nullptr, &m_CmdPool);
    if (result != VK_SUCCESS || m_CmdPool == VK_NULL_HANDLE)
    {
        Core::Log::Error("[VulkanTransferQueue] vkCreateCommandPool failed; transfer service unavailable");
        vkDestroySemaphore(m_Device, m_Timeline, nullptr);
        m_Timeline = VK_NULL_HANDLE;
        m_CmdPool = VK_NULL_HANDLE;
        return;
    }

    m_Belt = std::make_unique<StagingBelt>(m_Device, m_Vma, cfg.StagingCapacity);
    if (!m_Belt || !m_Belt->IsValid())
    {
        Core::Log::Error("[VulkanTransferQueue] Staging belt creation failed; transfer service unavailable");
        m_Belt.reset();
        vkDestroyCommandPool(m_Device, m_CmdPool, nullptr);
        vkDestroySemaphore(m_Device, m_Timeline, nullptr);
        m_CmdPool = VK_NULL_HANDLE;
        m_Timeline = VK_NULL_HANDLE;
    }
}

VulkanTransferQueue::~VulkanTransferQueue()
{
    if (m_Queue != VK_NULL_HANDLE)
        vkQueueWaitIdle(m_Queue);
    RetireCompletedCommandBuffers(std::numeric_limits<std::uint64_t>::max());
    if (m_CmdPool   != VK_NULL_HANDLE) vkDestroyCommandPool(m_Device, m_CmdPool, nullptr);
    if (m_Timeline  != VK_NULL_HANDLE) vkDestroySemaphore(m_Device, m_Timeline, nullptr);
}

bool VulkanTransferQueue::IsValid() const noexcept
{
    return m_Device != VK_NULL_HANDLE && m_Vma != VK_NULL_HANDLE && m_Queue != VK_NULL_HANDLE &&
           m_Timeline != VK_NULL_HANDLE && m_CmdPool != VK_NULL_HANDLE && m_Belt && m_Belt->IsValid();
}

uint64_t VulkanTransferQueue::QueryCompletedValue() const
{
    if (!IsValid())
    {
        Core::Log::Warn("[VulkanTransferQueue] QueryCompletedValue on invalid transfer service; returning zero");
        return 0;
    }

    uint64_t val = 0;
    const VkResult result = vkGetSemaphoreCounterValue(m_Device, m_Timeline, &val);
    if (result != VK_SUCCESS)
    {
        Core::Log::Warn("[VulkanTransferQueue] vkGetSemaphoreCounterValue failed; returning zero");
        return 0;
    }
    return val;
}

VkCommandBuffer VulkanTransferQueue::Begin()
{
    if (!IsValid())
    {
        Core::Log::Warn("[VulkanTransferQueue] Cannot begin transfer command buffer; service is invalid");
        return VK_NULL_HANDLE;
    }

    VkCommandBufferAllocateInfo allocCI{};
    allocCI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocCI.commandPool        = m_CmdPool;
    allocCI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocCI.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkResult result = vkAllocateCommandBuffers(m_Device, &allocCI, &cmd);
    if (result != VK_SUCCESS || cmd == VK_NULL_HANDLE)
    {
        Core::Log::Error("[VulkanTransferQueue] vkAllocateCommandBuffers failed; upload skipped");
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginCI{};
    beginCI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginCI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(cmd, &beginCI);
    if (result != VK_SUCCESS)
    {
        Core::Log::Error("[VulkanTransferQueue] vkBeginCommandBuffer failed; upload skipped");
        vkFreeCommandBuffers(m_Device, m_CmdPool, 1, &cmd);
        return VK_NULL_HANDLE;
    }
    return cmd;
}

RHI::TransferToken VulkanTransferQueue::Submit(VkCommandBuffer cmd)
{
    if (!IsValid() || cmd == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] Cannot submit transfer command buffer; service or command buffer is invalid");
        if (cmd != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE && m_CmdPool != VK_NULL_HANDLE)
            vkFreeCommandBuffers(m_Device, m_CmdPool, 1u, &cmd);
        return {};
    }

    VkResult result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS)
    {
        Core::Log::Error("[VulkanTransferQueue] vkEndCommandBuffer failed; upload skipped");
        vkFreeCommandBuffers(m_Device, m_CmdPool, 1, &cmd);
        return {};
    }

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
        result = vkQueueSubmit2(m_Queue, 1, &submit, VK_NULL_HANDLE);
        if (result != VK_SUCCESS)
        {
            Core::Log::Error("[VulkanTransferQueue] vkQueueSubmit2 failed; upload skipped");
            vkFreeCommandBuffers(m_Device, m_CmdPool, 1, &cmd);
            return {};
        }
        m_InFlightCommandBuffers.push_back(RetiredCommandBuffer{.CommandBuffer = cmd,
                                                                .RetireValue = ticket});
        m_Belt->Retire(ticket);
    }

    return RHI::TransferToken{ticket};
}

void VulkanTransferQueue::RetireCompletedCommandBuffers(const uint64_t completedValue)
{
    if (m_Device == VK_NULL_HANDLE || m_CmdPool == VK_NULL_HANDLE)
        return;

    std::scoped_lock lock{m_Mutex};
    while (!m_InFlightCommandBuffers.empty() &&
           m_InFlightCommandBuffers.front().RetireValue <= completedValue)
    {
        const VkCommandBuffer cmd = m_InFlightCommandBuffers.front().CommandBuffer;
        if (cmd != VK_NULL_HANDLE)
            vkFreeCommandBuffers(m_Device, m_CmdPool, 1u, &cmd);
        m_InFlightCommandBuffers.pop_front();
    }
}

RHI::TransferToken VulkanTransferQueue::UploadBuffer(RHI::BufferHandle dst,
                                                      const void* data,
                                                      uint64_t size,
                                                      uint64_t offset)
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::UploadBufferRaw", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::UploadBufferRaw")};
    if (!IsValid())
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadBuffer rejected; transfer service is invalid");
        return {};
    }
    if (!m_Buffers)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadBuffer rejected; buffer pool is unavailable");
        return {};
    }
    if (data == nullptr || size == 0)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadBuffer rejected; source data is empty");
        return {};
    }

    auto* buf = m_Buffers->GetIfValid(dst);
    if (!buf || buf->Buffer == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadBuffer rejected; destination buffer handle is invalid");
        return {};
    }
    if (offset > buf->SizeBytes || size > buf->SizeBytes - offset)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadBuffer rejected; upload range exceeds destination buffer");
        return {};
    }

    auto staging = m_Belt->Allocate(static_cast<size_t>(size), 4);
    if (!staging.MappedPtr || staging.Buffer == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadBuffer rejected; staging allocation failed");
        return {};
    }
    std::memcpy(staging.MappedPtr, data, size);

    VkCommandBuffer cmd = Begin();
    if (cmd == VK_NULL_HANDLE)
        return {};
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
    if (!IsValid())
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; transfer service is invalid");
        return {};
    }
    if (!m_Images)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; image pool is unavailable");
        return {};
    }
    if (data == nullptr || dataSizeBytes == 0)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; source data is empty");
        return {};
    }

    auto* img = m_Images->GetIfValid(dst);
    if (!img || img->Image == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; destination texture handle is invalid");
        return {};
    }
    if (mipLevel >= img->MipLevels || arrayLayer >= img->ArrayLayers)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; mip level or array layer is out of range");
        return {};
    }
    if ((img->Usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; destination image lacks transfer-dst usage");
        return {};
    }

    auto staging = m_Belt->Allocate(static_cast<size_t>(dataSizeBytes), 4);
    if (!staging.MappedPtr || staging.Buffer == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTexture rejected; staging allocation failed");
        return {};
    }
    std::memcpy(staging.MappedPtr, data, dataSizeBytes);

    VkCommandBuffer cmd = Begin();
    if (cmd == VK_NULL_HANDLE)
        return {};

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

RHI::TransferToken VulkanTransferQueue::UploadTextureFullChain(RHI::TextureHandle dst,
                                                               std::span<const std::byte> src)
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::UploadTextureFullChain", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::UploadTextureFullChain")};
    if (!IsValid())
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; transfer service is invalid");
        return {};
    }
    if (!m_Images)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; image pool is unavailable");
        return {};
    }
    if (src.empty())
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; source data is empty");
        return {};
    }

    auto* img = m_Images->GetIfValid(dst);
    if (!img || img->Image == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; destination texture handle is invalid");
        return {};
    }
    if ((img->Usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; destination image lacks transfer-dst usage");
        return {};
    }
    if ((img->Usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; destination image lacks sampled usage");
        return {};
    }
    const bool supported2DArray = img->Dimension == RHI::TextureDimension::Tex2D && img->Depth == 1u;
    const bool supportedCube = img->Dimension == RHI::TextureDimension::TexCube &&
                               img->Depth == 1u &&
                               img->ArrayLayers == 6u;
    if (!supported2DArray && !supportedCube)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; only 2D color texture arrays and six-face cubemaps are supported by this slice");
        return {};
    }

    RHI::TextureDesc desc{};
    desc.Width = img->Width;
    desc.Height = img->Height;
    desc.DepthOrArrayLayers = img->ArrayLayers;
    desc.MipLevels = img->MipLevels;
    desc.Fmt = img->RhiFormat;
    desc.Dimension = img->Dimension;
    desc.Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;

    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    if (!layoutOr.has_value())
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; upload layout computation failed with error={}",
                        static_cast<int>(layoutOr.error()));
        return {};
    }
    const RHI::TextureUploadLayout& layout = *layoutOr;
    if (layout.TotalBytes != static_cast<std::uint64_t>(src.size_bytes()))
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; upload size mismatch: expected={} actual={}",
                        layout.TotalBytes,
                        static_cast<std::uint64_t>(src.size_bytes()));
        return {};
    }
    if (layout.TotalBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        layout.Subresources.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; upload layout is too large for this host/backend");
        return {};
    }

    const std::uint32_t offsetAlignment = RHI::RequiredBufferOffsetAlignment(desc.Fmt);
    auto staging = m_Belt->Allocate(static_cast<std::size_t>(layout.TotalBytes), offsetAlignment);
    if (!staging.MappedPtr || staging.Buffer == VK_NULL_HANDLE)
    {
        Core::Log::Warn("[VulkanTransferQueue] UploadTextureFullChain rejected; staging allocation failed");
        return {};
    }

    auto* stagingBytes = static_cast<std::byte*>(staging.MappedPtr);
    for (const RHI::TextureUploadSubresource& sub : layout.Subresources)
    {
        std::memcpy(stagingBytes + static_cast<std::size_t>(sub.OffsetBytes),
                    src.data() + static_cast<std::size_t>(sub.OffsetBytes),
                    static_cast<std::size_t>(sub.SizeBytes));
    }

    std::vector<VkBufferImageCopy> regions;
    regions.reserve(layout.Subresources.size());
    const VkImageAspectFlags aspectMask = AspectFromFormat(img->Format);
    for (const RHI::TextureUploadSubresource& sub : layout.Subresources)
    {
        VkBufferImageCopy region{};
        region.bufferOffset = staging.Offset + sub.OffsetBytes;
        region.imageSubresource = VkImageSubresourceLayers{.aspectMask = aspectMask,
                                                           .mipLevel = sub.MipLevel,
                                                           .baseArrayLayer = sub.ArrayLayer,
                                                           .layerCount = 1u};
        region.imageExtent = VkExtent3D{.width = sub.Width,
                                        .height = sub.Height,
                                        .depth = sub.Depth};
        regions.push_back(region);
    }

    VkCommandBuffer cmd = Begin();
    if (cmd == VK_NULL_HANDLE)
        return {};

    VkImageMemoryBarrier2 toXfer{};
    toXfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toXfer.srcStageMask = img->CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED
                              ? VK_PIPELINE_STAGE_2_NONE
                              : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    toXfer.srcAccessMask = img->CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED
                               ? 0
                               : VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    toXfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toXfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toXfer.oldLayout = img->CurrentLayout;
    toXfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toXfer.image = img->Image;
    toXfer.subresourceRange = VkImageSubresourceRange{.aspectMask = aspectMask,
                                                      .baseMipLevel = 0u,
                                                      .levelCount = img->MipLevels,
                                                      .baseArrayLayer = 0u,
                                                      .layerCount = img->ArrayLayers};
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1u;
    dep.pImageMemoryBarriers = &toXfer;
    vkCmdPipelineBarrier2(cmd, &dep);

    vkCmdCopyBufferToImage(cmd,
                           staging.Buffer,
                           img->Image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<std::uint32_t>(regions.size()),
                           regions.data());

    VkImageMemoryBarrier2 toRead = toXfer;
    toRead.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    toRead.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dep.pImageMemoryBarriers = &toRead;
    vkCmdPipelineBarrier2(cmd, &dep);

    const RHI::TransferToken token = Submit(cmd);
    if (token.IsValid())
        img->CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return token;
}

bool VulkanTransferQueue::IsComplete(RHI::TransferToken token) const
{
    if (!token.IsValid())
        return true;
    if (!IsValid())
        return false;
    return token.Value <= QueryCompletedValue();
}

void VulkanTransferQueue::CollectCompleted()
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanTransferQueue::CollectCompleted", Extrinsic::Core::Telemetry::HashString("VulkanTransferQueue::CollectCompleted")};
    if (!IsValid())
    {
        Core::Log::Warn("[VulkanTransferQueue] CollectCompleted skipped; transfer service is invalid");
        return;
    }

    const uint64_t done = QueryCompletedValue();
    RetireCompletedCommandBuffers(done);
    m_Belt->GarbageCollect(done);
}

} // namespace Extrinsic::Backends::Vulkan

