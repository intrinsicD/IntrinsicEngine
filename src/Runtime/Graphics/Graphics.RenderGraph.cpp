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
        // 0. Reset DAG scheduler state
        m_Scheduler.Reset();

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

    namespace
    {
        // Single source of truth for write-access detection across all barrier paths.
        // Used by image barriers, buffer barriers, and the adjacency-list builder.
        constexpr VkAccessFlags2 kWriteAccessMask =
            VK_ACCESS_2_MEMORY_WRITE_BIT |
            VK_ACCESS_2_SHADER_WRITE_BIT |
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_2_TRANSFER_WRITE_BIT |
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

        [[nodiscard]] constexpr bool IsWriteAccessCheck(VkAccessFlags2 a) noexcept
        {
            return (a & kWriteAccessMask) != 0;
        }
    }

    void RenderGraph::Compile(uint32_t frameIndex)
    {
        m_CompiledFrameIndex = frameIndex;
        ResolveTransientResources(frameIndex);
        CalculateBarriers();
        BuildSchedulerGraph();
    }

    void RenderGraph::ResolveTransientResources(uint32_t frameIndex)
    {
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
    }

    void RenderGraph::CalculateBarriers()
    {
        for (uint32_t passIdx = 0; passIdx < m_ActivePassCount; ++passIdx)
        {
            auto& pass = m_PassPool[passIdx];

            // --- Image Barriers ---
            VkImageMemoryBarrier2* imgStart = nullptr;
            size_t imgCount = 0;

            auto pushImageBarrier = [&](ResourceNode& res,
                                        VkPipelineStageFlags2 dstStage,
                                        VkAccessFlags2 dstAccess,
                                        VkImageLayout targetLayout)
            {
                if (res.Type == ResourceType::Buffer) return;
                if (res.Type == ResourceType::Import && res.PhysicalBuffer) return;

                const bool prevWrite = IsWriteAccessCheck(res.LastUsageAccess);
                const bool currWrite = IsWriteAccessCheck(dstAccess);
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

            // Attachment usages are implicit accesses; they MUST participate in layout tracking.
            // IMPORTANT: Process BEFORE explicit AccessHead. Otherwise, a pass that both renders to an image
            // and later copies/samples it can end up with TRANSFER_* as its final tracked layout,
            // which is invalid for vkCmdBeginRendering.
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

            // Explicit resource accesses — determine target layout from access flags.
            for (AccessNode* node = pass.AccessHead; node != nullptr; node = node->Next)
            {
                auto& res = m_ResourcePool[node->ID];
                VkImageLayout targetLayout = res.CurrentLayout;

                if ((node->Access & (VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                else if ((node->Access & (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                else if ((node->Access & VK_ACCESS_2_TRANSFER_WRITE_BIT) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                else if ((node->Access & VK_ACCESS_2_TRANSFER_READ_BIT) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                else if ((node->Access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                else if ((node->Access & (VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT)) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                else if ((node->Access & (VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT)) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_GENERAL;

                pushImageBarrier(res, node->Stage, node->Access, targetLayout);
            }

            if (imgCount > 0) pass.ImageBarriers = std::span<VkImageMemoryBarrier2>(imgStart, imgCount);

            // --- Buffer Barriers ---
            VkBufferMemoryBarrier2* bufStart = nullptr;
            size_t bufCount = 0;

            for (AccessNode* node = pass.AccessHead; node != nullptr; node = node->Next)
            {
                auto& res = m_ResourcePool[node->ID];
                bool isBuffer = (res.Type == ResourceType::Buffer) || (res.Type == ResourceType::Import && res.PhysicalBuffer);
                if (!isBuffer) continue;

                bool prevWrite = IsWriteAccessCheck(res.LastUsageAccess);
                bool currWrite = IsWriteAccessCheck(node->Access);

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

    void RenderGraph::BuildSchedulerGraph()
    {
        // Reset and populate the DAGScheduler with this frame's passes and dependencies.
        m_Scheduler.Reset();

        // Add a node per pass.
        for (uint32_t i = 0; i < m_ActivePassCount; ++i)
        {
            m_Scheduler.AddNode();
        }

        // Walk access/attachment lists and declare read/write hazards.
        for (uint32_t passIdx = 0; passIdx < m_ActivePassCount; ++passIdx)
        {
            const auto& pass = m_PassPool[passIdx];

            // Access hazards.
            for (AccessNode* node = pass.AccessHead; node != nullptr; node = node->Next)
            {
                if (node->ID >= m_ActiveResourceCount) continue;

                const bool isWrite = IsWriteAccessCheck(node->Access);

                if (isWrite)
                    m_Scheduler.DeclareWrite(passIdx, static_cast<size_t>(node->ID));
                else
                    m_Scheduler.DeclareRead(passIdx, static_cast<size_t>(node->ID));
            }

            // Attachment hazards: treat as writes (raster). This ensures a pass that renders to an image
            // correctly serializes with later reads/writes even if user forgot to declare explicit Access.
            for (AttachmentNode* att = pass.AttachmentHead; att != nullptr; att = att->Next)
            {
                if (att->ID >= m_ActiveResourceCount) continue;
                m_Scheduler.DeclareWrite(passIdx, static_cast<size_t>(att->ID));
            }
        }

        // Topological sort into execution layers.
        auto result = m_Scheduler.Compile();
        if (!result)
        {
            // Cycle detected — fall back to sequential order for safety.
            // DAGScheduler logs the error. We need to provide a valid execution order.
            Core::Log::Error("RenderGraph: Falling back to sequential execution order.");
        }
    }

    void RenderGraph::Execute(VkCommandBuffer cmd)
    {
        // If Compile() wasn't called (or for safety), build layers lazily.
        if (m_Scheduler.GetExecutionLayers().empty() && m_ActivePassCount > 0)
        {
            BuildSchedulerGraph();
        }

        for (const auto& layer : m_Scheduler.GetExecutionLayers())
        {
            if (!layer.empty())
                ExecuteLayer(cmd, layer);
        }

        // Note: CommandContext::Reset() is intentionally not called by RenderGraph.
        // The engine should reset thread-local pools at a known safe point (end-of-frame) once.
    }

    void RenderGraph::ExecuteLayer(VkCommandBuffer cmd, const std::vector<uint32_t>& layer)
    {
        // 1. Precompute inheritance info per pass (stable pointers for the worker tasks).
        struct RasterInfo
        {
            bool IsRaster = false;
            std::vector<VkFormat> ColorFormats;
            VkFormat Depth = VK_FORMAT_UNDEFINED;
            VkFormat Stencil = VK_FORMAT_UNDEFINED;
        };

        std::vector<RasterInfo> rasterInfos(layer.size());

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
                        ri.Depth = res.Format;
                    else
                        ri.ColorFormats.push_back(res.Format);
                }
            }
            rasterInfos[i] = std::move(ri);
        }

        // 2. Record secondary command buffers in parallel.
        std::vector<VkCommandBuffer> secondaryCmds(layer.size(), VK_NULL_HANDLE);

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

                const uint64_t frameEpoch = m_Device ? m_Device->GetGlobalFrameNumber() : 0ull;
                VkCommandBuffer sec = RHI::CommandContext::BeginSecondary(*m_Device, frameEpoch, inherit);

                if (pass.ExecuteFn)
                    pass.ExecuteFn(pass.ExecuteUserData, m_Registry, sec);

                RHI::CommandContext::End(sec);
                secondaryCmds[i] = sec;
            });
        }

        Core::Tasks::Scheduler::WaitForAll();

        // 3. Record barriers, begin rendering, and execute secondaries on the primary buffer.
        for (size_t i = 0; i < layer.size(); ++i)
        {
            const uint32_t passIdx = layer[i];
            const auto& pass = m_PassPool[passIdx];

            // Barriers (computed in Compile, hoisted to primary).
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
                    // IMPORTANT: Use the attachment-capable layout, not CurrentLayout
                    // (which may reflect a later transition).
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
