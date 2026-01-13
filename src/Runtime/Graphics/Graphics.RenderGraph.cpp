// Graphics.RenderGraph.cpp
module;
#include "RHI.Vulkan.hpp"
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_map>

module Graphics:RenderGraph.Impl;
import :RenderGraph;
import Core;

namespace Graphics
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
        return m_PhysicalBuffers[handle.ID];
    }

    void RGRegistry::RegisterImage(ResourceID id, VkImage img, VkImageView view)
    {
        if (m_PhysicalImages.size() <= id) m_PhysicalImages.resize(id + 1);
        m_PhysicalImages[id] = {img, view};
    }

    void RGRegistry::RegisterBuffer(ResourceID id, VkBuffer buffer)
    {
        if (m_PhysicalBuffers.size() <= id) m_PhysicalBuffers.resize(id + 1);
        m_PhysicalBuffers[id] = buffer;
    }

    // --- RGBuilder ---
    
    RGResourceHandle RGBuilder::Read(RGResourceHandle resource, VkPipelineStageFlags2 stage, VkAccessFlags2 access)
    {
        m_Graph.m_Passes[m_PassIndex].Accesses.push_back({resource.ID, stage, access});
        // Extend lifetime
        auto& node = m_Graph.m_Resources[resource.ID];
        node.EndPass = std::max(node.EndPass, m_PassIndex);
        return resource;
    }

    RGResourceHandle RGBuilder::Write(RGResourceHandle resource, VkPipelineStageFlags2 stage, VkAccessFlags2 access)
    {
        m_Graph.m_Passes[m_PassIndex].Accesses.push_back({resource.ID, stage, access});
        auto& node = m_Graph.m_Resources[resource.ID];
        if (node.StartPass == ~0u) node.StartPass = m_PassIndex; // First write defines start
        node.EndPass = std::max(node.EndPass, m_PassIndex);
        return resource;
    }

    RGResourceHandle RGBuilder::WriteColor(RGResourceHandle resource, RGAttachmentInfo info)
    {
        // Raster write is implicitly Color Attachment Output
        Write(resource, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        m_Graph.m_Passes[m_PassIndex].Attachments.push_back({resource.ID, info, false});
        return resource;
    }

    RGResourceHandle RGBuilder::WriteDepth(RGResourceHandle resource, RGAttachmentInfo info)
    {
        Write(resource, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        m_Graph.m_Passes[m_PassIndex].Attachments.push_back({resource.ID, info, true});
        return resource;
    }

    RGResourceHandle RGBuilder::CreateTexture(Core::Hash::StringID name, const RGTextureDesc& desc)
    {
        auto [id, created] = m_Graph.CreateResourceInternal(name, ResourceType::Texture);
        if (created)
        {
            auto& node = m_Graph.m_Resources[id];
            node.Extent = {desc.Width, desc.Height};
            node.InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            node.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            node.Format = desc.Format;
            node.Usage = desc.Usage;
            node.Aspect = desc.Aspect;
        }
        return {id};
    }

    RGResourceHandle RGBuilder::CreateBuffer(Core::Hash::StringID name, const RGBufferDesc& desc)
    {
        auto [id, created] = m_Graph.CreateResourceInternal(name, ResourceType::Buffer);
        if (created)
        {
            auto& node = m_Graph.m_Resources[id];
            node.BufferSize = desc.Size;
            node.BufferUsage = desc.Usage;
        }
        return {id};
    }

    RGResourceHandle RGBuilder::ImportTexture(Core::Hash::StringID name, VkImage image, VkImageView view, VkFormat format,
                                              VkExtent2D extent, VkImageLayout currentLayout)
    {
        auto [id, created] = m_Graph.CreateResourceInternal(name, ResourceType::Import);
        if (created)
        {
            auto& node = m_Graph.m_Resources[id];
            node.PhysicalImage = image;
            node.PhysicalView = view;
            node.InitialLayout = currentLayout;
            node.CurrentLayout = currentLayout;
            node.Extent = extent;
            node.Format = format;
            node.Aspect = (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT)
                              ? (VkImageAspectFlags)(VK_IMAGE_ASPECT_DEPTH_BIT) // Simplify logic for now
                              : VK_IMAGE_ASPECT_COLOR_BIT;
            m_Graph.m_Registry.RegisterImage(id, image, view);
            
            // Mark as active immediately for imports
            node.StartPass = 0;
            node.EndPass = 0;
        }
        return {id};
    }

    RGResourceHandle RGBuilder::ImportBuffer(Core::Hash::StringID name, RHI::VulkanBuffer& buffer)
    {
        auto [id, created] = m_Graph.CreateResourceInternal(name, ResourceType::Import);
        if (created)
        {
            auto& node = m_Graph.m_Resources[id];
            node.PhysicalBuffer = buffer.GetHandle();
            m_Graph.m_Registry.RegisterBuffer(id, buffer.GetHandle());
            node.StartPass = 0; 
            node.EndPass = 0;
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
    RenderGraph::RenderGraph(std::shared_ptr<RHI::VulkanDevice> device, Core::Memory::LinearArena& arena) :
        m_Device(device),
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
        return m_Passes.emplace_back(RGPass{name, {}});
    }

    std::pair<ResourceID, bool> RenderGraph::CreateResourceInternal(Core::Hash::StringID name, ResourceType type)
    {
        if (auto it = m_ResourceLookup.find(name); it != m_ResourceLookup.end())
        {
            return {it->second, false};
        }
        auto id = static_cast<ResourceID>(m_Resources.size());
        ResourceNode node{};
        node.Name = name;
        node.Type = type;
        m_Resources.push_back(node);
        m_ResourceLookup[name] = id;
        return {id, true};
    }

    void RenderGraph::Reset()
    {
        m_Passes.clear();
        m_Resources.clear();
        m_Barriers.clear();
        m_ResourceLookup.clear();
        m_Registry = RGRegistry();
    }

    RHI::VulkanImage* RenderGraph::ResolveImage(uint32_t frameIndex, const ResourceNode& node)
    {
        ImageCacheKey key{node.Format, node.Extent.width, node.Extent.height, node.Usage};
        auto& stack = m_ImagePool[key];

        for (auto& item : stack.Images)
        {
            if (item.LastFrameIndex < frameIndex)
            {
                item.LastFrameIndex = frameIndex;
                item.ActiveIntervals.clear();
                item.ActiveIntervals.emplace_back(node.StartPass, node.EndPass);
                return item.Resource.get();
            }
            if (item.LastFrameIndex == frameIndex)
            {
                bool overlap = false;
                for (const auto& interval : item.ActiveIntervals)
                {
                    if (node.StartPass <= interval.second && node.EndPass >= interval.first)
                    {
                        overlap = true;
                        break;
                    }
                }
                if (!overlap)
                {
                    item.ActiveIntervals.emplace_back(node.StartPass, node.EndPass);
                    return item.Resource.get();
                }
            }
        }

        auto img = std::make_unique<RHI::VulkanImage>(m_Device, node.Extent.width, node.Extent.height, 1, node.Format, node.Usage, node.Aspect);
        auto* ptr = img.get();
        PooledImage pooled{std::move(img), frameIndex};
        pooled.ActiveIntervals.emplace_back(node.StartPass, node.EndPass);
        stack.Images.push_back(std::move(pooled));
        return ptr;
    }

    RHI::VulkanBuffer* RenderGraph::ResolveBuffer(uint32_t frameIndex, const ResourceNode& node)
    {
        BufferCacheKey key{node.BufferSize, node.BufferUsage};
        auto& stack = m_BufferPool[key];

        for (auto& item : stack.Buffers)
        {
            if (item.LastFrameIndex < frameIndex)
            {
                item.LastFrameIndex = frameIndex;
                item.ActiveIntervals.clear();
                item.ActiveIntervals.emplace_back(node.StartPass, node.EndPass);
                return item.Resource.get();
            }
            if (item.LastFrameIndex == frameIndex)
            {
                bool overlap = false;
                for (const auto& interval : item.ActiveIntervals)
                {
                    if (node.StartPass <= interval.second && node.EndPass >= interval.first)
                    {
                        overlap = true;
                        break;
                    }
                }
                if (!overlap)
                {
                    item.ActiveIntervals.emplace_back(node.StartPass, node.EndPass);
                    return item.Resource.get();
                }
            }
        }

        auto buf = std::make_unique<RHI::VulkanBuffer>(m_Device, node.BufferSize, node.BufferUsage, VMA_MEMORY_USAGE_GPU_ONLY);
        auto* ptr = buf.get();
        PooledBuffer pooled{std::move(buf), frameIndex};
        pooled.ActiveIntervals.emplace_back(node.StartPass, node.EndPass);
        stack.Buffers.push_back(std::move(pooled));
        return ptr;
    }

    void RenderGraph::Compile(uint32_t frameIndex)
    {
        m_Barriers.resize(m_Passes.size());

        // Clear any previous barrier batches (Compile() can be called every frame)
        for (auto& batch : m_Barriers)
        {
            batch.ImageBarriers.clear();
            batch.BufferBarriers.clear();
        }

        // 1. Resolve Transient Resources
        for (size_t i = 0; i < m_Resources.size(); ++i)
        {
            auto& res = m_Resources[i];

            // Ensure imported resources start from their declared initial state each frame.
            if (res.Type == ResourceType::Import)
            {
                res.CurrentLayout = res.InitialLayout;
                res.LastUsageStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                res.LastUsageAccess = 0;
            }

            // Texture Handling
            if (res.Type == ResourceType::Texture && res.PhysicalImage == VK_NULL_HANDLE)
            {
                RHI::VulkanImage* img = ResolveImage(frameIndex, res);
                if (!img || !img->IsValid())
                {
                    Core::Log::Error("RenderGraph: failed to allocate transient image for resource {}", (uint32_t)i);
                    res.PhysicalImage = VK_NULL_HANDLE;
                    res.PhysicalView = VK_NULL_HANDLE;
                    continue;
                }

                res.PhysicalImage = img->GetHandle();
                res.PhysicalView = img->GetView();
                res.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                res.InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                m_Registry.RegisterImage((ResourceID)i, res.PhysicalImage, res.PhysicalView);
            }

            // Buffer Handling
            if (res.Type == ResourceType::Buffer && res.PhysicalBuffer == VK_NULL_HANDLE)
            {
                RHI::VulkanBuffer* buf = ResolveBuffer(frameIndex, res);
                if (!buf || buf->GetHandle() == VK_NULL_HANDLE)
                {
                    Core::Log::Error("RenderGraph: failed to allocate transient buffer for resource {}", (uint32_t)i);
                    res.PhysicalBuffer = VK_NULL_HANDLE;
                    continue;
                }
                res.PhysicalBuffer = buf->GetHandle();
                m_Registry.RegisterBuffer((ResourceID)i, res.PhysicalBuffer);
            }
        }

        // 2. Barrier Calculation
        for (size_t passIdx = 0; passIdx < m_Passes.size(); ++passIdx)
        {
            const auto& pass = m_Passes[passIdx];

            // Iterate ALL accesses in this pass
            for (const auto& access : pass.Accesses)
            {
                auto& res = m_Resources[access.ID];
                
                bool needsBarrier = false;
                
                // --- IMAGE LOGIC ---
                if (res.Type == ResourceType::Texture || (res.Type == ResourceType::Import && res.PhysicalImage))
                {
                    VkImageLayout targetLayout = res.CurrentLayout;

                    // Infer optimal layout if not explicit
                    if (access.Access & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) targetLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    else if (access.Access & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) targetLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    else if (access.Access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    else if (access.Access & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) targetLayout = VK_IMAGE_LAYOUT_GENERAL;
                    else if (access.Access & VK_ACCESS_2_SHADER_STORAGE_READ_BIT) targetLayout = VK_IMAGE_LAYOUT_GENERAL;
                    else if (access.Access & VK_ACCESS_2_TRANSFER_WRITE_BIT) targetLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    else if (access.Access & VK_ACCESS_2_TRANSFER_READ_BIT) targetLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                    needsBarrier = (res.CurrentLayout != targetLayout) ||
                                   // If the previous usage produced a write, we must order it before any subsequent access.
                                   (res.LastUsageAccess & (VK_ACCESS_2_MEMORY_WRITE_BIT |
                                                         VK_ACCESS_2_SHADER_WRITE_BIT |
                                                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                                         VK_ACCESS_2_TRANSFER_WRITE_BIT |
                                                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) ||
                                   // If we're writing now, we must create an execution+memory dependency.
                                   (access.Access & (VK_ACCESS_2_MEMORY_WRITE_BIT |
                                                    VK_ACCESS_2_SHADER_WRITE_BIT |
                                                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                                    VK_ACCESS_2_TRANSFER_WRITE_BIT |
                                                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));

                    // If this is the first usage, we still need a barrier if we are changing layout away from UNDEFINED.
                    if (res.LastUsageStage == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT && res.LastUsageAccess == 0)
                    {
                        needsBarrier = (res.CurrentLayout != targetLayout);
                    }

                    if (needsBarrier)
                    {
                        VkImageMemoryBarrier2 barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                        barrier.image = res.PhysicalImage;
                        barrier.oldLayout = res.CurrentLayout;
                        barrier.newLayout = targetLayout;
                        barrier.srcStageMask = (res.CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                                   ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
                                                   : res.LastUsageStage;
                        barrier.srcAccessMask = (res.CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0 : res.LastUsageAccess;
                        barrier.dstStageMask = access.Stage;
                        barrier.dstAccessMask = access.Access;
                        barrier.subresourceRange.aspectMask = res.Aspect;
                        barrier.subresourceRange.baseMipLevel = 0;
                        barrier.subresourceRange.levelCount = 1;
                        barrier.subresourceRange.baseArrayLayer = 0;
                        barrier.subresourceRange.layerCount = 1;

                        m_Barriers[passIdx].ImageBarriers.push_back(barrier);

                        res.CurrentLayout = targetLayout;
                        res.LastUsageStage = access.Stage;
                        res.LastUsageAccess = access.Access;
                    }
                    else
                    {
                        res.LastUsageStage = access.Stage;
                        res.LastUsageAccess = access.Access;
                    }
                }
                // --- BUFFER LOGIC ---
                else if (res.Type == ResourceType::Buffer || (res.Type == ResourceType::Import && res.PhysicalBuffer))
                {
                    // Buffer Barriers check for execution dependencies and memory visibility
                    // If the previous usage was a WRITE, we ALWAYS need a barrier before READ or WRITE.
                    // If previous was READ and now is WRITE, we need a barrier (WAR).
                    // If previous was READ and now is READ, no barrier.

                    bool prevWrite = (res.LastUsageAccess & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));
                    bool currWrite = (access.Access & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));

                    if (prevWrite || currWrite)
                    {
                        // Optimization: Skip if it's the first usage
                        if (res.LastUsageStage != VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT)
                        {
                            VkBufferMemoryBarrier2 barrier{};
                            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                            barrier.buffer = res.PhysicalBuffer;
                            barrier.offset = 0;
                            barrier.size = VK_WHOLE_SIZE;
                            barrier.srcStageMask = res.LastUsageStage;
                            barrier.srcAccessMask = res.LastUsageAccess;
                            barrier.dstStageMask = access.Stage;
                            barrier.dstAccessMask = access.Access;

                            m_Barriers[passIdx].BufferBarriers.push_back(barrier);
                        }
                    }
                    
                    res.LastUsageStage = access.Stage;
                    res.LastUsageAccess = access.Access;
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

            if (pass.ExecuteFn)
            {
                pass.ExecuteFn(pass.ExecuteUserData, m_Registry, cmd);
            }

            if (isRaster)
            {
                vkCmdEndRendering(cmd);
            }
        }
    }
}


