// Graphics.RenderGraph.cpp
module;
#include "RHI.Vulkan.hpp"
#include <string>
#include <vector>
#include <memory>
#include <span>
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
    template <typename NodeT>
    void AppendToList(Core::Memory::LinearArena& arena, NodeT*& head, NodeT*& tail, NodeT&& value)
    {
        // Allocate node in the frame arena (Zero overhead)
        auto nodeMem = arena.New<NodeT>(std::move(value));
        if (!nodeMem) return; // OOM check

        NodeT* node = *nodeMem;
        node->Next = nullptr;

        if (tail)
        {
            tail->Next = node;
            tail = node;
        }
        else
        {
            head = tail = node;
        }
    }

    RGResourceHandle RGBuilder::Read(RGResourceHandle resource, VkPipelineStageFlags2 stage, VkAccessFlags2 access)
    {
        if (!resource.IsValid() || resource.ID >= m_Graph.m_ActiveResourceCount)
        {
            Core::Log::Error("RG: Invalid resource handle");
            return {kInvalidResource};
        }

        AccessNode node{resource.ID, stage, access, nullptr};
        AppendToList(m_Graph.m_Arena,
                     m_Graph.m_PassPool[m_PassIndex].AccessHead,
                     m_Graph.m_PassPool[m_PassIndex].AccessTail,
                     std::move(node));

        auto& resNode = m_Graph.m_ResourcePool[resource.ID];
        if (resNode.StartPass == ~0u) resNode.StartPass = m_PassIndex;
        resNode.EndPass = std::max(resNode.EndPass, m_PassIndex);
        return resource;
    }

    RGResourceHandle RGBuilder::Write(RGResourceHandle resource, VkPipelineStageFlags2 stage, VkAccessFlags2 access)
    {
        if (!resource.IsValid() || resource.ID >= m_Graph.m_ActiveResourceCount)
        {
            Core::Log::Error("RG: Invalid resource handle");
            return {kInvalidResource};
        }

        AccessNode node{resource.ID, stage, access, nullptr};
        AppendToList(m_Graph.m_Arena,
                     m_Graph.m_PassPool[m_PassIndex].AccessHead,
                     m_Graph.m_PassPool[m_PassIndex].AccessTail,
                     std::move(node));

        auto& resNode = m_Graph.m_ResourcePool[resource.ID];
        if (resNode.StartPass == ~0u) resNode.StartPass = m_PassIndex;
        resNode.EndPass = std::max(resNode.EndPass, m_PassIndex);
        return resource;
    }

    RGResourceHandle RGBuilder::WriteColor(RGResourceHandle resource, RGAttachmentInfo info)
    {
        Write(resource, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        AttachmentNode node{resource.ID, info, false, nullptr};
        AppendToList(m_Graph.m_Arena,
                     m_Graph.m_PassPool[m_PassIndex].AttachmentHead,
                     m_Graph.m_PassPool[m_PassIndex].AttachmentTail,
                     std::move(node));
        return resource;
    }

    RGResourceHandle RGBuilder::WriteDepth(RGResourceHandle resource, RGAttachmentInfo info)
    {
        Write(resource, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

        AttachmentNode node{resource.ID, info, true, nullptr};
        AppendToList(m_Graph.m_Arena,
                     m_Graph.m_PassPool[m_PassIndex].AttachmentHead,
                     m_Graph.m_PassPool[m_PassIndex].AttachmentTail,
                     std::move(node));
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
        // 1. Reset Allocators (Reclaim memory)
        m_Scope.Reset();
        m_Arena.Reset();

        // 2. Soft-reset passes
        for (uint32_t i = 0; i < m_ActivePassCount; ++i)
        {
            auto& pass = m_PassPool[i];
            pass.AccessHead = nullptr;
            pass.AccessTail = nullptr;
            pass.AttachmentHead = nullptr;
            pass.AttachmentTail = nullptr;
            pass.ImageBarriers = {};
            pass.BufferBarriers = {};
            pass.ExecuteFn = nullptr;
            pass.ExecuteUserData = nullptr;
        }
        m_ActivePassCount = 0;

        // 3. Reset resources
        m_ActiveResourceCount = 0;
        m_ResourceLookup.clear();
        m_Registry = RGRegistry();
    }

    RHI::VulkanImage* RenderGraph::ResolveImage(uint32_t frameIndex, const ResourceNode& node)
    {
        // 1. Create the Handle (Cheap, no memory yet)
        // Note: Ensure RHI::VulkanImage has a move constructor!
        auto unboundImg = RHI::VulkanImage::CreateUnbound(*m_Device, node.Extent.width, node.Extent.height, node.Format, node.Usage);

        // 2. Query Requirements
        VkMemoryRequirements reqs = unboundImg.GetMemoryRequirements();

        // 3. Find Memory (The "Refinement")
        // This returns a raw VkDeviceMemory handle from your pool
        VkDeviceMemory memory = AllocateOrReuseMemory(reqs, node.StartPass, node.EndPass, frameIndex);

        // 4. Bind
        unboundImg.BindMemory(memory, 0);

        // 5. Move to Frame Scope
        // We move the 'unboundImg' object (which is now fully bound) into the arena.
        // The ScopeStack will call the destructor of this object at the end of the frame.
        auto allocation = m_Scope.New<RHI::VulkanImage>(std::move(unboundImg));

        if (!allocation)
        {
            Core::Log::Error("RenderGraph: Failed to allocate image wrapper in ScopeStack (OOM)");
            return nullptr;
        }

        // Dereference std::expected to get the raw pointer
        return *allocation;
    }

    RHI::VulkanBuffer* RenderGraph::ResolveBuffer(uint32_t frameIndex, const ResourceNode& node)
    {
        BufferCacheKey key{node.BufferSize, node.BufferUsage};
        auto& stack = m_BufferPool[key];

        uint64_t globalFrame = m_Device->GetGlobalFrameNumber();

        for (auto& item : stack.Buffers)
        {
            if (item.LastFrameIndex != frameIndex) continue;

            if (item.LastUsedGlobalFrame != globalFrame)
            {
                item.LastUsedGlobalFrame = globalFrame;
                item.ActiveIntervals.clear();
                item.ActiveIntervals.emplace_back(node.StartPass, node.EndPass);
                return item.Resource.get();
            }

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

        auto buf = std::make_unique<RHI::VulkanBuffer>(*m_Device, node.BufferSize, node.BufferUsage,
                                                       VMA_MEMORY_USAGE_GPU_ONLY);
        auto* ptr = buf.get();
        PooledBuffer pooled{std::move(buf), frameIndex, globalFrame};
        pooled.ActiveIntervals.emplace_back(node.StartPass, node.EndPass);
        stack.Buffers.push_back(std::move(pooled));
        return ptr;
    }

    VkDeviceMemory RenderGraph::AllocateOrReuseMemory(const VkMemoryRequirements& reqs, uint32_t startPass,
                                                      uint32_t endPass, uint32_t frameIndex)
    {
        // 1. Look for a compatible bucket (Memory Type Bits)
        auto& bucket = m_MemoryPool[reqs.memoryTypeBits];

        uint64_t globalFrame = m_Device->GetGlobalFrameNumber();

        for (auto& chunk : bucket)
        {
            // Must match frame index (frames in flight isolation)
            if (chunk->LastFrameIndex != frameIndex) continue;

            // Reset if stale (used in previous frames)
            if (chunk->LastUsedGlobalFrame != globalFrame)
            {
                chunk->LastUsedGlobalFrame = globalFrame;
                chunk->AllocatedIntervals.clear();
            }

            // Strict Size & Alignment Check
            // Note: We currently support 1 image per chunk for simplicity (Heap Allocator).
            // A full implementation would use a 'LinearAllocator' logic here to put multiple images in one chunk.
            if (chunk->Size < reqs.size) continue;

            // Lifetime Overlap Check
            bool overlaps = false;
            for (const auto& interval : chunk->AllocatedIntervals)
            {
                // Simple 1D interval intersection: (StartA <= EndB) and (EndA >= StartB)
                if (startPass <= interval.second && endPass >= interval.first)
                {
                    overlaps = true;
                    break;
                }
            }

            if (!overlaps)
            {
                // Found a free spot! "Alias" this memory.
                chunk->AllocatedIntervals.emplace_back(startPass, endPass);
                return chunk->Memory;
            }
        }

        // 2. No suitable chunk found, allocate new one via VMA (or raw Vulkan)
        // Here we use a raw allocation for simplicity of the example, but in production use VMA.
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = reqs.size;

        // Find memory type index
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(m_Device->GetPhysicalDevice(), &memProps);

        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        {
            if ((reqs.memoryTypeBits & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                allocInfo.memoryTypeIndex = i;
                break;
            }
        }

        VkDeviceMemory newMemory;
        vkAllocateMemory(m_Device->GetLogicalDevice(), &allocInfo, nullptr, &newMemory);

        auto newChunk = std::make_unique<MemoryChunk>();
        newChunk->Memory = newMemory;
        newChunk->Size = reqs.size;
        newChunk->MemoryTypeBits = reqs.memoryTypeBits;
        newChunk->LastFrameIndex = frameIndex;
        newChunk->LastUsedGlobalFrame = globalFrame;
        newChunk->AllocatedIntervals.emplace_back(startPass, endPass);

        VkDeviceMemory result = newMemory;
        bucket.push_back(std::move(newChunk));

        return result;
    }

    void RenderGraph::Compile(uint32_t frameIndex)
    {
        // 1. Resolve Transient Resources (Unchanged)
        for (uint32_t i = 0; i < m_ActiveResourceCount; ++i)
        {
            auto& res = m_ResourcePool[i];
            if (res.Type == ResourceType::Import)
            {
                res.CurrentLayout = res.InitialLayout;
                res.LastUsageStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                res.LastUsageAccess = 0;
            }
            if (res.Type == ResourceType::Texture && res.PhysicalImage == VK_NULL_HANDLE)
            {
                RHI::VulkanImage* img = ResolveImage(frameIndex, res);
                if (img && img->IsValid())
                {
                    res.PhysicalImage = img->GetHandle();
                    res.PhysicalView = img->GetView();
                    res.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    res.InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    m_Registry.RegisterImage((ResourceID)i, res.PhysicalImage, res.PhysicalView);
                }
            }
            if (res.Type == ResourceType::Buffer && res.PhysicalBuffer == VK_NULL_HANDLE)
            {
                RHI::VulkanBuffer* buf = ResolveBuffer(frameIndex, res);
                if (buf && buf->GetHandle() != VK_NULL_HANDLE)
                {
                    res.PhysicalBuffer = buf->GetHandle();
                    m_Registry.RegisterBuffer((ResourceID)i, res.PhysicalBuffer);
                }
            }
        }

        // 2. Barrier Calculation (Refactored for Arena Spans)
        for (uint32_t passIdx = 0; passIdx < m_ActivePassCount; ++passIdx)
        {
            auto& pass = m_PassPool[passIdx];

            // --- Pass 1: Image Barriers ---
            VkImageMemoryBarrier2* imgStart = nullptr;
            size_t imgCount = 0;

            for (AccessNode* node = pass.AccessHead; node != nullptr; node = node->Next)
            {
                auto& res = m_ResourcePool[node->ID];
                if (res.Type == ResourceType::Buffer) continue;
                // Treat imported buffers as buffers, imported images as images
                if (res.Type == ResourceType::Import && res.PhysicalBuffer) continue;

                // --- Image Barrier Logic ---
                VkImageLayout targetLayout = res.CurrentLayout;
                if (node->Access & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) targetLayout =
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                else if (node->Access & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) targetLayout =
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                else if (node->Access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) targetLayout =
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                else if (node->Access & (VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT))
                    targetLayout = VK_IMAGE_LAYOUT_GENERAL;
                else if (node->Access & VK_ACCESS_2_TRANSFER_WRITE_BIT) targetLayout =
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                else if (node->Access & VK_ACCESS_2_TRANSFER_READ_BIT) targetLayout =
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                bool needsBarrier = (res.CurrentLayout != targetLayout) ||
                    (res.LastUsageAccess & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT |
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) ||
                    (node->Access & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT |
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));

                if (res.LastUsageStage == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT && res.LastUsageAccess == 0)
                    needsBarrier = (res.CurrentLayout != targetLayout);

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
                    barrier.dstStageMask = node->Stage;
                    barrier.dstAccessMask = node->Access;
                    barrier.subresourceRange.aspectMask = res.Aspect;
                    barrier.subresourceRange.baseMipLevel = 0;
                    barrier.subresourceRange.levelCount = 1;
                    barrier.subresourceRange.baseArrayLayer = 0;
                    barrier.subresourceRange.layerCount = 1;

                    // Allocate in Arena
                    auto alloc = m_Arena.New<VkImageMemoryBarrier2>(barrier);
                    if (alloc)
                    {
                        if (imgCount == 0) imgStart = *alloc;
                        imgCount++;
                    }

                    res.CurrentLayout = targetLayout;
                    res.LastUsageStage = node->Stage;
                    res.LastUsageAccess = node->Access;
                }
                else
                {
                    res.LastUsageStage = node->Stage;
                    res.LastUsageAccess = node->Access;
                }
            }
            if (imgCount > 0) pass.ImageBarriers = std::span<VkImageMemoryBarrier2>(imgStart, imgCount);

            // --- Pass 2: Buffer Barriers ---
            VkBufferMemoryBarrier2* bufStart = nullptr;
            size_t bufCount = 0;

            for (AccessNode* node = pass.AccessHead; node != nullptr; node = node->Next)
            {
                auto& res = m_ResourcePool[node->ID];
                bool isBuffer = (res.Type == ResourceType::Buffer) || (res.Type == ResourceType::Import && res.
                    PhysicalBuffer);
                if (!isBuffer) continue;

                bool prevWrite = (res.LastUsageAccess & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT |
                    VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));
                bool currWrite = (node->Access & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT |
                    VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));

                if ((prevWrite || currWrite) && res.LastUsageStage != VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT)
                {
                    VkBufferMemoryBarrier2 barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                    barrier.buffer = res.PhysicalBuffer;
                    barrier.offset = 0;
                    barrier.size = VK_WHOLE_SIZE;
                    barrier.srcStageMask = res.LastUsageStage;
                    barrier.srcAccessMask = res.LastUsageAccess;
                    barrier.dstStageMask = node->Stage;
                    barrier.dstAccessMask = node->Access;

                    auto alloc = m_Arena.New<VkBufferMemoryBarrier2>(barrier);
                    if (alloc)
                    {
                        if (bufCount == 0) bufStart = *alloc;
                        bufCount++;
                    }
                }
                res.LastUsageStage = node->Stage;
                res.LastUsageAccess = node->Access;
            }
            if (bufCount > 0) pass.BufferBarriers = std::span<VkBufferMemoryBarrier2>(bufStart, bufCount);
        }
    }

    void RenderGraph::Execute(VkCommandBuffer cmd)
    {
        for (uint32_t i = 0; i < m_ActivePassCount; ++i)
        {
            const auto& pass = m_PassPool[i];

            // 1. Pipeline Barriers (from Spans)
            if (!pass.ImageBarriers.empty() || !pass.BufferBarriers.empty())
            {
                VkDependencyInfo depInfo{};
                depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                depInfo.imageMemoryBarrierCount = (uint32_t)pass.ImageBarriers.size();
                depInfo.pImageMemoryBarriers = pass.ImageBarriers.data();
                depInfo.bufferMemoryBarrierCount = (uint32_t)pass.BufferBarriers.size();
                depInfo.pBufferMemoryBarriers = pass.BufferBarriers.data();
                vkCmdPipelineBarrier2(cmd, &depInfo);
            }

            // 2. Rendering (Attachments)
            bool isRaster = (pass.AttachmentHead != nullptr);
            if (isRaster)
            {
                // Local vector for attachments (max 8 usually, so low allocation overhead or use scratch arena)
                std::vector<VkRenderingAttachmentInfo> colorAtts;
                colorAtts.reserve(8);
                VkRenderingAttachmentInfo depthAtt{};
                bool hasDepth = false;
                VkExtent2D renderArea = {0, 0};

                for (AttachmentNode* att = pass.AttachmentHead; att != nullptr; att = att->Next)
                {
                    auto& res = m_ResourcePool[att->ID];
                    if (renderArea.width == 0 && renderArea.height == 0) renderArea = res.Extent;

                    VkRenderingAttachmentInfo info{};
                    info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    info.imageView = res.PhysicalView;
                    info.imageLayout = att->IsDepth
                                           ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                           : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    info.loadOp = att->Info.LoadOp;
                    info.storeOp = att->Info.StoreOp;
                    info.clearValue = att->Info.ClearValue;

                    if (att->IsDepth)
                    {
                        depthAtt = info;
                        hasDepth = true;
                    }
                    else { colorAtts.push_back(info); }
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

            // 3. User Callback
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

    std::vector<RenderGraphDebugPass> RenderGraph::BuildDebugPassList() const
    {
        std::vector<RenderGraphDebugPass> out;
        out.reserve(m_ActivePassCount);

        for (uint32_t i = 0; i < m_ActivePassCount; ++i)
        {
            const auto& p = m_PassPool[i];
            RenderGraphDebugPass dp{};
            dp.Name = p.Name.c_str();
            dp.PassIndex = i;

            for (AttachmentNode* att = p.AttachmentHead; att != nullptr; att = att->Next)
            {
                if (att->ID >= m_ResourcePool.size()) continue;
                const auto& res = m_ResourcePool[att->ID];
                dp.Attachments.push_back({res.Name, att->ID, att->IsDepth});
            }
            out.push_back(std::move(dp));
        }
        return out;
    }

    std::vector<RenderGraphDebugImage> RenderGraph::BuildDebugImageList() const
    {
        std::vector<RenderGraphDebugImage> out;
        out.reserve(m_ActiveResourceCount);

        for (uint32_t i = 0; i < m_ActiveResourceCount; ++i)
        {
            const auto& res = m_ResourcePool[i];
            if (res.Type != ResourceType::Texture && res.Type != ResourceType::Import)
                continue;

            RenderGraphDebugImage di{};
            di.Name = res.Name;
            di.Resource = i;
            di.Extent = res.Extent;
            di.Format = res.Format;
            di.Usage = res.Usage;
            di.Aspect = res.Aspect;
            di.CurrentLayout = res.CurrentLayout;
            di.Image = res.PhysicalImage;
            di.View = res.PhysicalView;
            di.StartPass = res.StartPass;
            di.EndPass = res.EndPass;

            out.push_back(di);
        }

        return out;
    }
}
