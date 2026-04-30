module;

#include <algorithm>
#include <cstdio>
#include <vector>

#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import :CommandPools;

namespace Extrinsic::Backends::Vulkan
{

// =============================================================================
// §9  VulkanCommandContext
// =============================================================================

void VulkanCommandContext::Bind(VkDevice device, VkCommandBuffer cmd,
                                 VkPipelineLayout globalLayout,
                                 VkDescriptorSet  bindlessSet,
                                 const Core::ResourcePool<VulkanBuffer,   RHI::BufferHandle,   kMaxFramesInFlight>* buffers,
                                 const Core::ResourcePool<VulkanImage,    RHI::TextureHandle,  kMaxFramesInFlight>* images,
                                 const Core::ResourcePool<VulkanPipeline, RHI::PipelineHandle, kMaxFramesInFlight>* pipelines)
{
    m_Device        = device;
    m_Cmd           = cmd;
    m_GlobalLayout  = globalLayout;
    m_BindlessSet   = bindlessSet;
    m_Buffers       = buffers;
    m_Images        = images;
    m_Pipelines     = pipelines;
    m_BindPoint     = VK_PIPELINE_BIND_POINT_GRAPHICS;
}

void VulkanCommandContext::Begin()
{
    [[maybe_unused]] Core::Telemetry::ScopedTimer timer{"VulkanCommandContext::Begin", Extrinsic::Core::Telemetry::HashString("VulkanCommandContext::Begin")};
    VkCommandBufferBeginInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK_FATAL(vkBeginCommandBuffer(m_Cmd, &ci));
}

void VulkanCommandContext::End()
{
    [[maybe_unused]] Core::Telemetry::ScopedTimer timer{"VulkanCommandContext::End", Extrinsic::Core::Telemetry::HashString("VulkanCommandContext::End")};
    VK_CHECK_FATAL(vkEndCommandBuffer(m_Cmd));
}

void VulkanCommandContext::BeginRenderPass(const RHI::RenderPassDesc& desc)
{
    // Build color attachment infos.
    std::vector<VkRenderingAttachmentInfo> colorInfos;
    colorInfos.reserve(desc.ColorTargets.size());

    for (const auto& ca : desc.ColorTargets)
    {
        VkRenderingAttachmentInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        if (ca.Target.IsValid())
        {
            const auto* img = m_Images->GetIfValid(ca.Target);
            info.imageView   = img ? img->View : VK_NULL_HANDLE;
        }
        // Invalid handle = backbuffer; VulkanDevice::BeginFrame sets its view
        // into a special slot that TextureBarrier recognises.  When using dynamic
        // rendering the swapchain image view is looked up the same way.
        info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        info.loadOp      = ca.Load  == RHI::LoadOp::Clear    ? VK_ATTACHMENT_LOAD_OP_CLEAR
                         : ca.Load  == RHI::LoadOp::Load     ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                              : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        info.storeOp     = ca.Store == RHI::StoreOp::Store   ? VK_ATTACHMENT_STORE_OP_STORE
                                                              : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        info.clearValue.color = {{ca.ClearR, ca.ClearG, ca.ClearB, ca.ClearA}};
        colorInfos.push_back(info);
    }

    // Depth attachment.
    VkRenderingAttachmentInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    bool hasDepth = false;
    if (desc.Depth.Target.IsValid())
    {
        const auto* img = m_Images->GetIfValid(desc.Depth.Target);
        if (img)
        {
            depthInfo.imageView   = img->View;
            depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthInfo.loadOp      = desc.Depth.Load  == RHI::LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                  : desc.Depth.Load  == RHI::LoadOp::Load  ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                                            : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthInfo.storeOp     = desc.Depth.Store == RHI::StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE
                                                                             : VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthInfo.clearValue.depthStencil = {desc.Depth.ClearDepth, desc.Depth.ClearStencil};
            hasDepth = true;
        }
    }

    // Determine render area from the first color target (or depth).
    VkExtent2D extent{1, 1};
    if (!desc.ColorTargets.empty() && desc.ColorTargets[0].Target.IsValid())
    {
        const auto* img = m_Images->GetIfValid(desc.ColorTargets[0].Target);
        if (img) extent = {img->Width, img->Height};
    }

    VkRenderingInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea           = {{0, 0}, extent};
    ri.layerCount           = 1;
    ri.colorAttachmentCount = static_cast<uint32_t>(colorInfos.size());
    ri.pColorAttachments    = colorInfos.data();
    ri.pDepthAttachment     = hasDepth ? &depthInfo : nullptr;
    vkCmdBeginRendering(m_Cmd, &ri);
}

void VulkanCommandContext::EndRenderPass()
{
    vkCmdEndRendering(m_Cmd);
}

void VulkanCommandContext::SetViewport(float x, float y, float w, float h,
                                        float minD, float maxD)
{
    // Flip Y for Vulkan NDC (origin top-left, Y down).
    VkViewport vp{x, y + h, w, -h, minD, maxD};
    vkCmdSetViewport(m_Cmd, 0, 1, &vp);
}

void VulkanCommandContext::SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    VkRect2D sc{{x, y}, {w, h}};
    vkCmdSetScissor(m_Cmd, 0, 1, &sc);
}

void VulkanCommandContext::BindPipeline(RHI::PipelineHandle handle)
{
    const auto* pip = m_Pipelines->GetIfValid(handle);
    if (!pip) return;
    m_BindPoint = pip->BindPoint;
    vkCmdBindPipeline(m_Cmd, m_BindPoint, pip->Pipeline);
    // Always bind the global bindless descriptor set at set 0.
    vkCmdBindDescriptorSets(m_Cmd, m_BindPoint, m_GlobalLayout,
                            0, 1, &m_BindlessSet, 0, nullptr);
}

void VulkanCommandContext::PushConstants(const void* data, uint32_t size, uint32_t offset)
{
    vkCmdPushConstants(m_Cmd, m_GlobalLayout,
                       VK_SHADER_STAGE_ALL, offset, size, data);
}

void VulkanCommandContext::BindIndexBuffer(RHI::BufferHandle handle,
                                           uint64_t offset,
                                           RHI::IndexType indexType)
{
    const auto* buf = m_Buffers->GetIfValid(handle);
    if (!buf) return;
    vkCmdBindIndexBuffer(m_Cmd, buf->Buffer, offset, ToVkIndexType(indexType));
}

void VulkanCommandContext::Draw(uint32_t vertexCount, uint32_t instanceCount,
                                 uint32_t firstVertex, uint32_t firstInstance)
{
    vkCmdDraw(m_Cmd, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandContext::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                        uint32_t firstIndex, int32_t vertexOffset,
                                        uint32_t firstInstance)
{
    vkCmdDrawIndexed(m_Cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandContext::DrawIndirect(RHI::BufferHandle argBuf,
                                         uint64_t offset, uint32_t drawCount)
{
    const auto* buf = m_Buffers->GetIfValid(argBuf);
    if (!buf) return;
    vkCmdDrawIndirect(m_Cmd, buf->Buffer, offset, drawCount,
                      sizeof(VkDrawIndirectCommand));
}

void VulkanCommandContext::DrawIndexedIndirect(RHI::BufferHandle argBuf,
                                                uint64_t offset, uint32_t drawCount)
{
    const auto* buf = m_Buffers->GetIfValid(argBuf);
    if (!buf) return;
    vkCmdDrawIndexedIndirect(m_Cmd, buf->Buffer, offset, drawCount,
                             sizeof(VkDrawIndexedIndirectCommand));
}

void VulkanCommandContext::DrawIndexedIndirectCount(RHI::BufferHandle argBuf, uint64_t argOffset,
                                                     RHI::BufferHandle cntBuf, uint64_t cntOffset,
                                                     uint32_t maxDraw)
{
    const auto* abuf = m_Buffers->GetIfValid(argBuf);
    const auto* cbuf = m_Buffers->GetIfValid(cntBuf);
    if (!abuf || !cbuf) return;
    vkCmdDrawIndexedIndirectCount(m_Cmd, abuf->Buffer, argOffset,
                                  cbuf->Buffer, cntOffset, maxDraw,
                                  sizeof(VkDrawIndexedIndirectCommand));
}

void VulkanCommandContext::DrawIndirectCount(RHI::BufferHandle argBuf, uint64_t argOffset,
                                              RHI::BufferHandle cntBuf, uint64_t cntOffset,
                                              uint32_t maxDraw)
{
    const auto* abuf = m_Buffers->GetIfValid(argBuf);
    const auto* cbuf = m_Buffers->GetIfValid(cntBuf);
    if (!abuf || !cbuf) return;
    vkCmdDrawIndirectCount(m_Cmd, abuf->Buffer, argOffset,
                           cbuf->Buffer, cntOffset, maxDraw,
                           sizeof(VkDrawIndirectCommand));
}

void VulkanCommandContext::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz)
{
    vkCmdDispatch(m_Cmd, gx, gy, gz);
}

void VulkanCommandContext::DispatchIndirect(RHI::BufferHandle argBuf, uint64_t offset)
{
    const auto* buf = m_Buffers->GetIfValid(argBuf);
    if (!buf) return;
    vkCmdDispatchIndirect(m_Cmd, buf->Buffer, offset);
}

void VulkanCommandContext::TextureBarrier(RHI::TextureHandle tex,
                                           RHI::TextureLayout before,
                                           RHI::TextureLayout after)
{
    const auto* img = m_Images->GetIfValid(tex);
    if (!img) return;
    const VkImageLayout oldLayout = ToVkImageLayout(before);
    const VkImageLayout newLayout = ToVkImageLayout(after);

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.oldLayout     = oldLayout;
    barrier.newLayout     = newLayout;
    barrier.image         = img->Image;
    barrier.subresourceRange = {AspectFromFormat(img->Format), 0, img->MipLevels, 0, img->ArrayLayers};

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(m_Cmd, &dep);
}

void VulkanCommandContext::BufferBarrier(RHI::BufferHandle buf,
                                          RHI::MemoryAccess before,
                                          RHI::MemoryAccess after)
{
    const auto* b = m_Buffers->GetIfValid(buf);
    if (!b) return;

    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask  = ToVkStage(before);
    barrier.srcAccessMask = ToVkAccess(before);
    barrier.dstStageMask  = ToVkStage(after);
    barrier.dstAccessMask = ToVkAccess(after);
    barrier.buffer        = b->Buffer;
    barrier.offset        = 0;
    barrier.size          = VK_WHOLE_SIZE;

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(m_Cmd, &dep);
}

void VulkanCommandContext::SubmitBarriers(const RHI::BarrierBatchDesc& batch)
{
    std::vector<VkImageMemoryBarrier2> imageBarriers{};
    std::vector<VkBufferMemoryBarrier2> bufferBarriers{};
    std::vector<VkMemoryBarrier2> memoryBarriers{};

    imageBarriers.reserve(batch.TextureBarriers.size());
    bufferBarriers.reserve(batch.BufferBarriers.size());
    memoryBarriers.reserve(batch.MemoryBarriers.size());

    for (const RHI::TextureBarrierDesc& desc : batch.TextureBarriers)
    {
        const auto* image = m_Images->GetIfValid(desc.Texture);
        if (!image) continue;

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = ToVkStage(desc.BeforeAccess);
        barrier.srcAccessMask = ToVkAccess(desc.BeforeAccess);
        barrier.dstStageMask = ToVkStage(desc.AfterAccess);
        barrier.dstAccessMask = ToVkAccess(desc.AfterAccess);
        barrier.oldLayout = ToVkImageLayout(desc.BeforeLayout);
        barrier.newLayout = ToVkImageLayout(desc.AfterLayout);
        barrier.srcQueueFamilyIndex = desc.SrcQueueFamily;
        barrier.dstQueueFamilyIndex = desc.DstQueueFamily;
        barrier.image = image->Image;
        barrier.subresourceRange = {AspectFromFormat(image->Format), 0, image->MipLevels, 0, image->ArrayLayers};
        imageBarriers.push_back(barrier);
    }

    for (const RHI::BufferBarrierDesc& desc : batch.BufferBarriers)
    {
        const auto* buffer = m_Buffers->GetIfValid(desc.Buffer);
        if (!buffer) continue;

        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = ToVkStage(desc.BeforeAccess);
        barrier.srcAccessMask = ToVkAccess(desc.BeforeAccess);
        barrier.dstStageMask = ToVkStage(desc.AfterAccess);
        barrier.dstAccessMask = ToVkAccess(desc.AfterAccess);
        barrier.srcQueueFamilyIndex = desc.SrcQueueFamily;
        barrier.dstQueueFamilyIndex = desc.DstQueueFamily;
        barrier.buffer = buffer->Buffer;
        barrier.offset = desc.Offset;
        barrier.size = desc.Size;
        bufferBarriers.push_back(barrier);
    }

    for (const RHI::MemoryBarrierDesc& desc : batch.MemoryBarriers)
    {
        VkMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        barrier.srcStageMask = ToVkStage(desc.BeforeAccess);
        barrier.srcAccessMask = ToVkAccess(desc.BeforeAccess);
        barrier.dstStageMask = ToVkStage(desc.AfterAccess);
        barrier.dstAccessMask = ToVkAccess(desc.AfterAccess);
        memoryBarriers.push_back(barrier);
    }

    if (imageBarriers.empty() && bufferBarriers.empty() && memoryBarriers.empty())
    {
        return;
    }

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = static_cast<uint32_t>(memoryBarriers.size());
    dep.pMemoryBarriers = memoryBarriers.data();
    dep.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
    dep.pBufferMemoryBarriers = bufferBarriers.data();
    dep.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
    dep.pImageMemoryBarriers = imageBarriers.data();
    vkCmdPipelineBarrier2(m_Cmd, &dep);
}

void VulkanCommandContext::FillBuffer(RHI::BufferHandle handle,
                                    uint64_t offset,
                                    uint64_t size,
                                    uint32_t value)
{
    const auto* buf = m_Buffers->GetIfValid(handle);
    if (!buf) return;
    vkCmdFillBuffer(m_Cmd, buf->Buffer, offset, size, value);
}

void VulkanCommandContext::CopyBuffer(RHI::BufferHandle src, RHI::BufferHandle dst,
                                       uint64_t srcOff, uint64_t dstOff, uint64_t size)
{
    const auto* s = m_Buffers->GetIfValid(src);
    const auto* d = m_Buffers->GetIfValid(dst);
    if (!s || !d) return;
    VkBufferCopy region{srcOff, dstOff, size};
    vkCmdCopyBuffer(m_Cmd, s->Buffer, d->Buffer, 1, &region);
}

void VulkanCommandContext::CopyBufferToTexture(RHI::BufferHandle src, uint64_t srcOff,
                                                RHI::TextureHandle dst,
                                                uint32_t mipLevel, uint32_t arrayLayer)
{
    const auto* s = m_Buffers->GetIfValid(src);
    const auto* d = m_Images->GetIfValid(dst);
    if (!s || !d) return;
    VkBufferImageCopy region{};
    region.bufferOffset     = srcOff;
    region.imageSubresource = {AspectFromFormat(d->Format), mipLevel, arrayLayer, 1};
    region.imageExtent      = {std::max(1u, d->Width >> mipLevel),
                                std::max(1u, d->Height >> mipLevel), 1};
    vkCmdCopyBufferToImage(m_Cmd, s->Buffer, d->Image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

} // namespace Extrinsic::Backends::Vulkan
