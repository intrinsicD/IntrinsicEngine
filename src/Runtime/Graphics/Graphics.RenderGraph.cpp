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
        if (!resource.IsValid() || resource.ID >= m_Graph.m_ActiveResourceCount)
        {
            Core::Log::Error("RenderGraph: invalid resource handle in pass {}", m_PassIndex);
            return {kInvalidResource};
        }
        m_Graph.m_PassPool[m_PassIndex].Accesses.push_back({resource.ID, stage, access});
        // Extend lifetime
        auto& node = m_Graph.m_ResourcePool[resource.ID];
        if (node.StartPass == ~0u) node.StartPass = m_PassIndex;
        node.EndPass = std::max(node.EndPass, m_PassIndex);
        return resource;
    }

    RGResourceHandle RGBuilder::Write(RGResourceHandle resource, VkPipelineStageFlags2 stage, VkAccessFlags2 access)
    {
        if (!resource.IsValid() || resource.ID >= m_Graph.m_ActiveResourceCount)
        {
            Core::Log::Error("RenderGraph: invalid resource handle in pass {}", m_PassIndex);
            return {kInvalidResource};
        }
        m_Graph.m_PassPool[m_PassIndex].Accesses.push_back({resource.ID, stage, access});
        auto& node = m_Graph.m_ResourcePool[resource.ID];
        if (node.StartPass == ~0u) node.StartPass = m_PassIndex; // First write defines start
        node.EndPass = std::max(node.EndPass, m_PassIndex);
        return resource;
    }

    RGResourceHandle RGBuilder::WriteColor(RGResourceHandle resource, RGAttachmentInfo info)
    {
        // Raster write is implicitly Color Attachment Output
        Write(resource, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        m_Graph.m_PassPool[m_PassIndex].Attachments.push_back({resource.ID, info, false});
        return resource;
    }

    RGResourceHandle RGBuilder::WriteDepth(RGResourceHandle resource, RGAttachmentInfo info)
    {
        Write(resource, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        m_Graph.m_PassPool[m_PassIndex].Attachments.push_back({resource.ID, info, true});
        return resource;
    }

    RGResourceHandle RGBuilder::CreateTexture(Core::Hash::StringID name, const RGTextureDesc& desc)
    {
        auto [id, created] = m_Graph.CreateResourceInternal(name, ResourceType::Texture);
        if (created)
        {
            auto& node = m_Graph.m_ResourcePool[id];
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
            auto& node = m_Graph.m_ResourcePool[id];
            node.BufferSize = desc.Size;
            node.BufferUsage = desc.Usage;
        }
        return {id};
    }

    RGResourceHandle RGBuilder::ImportTexture(Core::Hash::StringID name, VkImage image, VkImageView view,
                                              VkFormat format,
                                              VkExtent2D extent, VkImageLayout currentLayout)
    {
        auto [id, created] = m_Graph.CreateResourceInternal(name, ResourceType::Import);
        if (created)
        {
            auto& node = m_Graph.m_ResourcePool[id];
            node.PhysicalImage = image;
            node.PhysicalView = view;
            node.InitialLayout = currentLayout;
            node.CurrentLayout = currentLayout;
            node.Extent = extent;
            node.Format = format;
            node.Aspect = (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format ==
                              VK_FORMAT_D32_SFLOAT)
                              ? (VkImageAspectFlags)(VK_IMAGE_ASPECT_DEPTH_BIT)
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
            auto& node = m_Graph.m_ResourcePool[id];
            node.PhysicalBuffer = buffer.GetHandle();
            m_Graph.m_Registry.RegisterBuffer(id, buffer.GetHandle());
            node.StartPass = 0;
            node.EndPass = 0;
        }
        return {id};
    }

    VkExtent2D RGBuilder::GetTextureExtent(RGResourceHandle handle) const
    {
        if (handle.ID < m_Graph.m_ActiveResourceCount)
        {
            return m_Graph.m_ResourcePool[handle.ID].Extent;
        }
        return {0, 0};
    }

    // --- RenderGraph ---
    RenderGraph::RenderGraph(std::shared_ptr<RHI::VulkanDevice> device,
                             Core::Memory::LinearArena& arena,
                             Core::Memory::ScopeStack& scope) :
        m_Device(device),
        m_Arena(arena),
        m_Scope(scope)
    {
    }

    RenderGraph::~RenderGraph()
    {
        vkDeviceWaitIdle(m_Device->GetLogicalDevice());
        m_ImagePool.clear();
        m_BufferPool.clear();
    }

    void RenderGraph::Trim()
    {
        // Caller is expected to have synchronized with the GPU (e.g., vkDeviceWaitIdle via renderer resize path).
        // We keep Trim() itself lightweight and deterministic.
        m_ImagePool.clear();
        m_BufferPool.clear();
        Core::Log::Info("RenderGraph: Pools trimmed.");
    }

    RenderGraph::RGPass& RenderGraph::CreatePassInternal(const std::string& name)
    {
        // Grow pool if needed
        if (m_ActivePassCount >= m_PassPool.size())
        {
            m_PassPool.resize(m_ActivePassCount + 1);
        }

        auto& pass = m_PassPool[m_ActivePassCount++];
        pass.Name = name;
        pass.ExecuteFn = nullptr;
        pass.ExecuteUserData = nullptr;
        // Accesses/Attachments are cleared during Reset() (capacity preserved)
        return pass;
    }

    std::pair<ResourceID, bool> RenderGraph::CreateResourceInternal(Core::Hash::StringID name, ResourceType type)
    {
        if (auto it = m_ResourceLookup.find(name); it != m_ResourceLookup.end())
        {
            return {it->second, false};
        }

        // Grow pool if needed
        if (m_ActiveResourceCount >= m_ResourcePool.size())
        {
            m_ResourcePool.resize(m_ActiveResourceCount + 1);
        }

        auto id = m_ActiveResourceCount++;
        auto& node = m_ResourcePool[id];
        node = ResourceNode{}; // reset all state
        node.Name = name;
        node.Type = type;

        m_ResourceLookup[name] = id;
        return {id, true};
    }

    void RenderGraph::Reset()
    {
        // 1) Reset ScopeStack (Destructors for closures run here)
        m_Scope.Reset();

        // 2) Reset Allocators (Pointers go back to 0, memory is NOT freed)
        m_Arena.Reset();

        // 3) Soft-clear the pools (preserve capacity)
        for (uint32_t i = 0; i < m_ActivePassCount; ++i)
        {
            m_PassPool[i].Accesses.clear();
            m_PassPool[i].Attachments.clear();
            m_PassPool[i].Name.clear();
            m_PassPool[i].ExecuteFn = nullptr;
            m_PassPool[i].ExecuteUserData = nullptr;
        }
        m_ActivePassCount = 0;

        // Resources are POD-like; just reset count
        m_ActiveResourceCount = 0;
        m_ResourceLookup.clear();

        // Registry accumulates per-frame physical bindings
        m_Registry = RGRegistry();

        // Barriers are per-pass; recycle per-batch vectors
        for (auto& batch : m_Barriers)
        {
            batch.ImageBarriers.clear();
            batch.BufferBarriers.clear();
        }
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

        auto img = std::make_unique<RHI::VulkanImage>(*m_Device, node.Extent.width, node.Extent.height, 1, node.Format,
                                                      node.Usage, node.Aspect);
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

        auto buf = std::make_unique<RHI::VulkanBuffer>(*m_Device, node.BufferSize, node.BufferUsage,
                                                       VMA_MEMORY_USAGE_GPU_ONLY);
        auto* ptr = buf.get();
        PooledBuffer pooled{std::move(buf), frameIndex};
        pooled.ActiveIntervals.emplace_back(node.StartPass, node.EndPass);
        stack.Buffers.push_back(std::move(pooled));
        return ptr;
    }

    void RenderGraph::Compile(uint32_t frameIndex)
    {
        // Resize barrier batches to match pass count if needed (capacity preserved)
        if (m_Barriers.size() < m_ActivePassCount)
        {
            m_Barriers.resize(m_ActivePassCount);
        }

        // Clear any previous barrier batches
        for (uint32_t i = 0; i < m_ActivePassCount; ++i)
        {
            m_Barriers[i].ImageBarriers.clear();
            m_Barriers[i].BufferBarriers.clear();
        }

        // 1. Resolve Transient Resources
        for (uint32_t i = 0; i < m_ActiveResourceCount; ++i)
        {
            auto& res = m_ResourcePool[i];

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
        for (uint32_t passIdx = 0; passIdx < m_ActivePassCount; ++passIdx)
        {
            const auto& pass = m_PassPool[passIdx];

            for (const auto& access : pass.Accesses)
            {
                auto& res = m_ResourcePool[access.ID];

                bool needsBarrier = false;

                // --- IMAGE LOGIC ---
                if (res.Type == ResourceType::Texture || (res.Type == ResourceType::Import && res.PhysicalImage))
                {
                    VkImageLayout targetLayout = res.CurrentLayout;

                    if (access.Access & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)
                        targetLayout =
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    else if (access.Access & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
                        targetLayout =
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    else if (access.Access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT)
                        targetLayout =
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    else if (access.Access & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)
                        targetLayout =
                            VK_IMAGE_LAYOUT_GENERAL;
                    else if (access.Access & VK_ACCESS_2_SHADER_STORAGE_READ_BIT)
                        targetLayout =
                            VK_IMAGE_LAYOUT_GENERAL;
                    else if (access.Access & VK_ACCESS_2_TRANSFER_WRITE_BIT)
                        targetLayout =
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    else if (access.Access & VK_ACCESS_2_TRANSFER_READ_BIT)
                        targetLayout =
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                    needsBarrier = (res.CurrentLayout != targetLayout) ||
                        (res.LastUsageAccess & (VK_ACCESS_2_MEMORY_WRITE_BIT |
                            VK_ACCESS_2_SHADER_WRITE_BIT |
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_2_TRANSFER_WRITE_BIT |
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) ||
                        (access.Access & (VK_ACCESS_2_MEMORY_WRITE_BIT |
                            VK_ACCESS_2_SHADER_WRITE_BIT |
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_2_TRANSFER_WRITE_BIT |
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));

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
                        barrier.srcAccessMask = (res.CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                                    ? 0
                                                    : res.LastUsageAccess;
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
                    bool prevWrite = (res.LastUsageAccess & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT
                        | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));
                    bool currWrite = (access.Access & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT |
                        VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));

                    if (prevWrite || currWrite)
                    {
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
        std::vector<VkRenderingAttachmentInfo> colorAtts;
        colorAtts.reserve(m_PassPool[0].Attachments.size());
        for (uint32_t i = 0; i < m_ActivePassCount; ++i)
        {
            const auto& pass = m_PassPool[i];

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
                colorAtts.reserve(pass.Attachments.size());
                colorAtts.clear();
                VkRenderingAttachmentInfo depthAtt{};
                bool hasDepth = false;
                VkExtent2D renderArea = {0, 0};

                for (const auto& att : pass.Attachments)
                {
                    auto& res = m_ResourcePool[att.ID];
                    if (renderArea.width == 0 && renderArea.height == 0)
                        renderArea = res.Extent;
                    else if (renderArea.width != res.Extent.width || renderArea.height != res.Extent.height)
                        Core::Log::Error("RenderGraph: attachment extents mismatch in pass {}", pass.Name);

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
