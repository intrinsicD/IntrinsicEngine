module;

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import :CommandPools;
import Extrinsic.Core.Logging;

namespace Extrinsic::Backends::Vulkan
{
namespace
{
    std::atomic<std::uint64_t> g_FallbackCommandRecordingAttempts{0};

    void NoteFallbackCommandRecordingAttempt() noexcept
    {
        g_FallbackCommandRecordingAttempts.fetch_add(1, std::memory_order_relaxed);
    }
}

std::uint64_t GetFallbackCommandRecordingAttemptCount() noexcept
{
    return g_FallbackCommandRecordingAttempts.load(std::memory_order_relaxed);
}

// =============================================================================
// §9  VulkanCommandContext
// =============================================================================

void VulkanCommandContext::Bind(VkDevice device, VkCommandBuffer cmd,
                                 VkPipelineLayout globalLayout,
                                 VkDescriptorSet  bindlessSet,
                                 Core::ResourcePool<VulkanBuffer,   RHI::BufferHandle,   kMaxFramesInFlight>* buffers,
                                 Core::ResourcePool<VulkanImage,    RHI::TextureHandle,  kMaxFramesInFlight>* images,
                                 Core::ResourcePool<VulkanSampler,  RHI::SamplerHandle,  kMaxFramesInFlight>* samplers,
                                 Core::ResourcePool<VulkanPipeline, RHI::PipelineHandle, kMaxFramesInFlight>* pipelines,
                                 RHI::SamplerHandle defaultSampler,
                                 uint32_t graphicsQueueFamily,
                                 uint32_t presentQueueFamily,
                                 uint32_t transferQueueFamily)
{
    m_Device        = device;
    m_Cmd           = cmd;
    m_GlobalLayout  = globalLayout;
    m_BindlessSet   = bindlessSet;
    m_Buffers       = buffers;
    m_Images        = images;
    m_Samplers      = samplers;
    m_Pipelines     = pipelines;
    m_DefaultSampler = defaultSampler;
    m_GraphicsQueueFamily = graphicsQueueFamily;
    m_PresentQueueFamily  = presentQueueFamily;
    m_TransferQueueFamily = transferQueueFamily;
    m_BindPoint     = VK_PIPELINE_BIND_POINT_GRAPHICS;
    m_Recording     = false;
}

bool VulkanCommandContext::CanBegin() const
{
    if (m_Device == VK_NULL_HANDLE || m_Cmd == VK_NULL_HANDLE)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] Begin skipped; command context is not bound to a live command buffer");
        return false;
    }
    return true;
}

bool VulkanCommandContext::CanRecord(const char* operation) const
{
    if (m_Cmd == VK_NULL_HANDLE)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] Command skipped; command context is not bound to a live command buffer");
        return false;
    }
    if (!m_Recording)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("{}", operation ? operation : "[VulkanCommandContext] Command skipped; command context is not recording");
        return false;
    }
    return true;
}

void VulkanCommandContext::UpdateFrameSampledDescriptor(RHI::TextureHandle texture,
                                                        VkImageLayout layout,
                                                        const std::uint32_t descriptorIndex)
{
    if (layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        return;
    }
    if (m_Device == VK_NULL_HANDLE || m_BindlessSet == VK_NULL_HANDLE ||
        m_Images == nullptr || m_Samplers == nullptr || !m_DefaultSampler.IsValid())
    {
        return;
    }
    const VulkanImage* image = m_Images->GetIfValid(texture);
    const VulkanSampler* sampler = m_Samplers->GetIfValid(m_DefaultSampler);
    if (image == nullptr || sampler == nullptr ||
        image->View == VK_NULL_HANDLE || sampler->Sampler == VK_NULL_HANDLE)
    {
        return;
    }

    // GRAPHICS-076E/076F — temporary promoted-Vulkan sampled-present bridge.
    // The canonical postprocess/present shaders currently read their single
    // framegraph input from set 0 / binding 0 / a renderer-selected element.
    // Slot 0 preserves the older postprocess bridge. Slots 1 and 2 are
    // reserved by the renderer for DebugView and Present so they do not race by
    // rewriting the same descriptor element before one command-buffer submit.
    VkDescriptorImageInfo info{};
    info.imageView = image->View;
    info.sampler = sampler->Sampler;
    info.imageLayout = layout;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_BindlessSet;
    write.dstBinding = 0u;
    write.dstArrayElement = descriptorIndex;
    write.descriptorCount = 1u;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(m_Device, 1u, &write, 0u, nullptr);
}

void VulkanCommandContext::Begin()
{
    [[maybe_unused]] Core::Telemetry::ScopedTimer timer{"VulkanCommandContext::Begin", Extrinsic::Core::Telemetry::HashString("VulkanCommandContext::Begin")};
    if (!CanBegin())
        return;

    VkResult result = vkResetCommandBuffer(m_Cmd, 0u);
    if (result != VK_SUCCESS)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Error("[VulkanCommandContext] vkResetCommandBuffer failed; recording skipped");
        m_Recording = false;
        return;
    }

    VkCommandBufferBeginInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(m_Cmd, &ci);
    if (result != VK_SUCCESS)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Error("[VulkanCommandContext] vkBeginCommandBuffer failed; recording skipped");
        m_Recording = false;
        return;
    }
    m_Recording = true;
}

void VulkanCommandContext::End()
{
    [[maybe_unused]] Core::Telemetry::ScopedTimer timer{"VulkanCommandContext::End", Extrinsic::Core::Telemetry::HashString("VulkanCommandContext::End")};
    if (!CanRecord("[VulkanCommandContext] End skipped; command context is not recording"))
        return;

    const VkResult result = vkEndCommandBuffer(m_Cmd);
    if (result != VK_SUCCESS)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Error("[VulkanCommandContext] vkEndCommandBuffer failed; command buffer discarded");
        m_Recording = false;
        return;
    }
    m_Recording = false;
}

void VulkanCommandContext::BeginRenderPass(const RHI::RenderPassDesc& desc)
{
    if (!CanRecord("[VulkanCommandContext] BeginRenderPass skipped; command context is not recording") || !m_Images)
    {
        if (m_Cmd != VK_NULL_HANDLE && m_Recording && !m_Images)
        {
            NoteFallbackCommandRecordingAttempt();
            Core::Log::Warn("[VulkanCommandContext] BeginRenderPass skipped; image pool is unavailable");
        }
        return;
    }

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
    if (!CanRecord("[VulkanCommandContext] EndRenderPass skipped; command context is not recording"))
        return;
    vkCmdEndRendering(m_Cmd);
}

void VulkanCommandContext::SetViewport(float x, float y, float w, float h,
                                        float minD, float maxD)
{
    if (!CanRecord("[VulkanCommandContext] SetViewport skipped; command context is not recording"))
        return;

    // Flip Y for Vulkan NDC (origin top-left, Y down).
    VkViewport vp{x, y + h, w, -h, minD, maxD};
    vkCmdSetViewport(m_Cmd, 0, 1, &vp);
}

void VulkanCommandContext::SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    if (!CanRecord("[VulkanCommandContext] SetScissor skipped; command context is not recording"))
        return;

    VkRect2D sc{{x, y}, {w, h}};
    vkCmdSetScissor(m_Cmd, 0, 1, &sc);
}

void VulkanCommandContext::BindPipeline(RHI::PipelineHandle handle)
{
    if (!CanRecord("[VulkanCommandContext] BindPipeline skipped; command context is not recording"))
        return;
    if (!m_Pipelines || m_GlobalLayout == VK_NULL_HANDLE || m_BindlessSet == VK_NULL_HANDLE)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] BindPipeline skipped; pipeline pool or global bindless state is unavailable");
        return;
    }

    const auto* pip = m_Pipelines->GetIfValid(handle);
    if (!pip || pip->Pipeline == VK_NULL_HANDLE) return;
    m_BindPoint = pip->BindPoint;
    vkCmdBindPipeline(m_Cmd, m_BindPoint, pip->Pipeline);
    // Always bind the global bindless descriptor set at set 0.
    vkCmdBindDescriptorSets(m_Cmd, m_BindPoint, m_GlobalLayout,
                            0, 1, &m_BindlessSet, 0, nullptr);
}

void VulkanCommandContext::BindFrameSampledTexture(RHI::TextureHandle texture)
{
    UpdateFrameSampledDescriptor(texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0u);
}

void VulkanCommandContext::BindFrameSampledTextureAt(RHI::TextureHandle texture, const std::uint32_t descriptorIndex)
{
    UpdateFrameSampledDescriptor(texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, descriptorIndex);
}

void VulkanCommandContext::PushConstants(const void* data, uint32_t size, uint32_t offset)
{
    if (!CanRecord("[VulkanCommandContext] PushConstants skipped; command context is not recording"))
        return;
    if (m_GlobalLayout == VK_NULL_HANDLE || data == nullptr || size == 0u)
        return;

    vkCmdPushConstants(m_Cmd, m_GlobalLayout,
                       VK_SHADER_STAGE_ALL, offset, size, data);
}

void VulkanCommandContext::BindIndexBuffer(RHI::BufferHandle handle,
                                           uint64_t offset,
                                           RHI::IndexType indexType)
{
    if (!CanRecord("[VulkanCommandContext] BindIndexBuffer skipped; command context is not recording"))
        return;
    if (!m_Buffers)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] BindIndexBuffer skipped; buffer pool is unavailable");
        return;
    }

    const auto* buf = m_Buffers->GetIfValid(handle);
    if (!buf || buf->Buffer == VK_NULL_HANDLE) return;
    vkCmdBindIndexBuffer(m_Cmd, buf->Buffer, offset, ToVkIndexType(indexType));
}

void VulkanCommandContext::Draw(uint32_t vertexCount, uint32_t instanceCount,
                                 uint32_t firstVertex, uint32_t firstInstance)
{
    if (!CanRecord("[VulkanCommandContext] Draw skipped; command context is not recording"))
        return;
    vkCmdDraw(m_Cmd, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandContext::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                        uint32_t firstIndex, int32_t vertexOffset,
                                        uint32_t firstInstance)
{
    if (!CanRecord("[VulkanCommandContext] DrawIndexed skipped; command context is not recording"))
        return;
    vkCmdDrawIndexed(m_Cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandContext::DrawIndirect(RHI::BufferHandle argBuf,
                                         uint64_t offset, uint32_t drawCount)
{
    if (!CanRecord("[VulkanCommandContext] DrawIndirect skipped; command context is not recording"))
        return;
    if (!m_Buffers)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] DrawIndirect skipped; buffer pool is unavailable");
        return;
    }

    const auto* buf = m_Buffers->GetIfValid(argBuf);
    if (!buf || buf->Buffer == VK_NULL_HANDLE) return;
    vkCmdDrawIndirect(m_Cmd, buf->Buffer, offset, drawCount,
                      sizeof(VkDrawIndirectCommand));
}

void VulkanCommandContext::DrawIndexedIndirect(RHI::BufferHandle argBuf,
                                                uint64_t offset, uint32_t drawCount)
{
    if (!CanRecord("[VulkanCommandContext] DrawIndexedIndirect skipped; command context is not recording"))
        return;
    if (!m_Buffers)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] DrawIndexedIndirect skipped; buffer pool is unavailable");
        return;
    }

    const auto* buf = m_Buffers->GetIfValid(argBuf);
    if (!buf || buf->Buffer == VK_NULL_HANDLE) return;
    vkCmdDrawIndexedIndirect(m_Cmd, buf->Buffer, offset, drawCount,
                             sizeof(VkDrawIndexedIndirectCommand));
}

void VulkanCommandContext::DrawIndexedIndirectCount(RHI::BufferHandle argBuf, uint64_t argOffset,
                                                     RHI::BufferHandle cntBuf, uint64_t cntOffset,
                                                     uint32_t maxDraw)
{
    if (!CanRecord("[VulkanCommandContext] DrawIndexedIndirectCount skipped; command context is not recording"))
        return;
    if (!m_Buffers)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] DrawIndexedIndirectCount skipped; buffer pool is unavailable");
        return;
    }

    const auto* abuf = m_Buffers->GetIfValid(argBuf);
    const auto* cbuf = m_Buffers->GetIfValid(cntBuf);
    if (!abuf || !cbuf || abuf->Buffer == VK_NULL_HANDLE || cbuf->Buffer == VK_NULL_HANDLE) return;
    vkCmdDrawIndexedIndirectCount(m_Cmd, abuf->Buffer, argOffset,
                                  cbuf->Buffer, cntOffset, maxDraw,
                                  sizeof(VkDrawIndexedIndirectCommand));
}

void VulkanCommandContext::DrawIndirectCount(RHI::BufferHandle argBuf, uint64_t argOffset,
                                              RHI::BufferHandle cntBuf, uint64_t cntOffset,
                                              uint32_t maxDraw)
{
    if (!CanRecord("[VulkanCommandContext] DrawIndirectCount skipped; command context is not recording"))
        return;
    if (!m_Buffers)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] DrawIndirectCount skipped; buffer pool is unavailable");
        return;
    }

    const auto* abuf = m_Buffers->GetIfValid(argBuf);
    const auto* cbuf = m_Buffers->GetIfValid(cntBuf);
    if (!abuf || !cbuf || abuf->Buffer == VK_NULL_HANDLE || cbuf->Buffer == VK_NULL_HANDLE) return;
    vkCmdDrawIndirectCount(m_Cmd, abuf->Buffer, argOffset,
                           cbuf->Buffer, cntOffset, maxDraw,
                           sizeof(VkDrawIndirectCommand));
}

void VulkanCommandContext::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz)
{
    if (!CanRecord("[VulkanCommandContext] Dispatch skipped; command context is not recording"))
        return;
    vkCmdDispatch(m_Cmd, gx, gy, gz);
}

void VulkanCommandContext::DispatchIndirect(RHI::BufferHandle argBuf, uint64_t offset)
{
    if (!CanRecord("[VulkanCommandContext] DispatchIndirect skipped; command context is not recording"))
        return;
    if (!m_Buffers)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] DispatchIndirect skipped; buffer pool is unavailable");
        return;
    }

    const auto* buf = m_Buffers->GetIfValid(argBuf);
    if (!buf || buf->Buffer == VK_NULL_HANDLE) return;
    vkCmdDispatchIndirect(m_Cmd, buf->Buffer, offset);
}

void VulkanCommandContext::TextureBarrier(RHI::TextureHandle tex,
                                           RHI::TextureLayout before,
                                           RHI::TextureLayout after)
{
    if (!CanRecord("[VulkanCommandContext] TextureBarrier skipped; command context is not recording"))
        return;
    if (!m_Images)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] TextureBarrier skipped; image pool is unavailable");
        return;
    }

    auto* img = m_Images->GetIfValid(tex);
    if (!img || img->Image == VK_NULL_HANDLE) return;
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
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image         = img->Image;
    barrier.subresourceRange = {AspectFromFormat(img->Format), 0, img->MipLevels, 0, img->ArrayLayers};

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(m_Cmd, &dep);

    img->CurrentLayout = newLayout;
    UpdateFrameSampledDescriptor(tex, newLayout, 0u);
}

void VulkanCommandContext::BufferBarrier(RHI::BufferHandle buf,
                                          RHI::MemoryAccess before,
                                          RHI::MemoryAccess after)
{
    if (!CanRecord("[VulkanCommandContext] BufferBarrier skipped; command context is not recording"))
        return;
    if (!m_Buffers)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] BufferBarrier skipped; buffer pool is unavailable");
        return;
    }

    const auto* b = m_Buffers->GetIfValid(buf);
    if (!b || b->Buffer == VK_NULL_HANDLE) return;

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
    if (!CanRecord("[VulkanCommandContext] SubmitBarriers skipped; command context is not recording"))
        return;

    std::vector<VkImageMemoryBarrier2> imageBarriers{};
    std::vector<VulkanImage*> imageBarrierTargets{};
    std::vector<RHI::TextureHandle> imageBarrierHandles{};
    std::vector<VkBufferMemoryBarrier2> bufferBarriers{};
    std::vector<VkMemoryBarrier2> memoryBarriers{};

    imageBarriers.reserve(batch.TextureBarriers.size());
    imageBarrierTargets.reserve(batch.TextureBarriers.size());
    imageBarrierHandles.reserve(batch.TextureBarriers.size());
    bufferBarriers.reserve(batch.BufferBarriers.size());
    memoryBarriers.reserve(batch.MemoryBarriers.size());

    const auto resolveQueueFamily = [this](uint32_t requested) -> uint32_t
    {
        // Backend-neutral RHI traffic encodes "no ownership transfer" as
        // VK_QUEUE_FAMILY_IGNORED; honor it as a passthrough. When the renderer
        // asks for graphics/transfer/present ownership it does so via the same
        // sentinel today, so the command context conservatively keeps the
        // request as supplied. Concrete queue-family translation is reserved
        // for future renderer/transfer integrations once IsOperational() can
        // be true.
        (void)this;
        return requested;
    };

    for (const RHI::TextureBarrierDesc& desc : batch.TextureBarriers)
    {
        if (!m_Images)
        {
            NoteFallbackCommandRecordingAttempt();
            Core::Log::Warn("[VulkanCommandContext] SubmitBarriers skipped texture barriers; image pool is unavailable");
            break;
        }
        auto* image = m_Images->GetIfValid(desc.Texture);
        if (!image || image->Image == VK_NULL_HANDLE) continue;

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = ToVkStage(desc.BeforeAccess);
        barrier.srcAccessMask = ToVkAccess(desc.BeforeAccess);
        barrier.dstStageMask = ToVkStage(desc.AfterAccess);
        barrier.dstAccessMask = ToVkAccess(desc.AfterAccess);
        barrier.oldLayout = ToVkImageLayout(desc.BeforeLayout);
        barrier.newLayout = ToVkImageLayout(desc.AfterLayout);
        barrier.srcQueueFamilyIndex = resolveQueueFamily(desc.SrcQueueFamily);
        barrier.dstQueueFamilyIndex = resolveQueueFamily(desc.DstQueueFamily);
        barrier.image = image->Image;
        barrier.subresourceRange = {AspectFromFormat(image->Format), 0, image->MipLevels, 0, image->ArrayLayers};
        imageBarriers.push_back(barrier);
        imageBarrierTargets.push_back(image);
        imageBarrierHandles.push_back(desc.Texture);
    }

    for (const RHI::BufferBarrierDesc& desc : batch.BufferBarriers)
    {
        if (!m_Buffers)
        {
            NoteFallbackCommandRecordingAttempt();
            Core::Log::Warn("[VulkanCommandContext] SubmitBarriers skipped buffer barriers; buffer pool is unavailable");
            break;
        }
        const auto* buffer = m_Buffers->GetIfValid(desc.Buffer);
        if (!buffer || buffer->Buffer == VK_NULL_HANDLE) continue;

        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = ToVkStage(desc.BeforeAccess);
        barrier.srcAccessMask = ToVkAccess(desc.BeforeAccess);
        barrier.dstStageMask = ToVkStage(desc.AfterAccess);
        barrier.dstAccessMask = ToVkAccess(desc.AfterAccess);
        barrier.srcQueueFamilyIndex = resolveQueueFamily(desc.SrcQueueFamily);
        barrier.dstQueueFamilyIndex = resolveQueueFamily(desc.DstQueueFamily);
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

    // Record-time CurrentLayout tracking. The GPU executes barriers later, but
    // tracking the recorded layout here lets subsequent barriers/uploads pick a
    // correct oldLayout without the renderer/RHI seam having to know which
    // backend recorded the prior transition.
    bool frameSampledDescriptorUpdated = false;
    for (std::size_t barrierIndex = 0; barrierIndex < imageBarriers.size(); ++barrierIndex)
    {
        if (imageBarrierTargets[barrierIndex] != nullptr)
        {
            imageBarrierTargets[barrierIndex]->CurrentLayout = imageBarriers[barrierIndex].newLayout;
            if (!frameSampledDescriptorUpdated && barrierIndex < imageBarrierHandles.size() &&
                imageBarriers[barrierIndex].newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                UpdateFrameSampledDescriptor(imageBarrierHandles[barrierIndex],
                                             imageBarriers[barrierIndex].newLayout,
                                             0u);
                frameSampledDescriptorUpdated = true;
            }
        }
    }
}

void VulkanCommandContext::FillBuffer(RHI::BufferHandle handle,
                                    uint64_t offset,
                                    uint64_t size,
                                    uint32_t value)
{
    if (!CanRecord("[VulkanCommandContext] FillBuffer skipped; command context is not recording"))
        return;
    if (!m_Buffers)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] FillBuffer skipped; buffer pool is unavailable");
        return;
    }

    const auto* buf = m_Buffers->GetIfValid(handle);
    if (!buf || buf->Buffer == VK_NULL_HANDLE) return;
    vkCmdFillBuffer(m_Cmd, buf->Buffer, offset, size, value);
}

void VulkanCommandContext::CopyBuffer(RHI::BufferHandle src, RHI::BufferHandle dst,
                                       uint64_t srcOff, uint64_t dstOff, uint64_t size)
{
    if (!CanRecord("[VulkanCommandContext] CopyBuffer skipped; command context is not recording"))
        return;
    if (!m_Buffers)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] CopyBuffer skipped; buffer pool is unavailable");
        return;
    }

    const auto* s = m_Buffers->GetIfValid(src);
    const auto* d = m_Buffers->GetIfValid(dst);
    if (!s || !d || s->Buffer == VK_NULL_HANDLE || d->Buffer == VK_NULL_HANDLE) return;
    VkBufferCopy region{srcOff, dstOff, size};
    vkCmdCopyBuffer(m_Cmd, s->Buffer, d->Buffer, 1, &region);
}

void VulkanCommandContext::CopyBufferToTexture(RHI::BufferHandle src, uint64_t srcOff,
                                                RHI::TextureHandle dst,
                                                uint32_t mipLevel, uint32_t arrayLayer)
{
    if (!CanRecord("[VulkanCommandContext] CopyBufferToTexture skipped; command context is not recording"))
        return;
    if (!m_Buffers || !m_Images)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] CopyBufferToTexture skipped; buffer or image pool is unavailable");
        return;
    }

    const auto* s = m_Buffers->GetIfValid(src);
    const auto* d = m_Images->GetIfValid(dst);
    if (!s || !d || s->Buffer == VK_NULL_HANDLE || d->Image == VK_NULL_HANDLE) return;
    if (mipLevel >= d->MipLevels || arrayLayer >= d->ArrayLayers) return;
    VkBufferImageCopy region{};
    region.bufferOffset     = srcOff;
    region.imageSubresource = {AspectFromFormat(d->Format), mipLevel, arrayLayer, 1};
    region.imageExtent      = {std::max(1u, d->Width >> mipLevel),
                                std::max(1u, d->Height >> mipLevel), 1};
    vkCmdCopyBufferToImage(m_Cmd, s->Buffer, d->Image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void VulkanCommandContext::CopyTextureToBuffer(RHI::TextureHandle src,
                                                RHI::TextureLayout srcLayout,
                                                uint32_t mipLevel, uint32_t arrayLayer,
                                                RHI::BufferHandle dst,
                                                uint64_t dstOffset,
                                                uint32_t srcOffsetX,
                                                uint32_t srcOffsetY,
                                                uint32_t srcWidth,
                                                uint32_t srcHeight)
{
    if (!CanRecord("[VulkanCommandContext] CopyTextureToBuffer skipped; command context is not recording"))
        return;
    if (!m_Buffers || !m_Images)
    {
        NoteFallbackCommandRecordingAttempt();
        Core::Log::Warn("[VulkanCommandContext] CopyTextureToBuffer skipped; buffer or image pool is unavailable");
        return;
    }

    const auto* s = m_Images->GetIfValid(src);
    const auto* d = m_Buffers->GetIfValid(dst);
    if (!s || !d || s->Image == VK_NULL_HANDLE || d->Buffer == VK_NULL_HANDLE) return;
    if (mipLevel >= s->MipLevels || arrayLayer >= s->ArrayLayers) return;

    const uint32_t mipWidth  = std::max(1u, s->Width  >> mipLevel);
    const uint32_t mipHeight = std::max(1u, s->Height >> mipLevel);
    if (srcOffsetX >= mipWidth || srcOffsetY >= mipHeight) return;
    // GRAPHICS-074 Slice D.2 — `(srcWidth, srcHeight) = (0, 0)` means
    // "whole mip extent" (the GRAPHICS-033D backbuffer-readback contract);
    // any non-zero width/height pins the copy to that sub-rect (the
    // single-pixel picking readback uses 1x1). Clamp so callers cannot
    // overrun the mip even if they pass an oversized region.
    const uint32_t copyWidth  = (srcWidth  == 0u) ? (mipWidth  - srcOffsetX)
                                                  : std::min(srcWidth,  mipWidth  - srcOffsetX);
    const uint32_t copyHeight = (srcHeight == 0u) ? (mipHeight - srcOffsetY)
                                                  : std::min(srcHeight, mipHeight - srcOffsetY);

    VkBufferImageCopy region{};
    region.bufferOffset     = dstOffset;
    region.imageSubresource = {AspectFromFormat(s->Format), mipLevel, arrayLayer, 1};
    region.imageOffset      = {static_cast<int32_t>(srcOffsetX), static_cast<int32_t>(srcOffsetY), 0};
    region.imageExtent      = {copyWidth, copyHeight, 1};
    vkCmdCopyImageToBuffer(m_Cmd, s->Image, ToVkImageLayout(srcLayout),
                           d->Buffer, 1, &region);
}

} // namespace Extrinsic::Backends::Vulkan
