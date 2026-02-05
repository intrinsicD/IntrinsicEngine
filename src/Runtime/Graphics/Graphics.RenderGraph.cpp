// Graphics.RenderGraph.cpp
module;
#include "RHI.Vulkan.hpp"
#include <string>
#include <vector>
#include <memory>
#include <span>
#include <algorithm>
#include <utility>

module Graphics:RenderGraph.Impl;
import :RenderGraph;
import Core;
import RHI;

namespace Graphics
{
    // Remove file-local transient allocator state; lifetime is managed by RenderSystem.

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
        auto nodeMem = arena.New<NodeT>(std::forward<NodeT>(value));
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

        // NOTE:
        // RenderGraph no longer owns VkDeviceMemory when backed by the RHI::TransientAllocator.
        // Multiple logical MemoryChunks may point into the same VkDeviceMemory page, so freeing
        // per-chunk would double-free and trigger validation errors.
        m_MemoryPool.clear();

        m_ImagePool.clear();
        m_BufferPool.clear();
    }

    void RenderGraph::Trim()
    {
        // Caller is expected to have synchronized with the GPU (e.g., vkDeviceWaitIdle via renderer resize path).
        // We keep Trim() itself lightweight and deterministic.

        // NOTE:
        // With TransientAllocator backing, RenderGraph's memory pool is just lifetime metadata.
        // Physical memory is owned by RHI::TransientAllocator.
        m_MemoryPool.clear();

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

        // Reset transient GPU pages too (bump pointers only; no frees)
        if (m_TransientAllocator)
        {
            m_TransientAllocator->Reset();
        }

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
        auto unboundImg = RHI::VulkanImage::CreateUnbound(*m_Device, node.Extent.width, node.Extent.height, node.Format, node.Usage);

        // 2. Query Requirements
        VkMemoryRequirements reqs = unboundImg.GetMemoryRequirements();

        // 3. Find Memory
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;

        // AllocateOrReuseMemory now returns the memory handle but also stores the sub-offset in the chunk.
        // For now we bind at offset 0 unless we find a chunk with non-zero BaseOffset.
        VkDeviceMemory memHandle = AllocateOrReuseMemory(reqs, node.StartPass, node.EndPass, frameIndex);
        memory = memHandle;

        // NOTE: our current API returns only VkDeviceMemory; ResolveImage binds offset=0.
        // We upgrade by looking up the last chunk we just allocated in the pool.
        // This is safe because AllocateOrReuseMemory always pushes the new chunk at the end.
        if (memory != VK_NULL_HANDLE)
        {
            auto& bucket = m_MemoryPool[reqs.memoryTypeBits];
            if (!bucket.empty())
            {
                // Find the chunk matching this memory for current frame.
                for (auto it = bucket.rbegin(); it != bucket.rend(); ++it)
                {
                    if ((*it)->Memory == memory && (*it)->LastFrameIndex == frameIndex)
                    {
                        offset = (*it)->BaseOffset;
                        break;
                    }
                }
            }
        }

        // 4. Bind with offset
        unboundImg.BindMemory(memory, offset);

        // 5. Move to Frame Scope
        auto allocation = m_Scope.New<RHI::VulkanImage>(std::move(unboundImg));
        if (!allocation)
        {
            Core::Log::Error("RenderGraph: Failed to allocate image wrapper in ScopeStack (OOM)");
            return nullptr;
        }
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

        PooledBuffer pooled{std::move(buf), frameIndex, globalFrame};
        pooled.ActiveIntervals.emplace_back(node.StartPass, node.EndPass);
        stack.Buffers.push_back(std::move(pooled));
        return stack.Buffers.back().Resource.get();
    }

    VkDeviceMemory RenderGraph::AllocateOrReuseMemory(const VkMemoryRequirements& reqs, uint32_t startPass,
                                                      uint32_t endPass, uint32_t frameIndex)
    {
        // 1. Look for a compatible bucket (Memory Type Bits)
        auto& bucket = m_MemoryPool[reqs.memoryTypeBits];

        uint64_t globalFrame = m_Device->GetGlobalFrameNumber();

        for (auto& chunk : bucket)
        {
            if (chunk->LastFrameIndex != frameIndex) continue;

            if (chunk->LastUsedGlobalFrame != globalFrame)
            {
                chunk->LastUsedGlobalFrame = globalFrame;
                chunk->AllocatedIntervals.clear();
            }

            if (chunk->Size < reqs.size) continue;

            bool overlaps = false;
            for (const auto& interval : chunk->AllocatedIntervals)
            {
                if (startPass <= interval.second && endPass >= interval.first)
                {
                    overlaps = true;
                    break;
                }
            }

            if (!overlaps)
            {
                chunk->AllocatedIntervals.emplace_back(startPass, endPass);
                return chunk->Memory;
            }
        }

        // 2. No suitable chunk found -> allocate via transient page allocator.
        if (!m_TransientAllocator)
        {
            Core::Log::Error("RenderGraph: TransientAllocator not set. Call RenderGraph::SetTransientAllocator().");
            return VK_NULL_HANDLE;
        }

        auto allocation = m_TransientAllocator->Allocate(reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (!allocation.IsValid())
        {
            Core::Log::Error("RenderGraph: Failed to allocate transient memory.");
            return VK_NULL_HANDLE;
        }

        auto newChunk = std::make_unique<MemoryChunk>();
        newChunk->Memory = allocation.Memory;
        newChunk->Size = allocation.Size;
        newChunk->BaseOffset = allocation.Offset;
        newChunk->MemoryTypeBits = reqs.memoryTypeBits;
        newChunk->LastFrameIndex = frameIndex;
        newChunk->LastUsedGlobalFrame = globalFrame;
        newChunk->AllocatedIntervals.emplace_back(startPass, endPass);

        VkDeviceMemory result = newChunk->Memory;
        bucket.push_back(std::move(newChunk));

        return result;
    }

    void RenderGraph::Compile(uint32_t frameIndex)
    {
        m_CompiledFrameIndex = frameIndex;

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

            auto pushImageBarrier = [&](ResourceNode& res,
                                        VkPipelineStageFlags2 dstStage,
                                        VkAccessFlags2 dstAccess,
                                        VkImageLayout targetLayout)
            {
                // Imported buffers are handled in buffer barrier pass, not here.
                if (res.Type == ResourceType::Buffer) return;
                if (res.Type == ResourceType::Import && res.PhysicalBuffer) return;

                const bool prevWrite = (res.LastUsageAccess & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT |
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                    VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));
                const bool currWrite = (dstAccess & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT |
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                    VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));
                const bool layoutMismatch = (res.CurrentLayout != targetLayout);
                const bool isInitial = (res.LastUsageStage == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT && res.LastUsageAccess == 0);

                bool needsBarrier = layoutMismatch || prevWrite || currWrite;
                if (isInitial)
                    needsBarrier = layoutMismatch;

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
                    barrier.dstStageMask = dstStage;
                    barrier.dstAccessMask = dstAccess;
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
                    res.LastUsageStage = dstStage;
                    res.LastUsageAccess = dstAccess;
                }
                else
                {
                    res.LastUsageStage |= dstStage;
                    res.LastUsageAccess |= dstAccess;
                }
            };

            // 1a) Attachment usages are implicit accesses; they MUST participate in layout tracking.
            // IMPORTANT: Do this BEFORE iterating AccessHead. Otherwise, a pass that both renders to an image
            // and later copies/samples it (common for debug/picking) can end up with its *final* tracked layout
            // being TRANSFER_* which is invalid for vkCmdBeginRendering.
            for (AttachmentNode* att = pass.AttachmentHead; att != nullptr; att = att->Next)
            {
                if (att->ID >= m_ActiveResourceCount) continue;

                auto& res = m_ResourcePool[att->ID];

                if (att->IsDepth)
                {
                    pushImageBarrier(res,
                                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                }
                else
                {
                    pushImageBarrier(res,
                                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                }
            }

            // 1b) Explicit resource accesses.
            for (AccessNode* node = pass.AccessHead; node != nullptr; node = node->Next)
            {
                auto& res = m_ResourcePool[node->ID];

                // --- Image Barrier Logic ---
                // IMPORTANT: choose layout by looking for *specific* access intents.
                // Also: a node can contain multiple bits (e.g. TRANSFER_READ | MEMORY_WRITE to force hazard tracking).
                // In that case we still want the layout implied by the real operation (TRANSFER_READ/WRITE, ATTACHMENT, etc.).
                VkImageLayout targetLayout = res.CurrentLayout;

                if ((node->Access & (VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)) != 0)
                {
                    targetLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                }
                else if ((node->Access & (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
                {
                    targetLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                }
                else if ((node->Access & VK_ACCESS_2_TRANSFER_WRITE_BIT) != 0)
                {
                    targetLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                }
                else if ((node->Access & VK_ACCESS_2_TRANSFER_READ_BIT) != 0)
                {
                    targetLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                }
                else if ((node->Access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) != 0)
                {
                    // NOTE: This is a non-core extension-ish convenience bit in some codebases.
                    // If it doesn't exist on this SDK, sampling should come through SHADER_READ or UNIFORM_READ.
                    targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
                else if ((node->Access & (VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT)) != 0)
                {
                    targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
                else if ((node->Access & (VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT)) != 0)
                {
                    targetLayout = VK_IMAGE_LAYOUT_GENERAL;
                }

                pushImageBarrier(res, node->Stage, node->Access, targetLayout);
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

        // 3. Dependency analysis (DAG build + layering for parallel recording)
        BuildAdjacencyList();
        TopologicalSortIntoLayers();
    }

    namespace
    {
        [[nodiscard]] inline bool IsWriteAccess(VkAccessFlags2 a) noexcept
        {
            // Conservative: any write bit means write.
            return (a & (VK_ACCESS_2_MEMORY_WRITE_BIT |
                         VK_ACCESS_2_SHADER_WRITE_BIT |
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_2_TRANSFER_WRITE_BIT |
                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | // already implies shader write but keep explicit
                         VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR)) != 0;
        }
    }

    void RenderGraph::BuildAdjacencyList()
    {
        m_AdjacencyList.clear();
        m_AdjacencyList.resize(m_ActivePassCount);

        for (auto& d : m_AdjacencyList)
        {
            d.DependsOn.clear();
            d.Dependents.clear();
            d.Indegree = 0;
        }

        // Track last writer and last readers per resource. This is enough to model RAW/WAR/WAW.
        const uint32_t passInvalid = ~0u;
        std::vector<uint32_t> lastWriter(m_ActiveResourceCount, passInvalid);
        std::vector<std::vector<uint32_t>> lastReaders(m_ActiveResourceCount);

        auto addEdge = [&](uint32_t from, uint32_t to)
        {
            if (from == passInvalid || to == passInvalid || from == to) return;
            m_AdjacencyList[from].Dependents.push_back(to);
            m_AdjacencyList[to].DependsOn.push_back(from);
            m_AdjacencyList[to].Indegree++;
        };

        for (uint32_t passIdx = 0; passIdx < m_ActivePassCount; ++passIdx)
        {
            const auto& pass = m_PassPool[passIdx];

            // Access hazards.
            for (AccessNode* node = pass.AccessHead; node != nullptr; node = node->Next)
            {
                if (node->ID >= m_ActiveResourceCount) continue;

                const bool isWrite = IsWriteAccess(node->Access);
                const bool isRead = !isWrite; // treat all non-write accesses as read

                if (isRead)
                {
                    // RAW: depend on last writer.
                    addEdge(lastWriter[node->ID], passIdx);
                    lastReaders[node->ID].push_back(passIdx);
                }
                else
                {
                    // WAW: depend on last writer.
                    addEdge(lastWriter[node->ID], passIdx);

                    // WAR: depend on all outstanding readers since last write.
                    for (uint32_t r : lastReaders[node->ID])
                        addEdge(r, passIdx);
                    lastReaders[node->ID].clear();

                    lastWriter[node->ID] = passIdx;
                }
            }

            // Attachment hazards: treat as writes (raster). This ensures a pass that renders to an image
            // correctly serializes with later reads/writes even if user forgot to declare explicit Access.
            for (AttachmentNode* att = pass.AttachmentHead; att != nullptr; att = att->Next)
            {
                if (att->ID >= m_ActiveResourceCount) continue;

                // WAW + WAR with prior users.
                addEdge(lastWriter[att->ID], passIdx);
                for (uint32_t r : lastReaders[att->ID])
                    addEdge(r, passIdx);
                lastReaders[att->ID].clear();
                lastWriter[att->ID] = passIdx;
            }
        }
    }

    void RenderGraph::TopologicalSortIntoLayers()
    {
        m_ExecutionLayers.clear();
        if (m_ActivePassCount == 0) return;

        std::vector<uint32_t> indeg(m_ActivePassCount);
        for (uint32_t i = 0; i < m_ActivePassCount; ++i)
            indeg[i] = m_AdjacencyList[i].Indegree;

        std::vector<uint32_t> layer;
        layer.reserve(m_ActivePassCount);

        for (uint32_t i = 0; i < m_ActivePassCount; ++i)
            if (indeg[i] == 0) layer.push_back(i);

        uint32_t processed = 0;
        while (!layer.empty())
        {
            m_ExecutionLayers.push_back(layer);
            processed += static_cast<uint32_t>(layer.size());

            std::vector<uint32_t> next;
            next.reserve(m_ActivePassCount);

            for (uint32_t p : layer)
            {
                for (uint32_t dep : m_AdjacencyList[p].Dependents)
                {
                    if (dep >= m_ActivePassCount) continue;
                    if (indeg[dep] == 0) continue; // already scheduled
                    indeg[dep]--;
                    if (indeg[dep] == 0) next.push_back(dep);
                }
            }

            layer = std::move(next);
        }

        if (processed != m_ActivePassCount)
        {
            // Cycle: should not happen. Fall back to sequential order for safety.
            Core::Log::Error("RenderGraph: dependency cycle detected (processed {} / {}). Falling back to sequential execution order.",
                             processed, m_ActivePassCount);
            m_ExecutionLayers.clear();
            m_ExecutionLayers.emplace_back();
            m_ExecutionLayers.back().reserve(m_ActivePassCount);
            for (uint32_t i = 0; i < m_ActivePassCount; ++i) m_ExecutionLayers.back().push_back(i);
        }
    }

    void RenderGraph::Execute(VkCommandBuffer cmd)
    {
        // If Compile() wasn't called (or for safety), build layers lazily.
        if (m_ExecutionLayers.empty() && m_ActivePassCount > 0)
        {
            BuildAdjacencyList();
            TopologicalSortIntoLayers();
        }

        // Record each layer in parallel into secondary command buffers.
        // GPU execution order remains: layers in order; passes within a layer in vector order.
        for (const auto& layer : m_ExecutionLayers)
        {
            if (layer.empty()) continue;

            // Precompute inheritance info per pass (stable pointers for the worker tasks).
            struct RasterInfo
            {
                bool IsRaster = false;
                std::vector<VkFormat> ColorFormats;
                VkFormat Depth = VK_FORMAT_UNDEFINED;
                VkFormat Stencil = VK_FORMAT_UNDEFINED;
            };

            std::vector<RasterInfo> rasterInfos;
            rasterInfos.resize(layer.size());

            for (size_t i = 0; i < layer.size(); ++i)
            {
                const auto& pass = m_PassPool[layer[i]];
                RasterInfo ri{};
                ri.IsRaster = (pass.AttachmentHead != nullptr);
                if (ri.IsRaster)
                {
                    for (AttachmentNode* att = pass.AttachmentHead; att != nullptr; att = att->Next)
                    {
                        const auto& res = m_ResourcePool[att->ID];
                        if (att->IsDepth)
                        {
                            ri.Depth = res.Format;
                        }
                        else
                        {
                            ri.ColorFormats.push_back(res.Format);
                        }
                    }
                }
                rasterInfos[i] = std::move(ri);
            }

            std::vector<VkCommandBuffer> secondaryCmds(layer.size(), VK_NULL_HANDLE);

            // Dispatch one task per pass. This is fine for now; we can chunk later.
            for (size_t i = 0; i < layer.size(); ++i)
            {
                const uint32_t passIdx = layer[i];

                Core::Tasks::Scheduler::Dispatch([this, passIdx, &secondaryCmds, i, &rasterInfos]
                {
                    auto& pass = m_PassPool[passIdx];
                    const bool isRaster = (pass.AttachmentHead != nullptr);

                    RHI::SecondaryInheritanceInfo inherit{};
                    if (isRaster)
                    {
                        inherit.ColorAttachmentFormats = std::span<const VkFormat>(rasterInfos[i].ColorFormats.data(),
                                                                                   rasterInfos[i].ColorFormats.size());
                        inherit.DepthAttachmentFormat = rasterInfos[i].Depth;
                        inherit.StencilAttachmentFormat = rasterInfos[i].Stencil;
                        inherit.RasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
                        inherit.ViewMask = 0;
                    }

                    // `m_CompiledFrameIndex` is a monotonically increasing frame counter.
                    // Secondary command buffer reuse is keyed on a monotonic CPU frame epoch.
                    const uint64_t frameEpoch = m_Device ? m_Device->GetGlobalFrameNumber() : 0ull;
                    VkCommandBuffer sec = RHI::CommandContext::BeginSecondary(*m_Device, frameEpoch, inherit);

                    if (pass.ExecuteFn)
                        pass.ExecuteFn(pass.ExecuteUserData, m_Registry, sec);

                    RHI::CommandContext::End(sec);
                    secondaryCmds[i] = sec;
                });
            }

            Core::Tasks::Scheduler::WaitForAll();

            // Execute passes in a deterministic order on the primary command buffer.
            for (size_t i = 0; i < layer.size(); ++i)
            {
                const uint32_t passIdx = layer[i];
                const auto& pass = m_PassPool[passIdx];

                // 1) Barriers for this pass (computed in Compile, and now hoisted to primary).
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

                const bool isRaster = (pass.AttachmentHead != nullptr);
                VkRenderingInfo renderInfo{};
                std::vector<VkRenderingAttachmentInfo> colorAtts;
                VkRenderingAttachmentInfo depthAtt{};
                bool hasDepth = false;

                if (isRaster)
                {
                    colorAtts.reserve(8);
                    VkExtent2D renderArea{0, 0};

                    for (AttachmentNode* att = pass.AttachmentHead; att != nullptr; att = att->Next)
                    {
                        auto& res = m_ResourcePool[att->ID];
                        if (renderArea.width == 0 && renderArea.height == 0) renderArea = res.Extent;

                        VkRenderingAttachmentInfo info{};
                        info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                        info.imageView = res.PhysicalView;

                        // Dynamic rendering requires an attachment-capable layout.
                        // IMPORTANT: Don't use ResourceNode::CurrentLayout here.
                        // A pass can legally transition the same image to TRANSFER_SRC_OPTIMAL later (e.g. picking copy pass)
                        // which would make CurrentLayout reflect the *final* layout, not the layout at begin rendering.
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
                        else
                        {
                            colorAtts.push_back(info);
                        }
                    }

                    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                    renderInfo.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
                    renderInfo.renderArea = {{0, 0}, renderArea};
                    renderInfo.layerCount = 1;
                    renderInfo.colorAttachmentCount = (uint32_t)colorAtts.size();
                    renderInfo.pColorAttachments = colorAtts.data();
                    renderInfo.pDepthAttachment = hasDepth ? &depthAtt : nullptr;

                    vkCmdBeginRendering(cmd, &renderInfo);
                }

                VkCommandBuffer sec = secondaryCmds[i];
                if (sec != VK_NULL_HANDLE)
                    vkCmdExecuteCommands(cmd, 1, &sec);

                if (isRaster)
                    vkCmdEndRendering(cmd);
            }
        }

        // Note: CommandContext::Reset() is intentionally not called by RenderGraph.
        // The engine should reset thread-local pools at a known safe point (end-of-frame) once.
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
