module;
#include <RHI/RHI.Vulkan.hpp>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_map>

module Runtime.RenderGraph;

import Core.Memory;
import Core.Logging;

namespace Runtime::Graph
{
    // --- RGRegistry ---
    VkImage RGRegistry::GetImage(RGResourceHandle handle) const
    {
        if (handle.ID >= m_PhysicalImages.size()) return VK_NULL_HANDLE;
        return m_PhysicalImages[handle.ID].Image;
    }

    VkImageView RGRegistry::GetImageView(RGResourceHandle handle) const
    {
        if (handle.ID >= m_PhysicalImages.size()) return VK_NULL_HANDLE;
        return m_PhysicalImages[handle.ID].View;
    }

    VkBuffer RGRegistry::GetBuffer(RGResourceHandle handle) const
    {
        if (handle.ID >= m_PhysicalBuffers.size()) return VK_NULL_HANDLE;
        return m_PhysicalBuffers[handle.ID].Buffer;
    }

    void RGRegistry::RegisterImage(ResourceID id, VkImage img, VkImageView view)
    {
        if (m_PhysicalImages.size() <= id) m_PhysicalImages.resize(id + 1);
        m_PhysicalImages[id] = {img, view};
    }

    void RGRegistry::RegisterBuffer(ResourceID id, VkBuffer buffer)
    {
        if (m_PhysicalBuffers.size() <= id) m_PhysicalBuffers.resize(id + 1);
        m_PhysicalBuffers[id] = {buffer};
    }

    // --- RGBuilder ---
    RGResourceHandle RGBuilder::Read(RGResourceHandle resource)
    {
        m_Graph.m_Passes[m_PassIndex].Reads.push_back(resource.ID);
        return resource;
    }

    RGResourceHandle RGBuilder::Write(RGResourceHandle resource)
    {
        m_Graph.m_Passes[m_PassIndex].Writes.push_back(resource.ID);
        return resource;
    }

    RGResourceHandle RGBuilder::ReadBuffer(RGResourceHandle resource)
    {
        return Read(resource);
    }

    RGResourceHandle RGBuilder::WriteBuffer(RGResourceHandle resource)
    {
        return Write(resource);
    }

    RGResourceHandle RGBuilder::WriteColor(RGResourceHandle resource, RGAttachmentInfo info)
    {
        m_Graph.m_Passes[m_PassIndex].Writes.push_back(resource.ID);
        m_Graph.m_Passes[m_PassIndex].Attachments.push_back({resource.ID, info, false});
        return resource;
    }

    RGResourceHandle RGBuilder::WriteDepth(RGResourceHandle resource, RGAttachmentInfo info)
    {
        m_Graph.m_Passes[m_PassIndex].Writes.push_back(resource.ID);
        m_Graph.m_Passes[m_PassIndex].Attachments.push_back({resource.ID, info, true});
        return resource;
    }

    RGResourceHandle RGBuilder::CreateTexture(const std::string& name, const RGTextureDesc& desc)
    {
        auto [id, created] = m_Graph.CreateResourceInternal(name, ResourceType::Texture);

        // Only initialize properties if it's a new resource.
        // If existing, we assume properties match or it's a usage declaration.
        if (created)
        {
            auto& node = m_Graph.m_Resources[id];
            node.Extent = {desc.Width, desc.Height};
            node.InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            node.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            node.Format = desc.Format;
        }
        return {id};
    }

    RGResourceHandle RGBuilder::CreateBuffer(const std::string& name, const RGBufferDesc& desc)
    {
        auto [id, created] = m_Graph.CreateResourceInternal(name, ResourceType::Buffer);

        if (created)
        {
            auto& node = m_Graph.m_Resources[id];
            node.BufferSize = desc.Size;
            node.BufferUsage = desc.Usage;
            node.BufferMemory = desc.Memory;
            node.CurrentStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            node.CurrentAccess = 0;
        }
        return {id};
    }

    RGResourceHandle RGBuilder::ImportTexture(const std::string& name, VkImage image, VkImageView view, VkFormat format,
                                              VkExtent2D extent)
    {
        auto [id, created] = m_Graph.CreateResourceInternal(name, ResourceType::Import);

        if (created)
        {
            auto& node = m_Graph.m_Resources[id];
            node.PhysicalImage = image;
            node.PhysicalView = view;
            node.InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            node.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            node.Extent = extent;
            node.Format = format;
            m_Graph.m_Registry.RegisterImage(id, image, view);
        }
        else
        {
            // Resource exists. In a robust system, check if image == node.PhysicalImage.
            // We assume the user is sane and imports the same physical resource for the same name.
            // Important: Do NOT reset CurrentLayout here, as that tracks the state from previous passes.
        }
        return {id};
    }

    VkExtent2D RGBuilder::GetTextureExtent(RGResourceHandle handle) const
    {
        if (handle.ID < m_Graph.m_Resources.size())
        {
            return m_Graph.m_Resources[handle.ID].Extent;
        }
        return {0, 0};
    }

    // --- RenderGraph ---
    RenderGraph::RenderGraph(std::shared_ptr<RHI::VulkanDevice> device, Core::Memory::LinearArena& arena) : m_Device(device),
        m_Arena(arena)
    {
    }

    RenderGraph::~RenderGraph()
    {
        vkDeviceWaitIdle(m_Device->GetLogicalDevice());
        m_ImagePool.clear();
        m_BufferPool.clear();
    }

    RenderGraph::RGPass& RenderGraph::CreatePassInternal(const std::string& name)
    {
        return m_Passes.emplace_back(RGPass{name});
    }

    std::pair<ResourceID, bool> RenderGraph::CreateResourceInternal(const std::string& name, ResourceType type)
    {
        // 1. Check Aliasing
        if (auto it = m_ResourceLookup.find(name); it != m_ResourceLookup.end())
        {
            // Return existing ID and created=false
            return {it->second, false};
        }

        // 2. Create New
        ResourceID id = (ResourceID)m_Resources.size();
        ResourceNode node{};
        node.Name = name;
        node.Type = type;
        m_Resources.push_back(node);

        // Register name
        m_ResourceLookup[name] = id;

        return {id, true};
    }

    void RenderGraph::Reset()
    {
        m_Passes.clear();
        m_Resources.clear();
        m_Barriers.clear();
        m_ResourceLookup.clear(); // Clear lookup table
        m_Registry = RGRegistry();

        for (auto& item : m_ImagePool)
        {
            item.IsFree = true;
        }
        for (auto& item : m_BufferPool)
        {
            item.IsFree = true;
        }
    }

    RHI::VulkanImage* RenderGraph::AllocateImage(uint32_t frameIndex, uint32_t width, uint32_t height, VkFormat format,
                                                 VkImageUsageFlags usage, VkImageAspectFlags aspect)
    {
        for (auto& item : m_ImagePool)
        {
            if (item.IsFree && item.LastFrameIndex == frameIndex)
            {
                if (item.Resource->GetFormat() == format &&
                    item.Resource->GetWidth() == width &&
                    item.Resource->GetHeight() == height &&
                    item.Resource->GetView() != VK_NULL_HANDLE)
                {
                    item.IsFree = false;
                    return item.Resource.get();
                }
            }
        }

        auto img = std::make_unique<RHI::VulkanImage>(m_Device, width, height, 1, format, usage, aspect);
        auto* ptr = img.get();
        m_ImagePool.push_back({std::move(img), frameIndex, false});
        return ptr;
    }

    RHI::VulkanBuffer* RenderGraph::AllocateBuffer(uint32_t frameIndex, size_t size, VkBufferUsageFlags usage,
                                                   VmaMemoryUsage memoryUsage)
    {
        for (auto& item : m_BufferPool)
        {
            if (item.IsFree && item.LastFrameIndex == frameIndex)
            {
                if (item.Size == size && item.Usage == usage && item.Memory == memoryUsage)
                {
                    item.IsFree = false;
                    return item.Resource.get();
                }
            }
        }

        auto buffer = std::make_unique<RHI::VulkanBuffer>(m_Device, size, usage, memoryUsage);
        auto* ptr = buffer.get();
        m_BufferPool.push_back({std::move(buffer), frameIndex, false, size, usage, memoryUsage});
        return ptr;
    }

    namespace
    {
        VkPipelineStageFlags2 DetermineStageFromUsage(VkBufferUsageFlags usage, bool isWrite)
        {
            (void)isWrite;
            VkPipelineStageFlags2 stage = 0;

            if (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
            {
                stage |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            }
            if (usage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
            {
                stage |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            }
            if (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
            {
                stage |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            }

            if (stage == 0)
            {
                return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            }

            return stage;
        }

        VkAccessFlags2 DetermineAccessFromUsage(VkBufferUsageFlags usage, bool isWrite)
        {
            if (isWrite)
            {
                if (usage & (VK_BUFFER_USAGE_TRANSFER_DST_BIT))
                {
                    return VK_ACCESS_2_TRANSFER_WRITE_BIT;
                }
                if (usage & (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
                {
                    return VK_ACCESS_2_SHADER_WRITE_BIT;
                }
                return VK_ACCESS_2_MEMORY_WRITE_BIT;
            }

            VkAccessFlags2 access = VK_ACCESS_2_MEMORY_READ_BIT;
            if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
            {
                access |= VK_ACCESS_2_INDEX_READ_BIT;
            }
            if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
            {
                access |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
            }
            if (usage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT))
            {
                access |= VK_ACCESS_2_UNIFORM_READ_BIT;
            }
            if (usage & (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
            {
                access |= VK_ACCESS_2_SHADER_READ_BIT;
            }
            if (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
            {
                access |= VK_ACCESS_2_TRANSFER_READ_BIT;
            }

            return access;
        }
    }

    void RenderGraph::Compile(uint32_t frameIndex)
    {
        m_Barriers.resize(m_Passes.size());

        // 1. Resolve Transient Resources
        for (size_t i = 0; i < m_Resources.size(); ++i)
        {
            auto& res = m_Resources[i];
            if (res.Type == ResourceType::Texture && res.PhysicalImage == VK_NULL_HANDLE)
            {
                VkFormat fmt = res.Format;
                VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
                VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

                if (res.Name.find("Depth") != std::string::npos)
                {
                    if (fmt == VK_FORMAT_UNDEFINED) fmt = RHI::VulkanImage::FindDepthFormat(*m_Device);
                    usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                    aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
                }
                else
                {
                    usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                }

                RHI::VulkanImage* img = AllocateImage(frameIndex, res.Extent.width, res.Extent.height, fmt, usage,
                                                      aspect);
                res.PhysicalImage = img->GetHandle();
                res.PhysicalView = img->GetView();
            }
            if (res.PhysicalImage)
            {
                m_Registry.RegisterImage((ResourceID)i, res.PhysicalImage, res.PhysicalView);
            }
            else if (res.Type == ResourceType::Buffer && res.PhysicalBuffer == VK_NULL_HANDLE)
            {
                RHI::VulkanBuffer* buffer = AllocateBuffer(frameIndex, res.BufferSize, res.BufferUsage, res.BufferMemory);
                res.PhysicalBuffer = buffer->GetHandle();
            }
            if (res.PhysicalBuffer)
            {
                m_Registry.RegisterBuffer((ResourceID)i, res.PhysicalBuffer);
            }
        }

        // 2. Barrier Calculation
        for (size_t passIdx = 0; passIdx < m_Passes.size(); ++passIdx)
        {
            const auto& pass = m_Passes[passIdx];
            auto& batch = m_Barriers[passIdx];

            // A. Attachments
            for (const auto& att : pass.Attachments)
            {
                auto& res = m_Resources[att.ID];
                VkImageLayout targetLayout = att.IsDepth
                                                 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                 : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                VkImageMemoryBarrier2 barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                barrier.image = res.PhysicalImage;
                barrier.oldLayout = res.CurrentLayout;
                barrier.newLayout = targetLayout;

                barrier.srcStageMask = (res.CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                           ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
                                           : res.CurrentStage;
                barrier.srcAccessMask = (res.CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                            ? 0
                                            : res.CurrentAccess;

                if (att.IsDepth)
                {
                    barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                    barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    if (res.Format == VK_FORMAT_D32_SFLOAT_S8_UINT || res.Format == VK_FORMAT_D24_UNORM_S8_UINT)
                    {
                        barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
                    }
                }
                else
                {
                    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                }

                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;

                // Always emit the barrier so repeated writes to the same attachment acquire proper
                // execution and memory dependencies, even when layouts remain unchanged.
                batch.ImageBarriers.push_back(barrier);
                res.CurrentLayout = targetLayout;
                res.CurrentStage = barrier.dstStageMask;
                res.CurrentAccess = barrier.dstAccessMask;
            }

            // B. Reads
            for (ResourceID id : pass.Reads)
            {
                auto& res = m_Resources[id];
                if (res.Type == ResourceType::Texture || res.Type == ResourceType::Import)
                {
                    if (res.CurrentLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    {
                        VkImageMemoryBarrier2 barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                        barrier.image = res.PhysicalImage;
                        barrier.oldLayout = res.CurrentLayout;
                        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                        barrier.srcStageMask = (res.CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                                   ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
                                                   : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

                        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                        barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                        batch.ImageBarriers.push_back(barrier);
                        res.CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    }
                }
                else if (res.Type == ResourceType::Buffer)
                {
                    VkPipelineStageFlags2 dstStage = DetermineStageFromUsage(res.BufferUsage, false);
                    VkAccessFlags2 dstAccess = DetermineAccessFromUsage(res.BufferUsage, false);
                    if (res.CurrentStage != dstStage || res.CurrentAccess != dstAccess)
                    {
                        VkBufferMemoryBarrier2 barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                        barrier.buffer = res.PhysicalBuffer;
                        barrier.size = res.BufferSize;
                        barrier.srcStageMask = res.CurrentStage;
                        barrier.srcAccessMask = res.CurrentAccess;
                        barrier.dstStageMask = dstStage;
                        barrier.dstAccessMask = dstAccess;

                        batch.BufferBarriers.push_back(barrier);
                        res.CurrentStage = dstStage;
                        res.CurrentAccess = dstAccess;
                    }
                }
            }

            // C. Writes (non-attachments)
            for (ResourceID id : pass.Writes)
            {
                auto& res = m_Resources[id];
                if (res.Type == ResourceType::Buffer)
                {
                    VkPipelineStageFlags2 dstStage = DetermineStageFromUsage(res.BufferUsage, true);
                    VkAccessFlags2 dstAccess = DetermineAccessFromUsage(res.BufferUsage, true);
                    if (res.CurrentStage != dstStage || res.CurrentAccess != dstAccess)
                    {
                        VkBufferMemoryBarrier2 barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                        barrier.buffer = res.PhysicalBuffer;
                        barrier.size = res.BufferSize;
                        barrier.srcStageMask = res.CurrentStage;
                        barrier.srcAccessMask = res.CurrentAccess;
                        barrier.dstStageMask = dstStage;
                        barrier.dstAccessMask = dstAccess;

                        batch.BufferBarriers.push_back(barrier);
                        res.CurrentStage = dstStage;
                        res.CurrentAccess = dstAccess;
                    }
                }
            }
        }
    }

    void RenderGraph::Execute(VkCommandBuffer cmd)
    {
        for (size_t i = 0; i < m_Passes.size(); ++i)
        {
            const auto& pass = m_Passes[i];

            if (!m_Barriers[i].ImageBarriers.empty() || !m_Barriers[i].BufferBarriers.empty())
            {
                VkDependencyInfo depInfo{};
                depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                depInfo.imageMemoryBarrierCount = (uint32_t)m_Barriers[i].ImageBarriers.size();
                depInfo.pImageMemoryBarriers = m_Barriers[i].ImageBarriers.data();
                depInfo.bufferMemoryBarrierCount = (uint32_t)m_Barriers[i].BufferBarriers.size();
                depInfo.pBufferMemoryBarriers = m_Barriers[i].BufferBarriers.data();
                vkCmdPipelineBarrier2(cmd, &depInfo);
            }

            bool isRaster = !pass.Attachments.empty();
            if (isRaster)
            {
                std::vector<VkRenderingAttachmentInfo> colorAtts;
                VkRenderingAttachmentInfo depthAtt{};
                bool hasDepth = false;
                VkExtent2D renderArea = {0, 0};

                for (const auto& att : pass.Attachments)
                {
                    auto& res = m_Resources[att.ID];
                    renderArea = res.Extent;

                    VkRenderingAttachmentInfo info{};
                    info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    info.imageView = res.PhysicalView;
                    info.imageLayout = att.IsDepth
                                           ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                           : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    info.loadOp = att.Info.LoadOp;
                    info.storeOp = att.Info.StoreOp;
                    info.clearValue = att.Info.ClearValue;

                    if (att.IsDepth)
                    {
                        depthAtt = info;
                        hasDepth = true;
                    }
                    else
                    {
                        colorAtts.push_back(info);
                    }
                }

                VkRenderingInfo renderInfo{};
                renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                renderInfo.renderArea = {{0, 0}, renderArea};
                renderInfo.layerCount = 1;
                renderInfo.colorAttachmentCount = (uint32_t)colorAtts.size();
                renderInfo.pColorAttachments = colorAtts.data();
                renderInfo.pDepthAttachment = hasDepth ? &depthAtt : nullptr;

                vkCmdBeginRendering(cmd, &renderInfo);
            }

            pass.Execute(m_Registry, cmd);

            if (isRaster)
            {
                vkCmdEndRendering(cmd);
            }
        }
    }
}
