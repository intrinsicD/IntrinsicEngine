// Graphics.RenderGraph.cpp
module;
#include "RHI.Vulkan.hpp"
#include <cassert>
#include <chrono>
#include <string>
#include <vector>
#include <memory>
#include <span>
#include <algorithm>
#include <utility>

module Graphics.RenderGraph;

import Core.Hash;
import Core.Memory;
import Core.Logging;
import Core.Tasks;
import RHI.Buffer;
import RHI.CommandContext;
import RHI.Device;
import RHI.Image;
import RHI.TransientAllocator;

namespace Graphics
{
    // Remove file-local transient allocator state; lifetime is managed by RenderDriver.

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

    RGResourceHandle RGBuilder::AddAccess(
        RGResourceHandle resource,
        VkPipelineStageFlags2 stage,
        VkAccessFlags2 access,
        bool isWrite)
    {
        if (!resource.IsValid() || resource.ID >= m_Graph.m_ActiveResourceCount)
        {
            Core::Log::Error("RG: Invalid resource handle");
            return {kInvalidResource};
        }

        AccessNode node{resource.ID, stage, access, nullptr};
        AppendToList(*m_Graph.m_Arena,
                     m_Graph.m_PassPool[m_PassIndex].AccessHead,
                     m_Graph.m_PassPool[m_PassIndex].AccessTail,
                     std::move(node));

        TouchResourceLifetime(resource, isWrite);
        return resource;
    }

    RGResourceHandle RGBuilder::AddAttachmentWrite(
        RGResourceHandle resource,
        RGAttachmentInfo info,
        bool isDepth)
    {
        if (!resource.IsValid() || resource.ID >= m_Graph.m_ActiveResourceCount)
        {
            Core::Log::Error("RG: Invalid resource handle");
            return {kInvalidResource};
        }

        TouchResourceLifetime(resource, true);

        AttachmentNode node{resource.ID, info, isDepth, nullptr};
        AppendToList(*m_Graph.m_Arena,
                     m_Graph.m_PassPool[m_PassIndex].AttachmentHead,
                     m_Graph.m_PassPool[m_PassIndex].AttachmentTail,
                     std::move(node));
        return resource;
    }

    void RGBuilder::TouchResourceLifetime(RGResourceHandle resource, bool isWrite)
    {
        auto& resourceNode = m_Graph.m_ResourcePool[resource.ID];
        if (resourceNode.StartPass == ~0u)
            resourceNode.StartPass = m_PassIndex;
        resourceNode.EndPass = std::max(resourceNode.EndPass, m_PassIndex);

        if (isWrite)
        {
            if (resourceNode.FirstWritePass == ~0u)
                resourceNode.FirstWritePass = m_PassIndex;
            resourceNode.LastWritePass = m_PassIndex;
        }
        else
        {
            if (resourceNode.FirstReadPass == ~0u)
                resourceNode.FirstReadPass = m_PassIndex;
            resourceNode.LastReadPass = m_PassIndex;
        }
    }

    RGResourceHandle RGBuilder::Read(RGResourceHandle resource, VkPipelineStageFlags2 stage, VkAccessFlags2 access)
    {
        return AddAccess(resource, stage, access, false);
    }

    RGResourceHandle RGBuilder::Write(RGResourceHandle resource, VkPipelineStageFlags2 stage, VkAccessFlags2 access)
    {
        return AddAccess(resource, stage, access, true);
    }

    RGResourceHandle RGBuilder::WriteColor(RGResourceHandle resource, RGAttachmentInfo info)
    {
        return AddAttachmentWrite(resource, info, false);
    }

    RGResourceHandle RGBuilder::WriteDepth(RGResourceHandle resource, RGAttachmentInfo info)
    {
        return AddAttachmentWrite(resource, info, true);
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
        m_Arena(&arena),
        m_Scope(&scope)
    {
    }

    void RenderGraph::RebindAllocators(Core::Memory::LinearArena& arena,
                                       Core::Memory::ScopeStack& scope)
    {
        m_Arena = &arena;
        m_Scope = &scope;
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
        // Caller is expected to have synchronized with the GPU (e.g., WaitForGraphicsIdle via renderer resize path).
        // We keep Trim() itself lightweight and deterministic.

        // NOTE:
        // With TransientAllocator backing, RenderGraph's memory pool is just lifetime metadata.
        // Physical memory is owned by RHI::TransientAllocator.
        m_MemoryPool.clear();

        m_ImagePool.clear();
        m_BufferPool.clear();
        m_CachedPlan.reset();
        m_LastShapeKey = 0;
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

    void RenderGraph::Reset(uint32_t frameIndex)
    {
        // 0. Reset DAG scheduler state
        m_Scheduler.Reset();

        // 1. Reset Allocators (Reclaim memory)
        assert(m_Arena && "RenderGraph::Reset called with null arena — RebindAllocators() not called?");
        assert(m_Scope && "RenderGraph::Reset called with null scope — RebindAllocators() not called?");
        m_Scope->Reset();
        m_Arena->Reset();

        // Reset transient GPU pages only for the frame slot being recorded.
        if (m_TransientAllocator)
            m_TransientAllocator->Reset(frameIndex);

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

        // 3. Find Memory + exact suballocation offset
        const TransientMemoryBinding binding = AllocateOrReuseMemory(reqs, node.StartPass, node.EndPass, frameIndex);
        if (!binding.IsValid())
        {
            Core::Log::Error("RenderGraph: Failed to acquire transient memory binding for image resource 0x{:08X} ({}x{}, format={}).",
                             node.Name.Value,
                             node.Extent.width,
                             node.Extent.height,
                             static_cast<int>(node.Format));
            return nullptr;
        }

        // 4. Bind with the exact offset returned by the allocator
        unboundImg.BindMemory(binding.Memory, binding.Offset);

        // 5. Move to Frame Scope
        auto allocation = m_Scope->New<RHI::VulkanImage>(std::move(unboundImg));
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

    RenderGraph::TransientMemoryBinding RenderGraph::AllocateOrReuseMemory(const VkMemoryRequirements& reqs,
                                                                           uint32_t startPass,
                                                                           uint32_t endPass,
                                                                           uint32_t frameIndex)
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
                return {chunk->Memory, chunk->BaseOffset, chunk->Size};
            }
        }

        // 2. No suitable chunk found -> allocate via transient page allocator.
        if (!m_TransientAllocator)
        {
            Core::Log::Error("RenderGraph: TransientAllocator not set. Call RenderGraph::SetTransientAllocator().");
            return {};
        }

        auto allocation = m_TransientAllocator->Allocate(frameIndex, reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (!allocation.IsValid())
        {
            Core::Log::Error("RenderGraph: Failed to allocate transient memory.");
            return {};
        }

        auto newChunk = std::make_unique<MemoryChunk>();
        newChunk->Memory = allocation.Memory;
        newChunk->Size = allocation.Size;
        newChunk->BaseOffset = allocation.Offset;
        newChunk->MemoryTypeBits = reqs.memoryTypeBits;
        newChunk->LastFrameIndex = frameIndex;
        newChunk->LastUsedGlobalFrame = globalFrame;
        newChunk->AllocatedIntervals.emplace_back(startPass, endPass);

        const TransientMemoryBinding binding{newChunk->Memory, newChunk->BaseOffset, newChunk->Size};
        bucket.push_back(std::move(newChunk));
        return binding;
    }

    namespace
    {
        // 64-bit hash combine (boost-style, adapted for 64-bit).
        constexpr uint64_t HashCombine64(uint64_t seed, uint64_t value) noexcept
        {
            value *= 0x9e3779b97f4a7c15ull;
            value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
            value ^= (value >> 31u);
            seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
            return seed;
        }

        // Scheduler-only write detection includes MEMORY_WRITE because several passes use it
        // as an ordering marker for layout-sensitive read->read chains (for example
        // TRANSFER_SRC -> SHADER_READ_ONLY on the same image).
        constexpr VkAccessFlags2 kSchedulerWriteAccessMask =
            VK_ACCESS_2_MEMORY_WRITE_BIT |
            VK_ACCESS_2_SHADER_WRITE_BIT |
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_2_TRANSFER_WRITE_BIT |
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

        // Actual Vulkan write accesses used for barrier generation/state tracking.
        // MEMORY_WRITE is intentionally excluded here: it is too generic to pair with
        // read-only layouts such as SHADER_READ_ONLY_OPTIMAL and is only used above as a
        // graph-ordering hint.
        constexpr VkAccessFlags2 kBarrierWriteAccessMask =
            VK_ACCESS_2_SHADER_WRITE_BIT |
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_2_TRANSFER_WRITE_BIT |
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

        constexpr VkAccessFlags2 kKnownReadAccessMask =
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
            VK_ACCESS_2_SHADER_READ_BIT |
            VK_ACCESS_2_UNIFORM_READ_BIT |
            VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT |
            VK_ACCESS_2_TRANSFER_READ_BIT |
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_2_MEMORY_READ_BIT;

        [[nodiscard]] constexpr bool IsWriteAccessCheck(VkAccessFlags2 a) noexcept
        {
            return (a & kSchedulerWriteAccessMask) != 0;
        }

        [[nodiscard]] constexpr bool IsBarrierWriteAccessCheck(VkAccessFlags2 a) noexcept
        {
            return (a & kBarrierWriteAccessMask) != 0;
        }

        [[nodiscard]] constexpr VkAccessFlags2 SanitizeAccessForBarrier(VkAccessFlags2 access) noexcept
        {
            const bool hasRealWrite = (access & kBarrierWriteAccessMask) != 0;
            const bool hasRead = (access & kKnownReadAccessMask) != 0;
            if (hasRead && !hasRealWrite)
                access &= ~VK_ACCESS_2_MEMORY_WRITE_BIT;
            return access;
        }

        [[nodiscard]] constexpr bool IsAttachmentContinuationBarrier(const VkImageMemoryBarrier2& barrier) noexcept
        {
            if (barrier.oldLayout != barrier.newLayout)
                return false;

            const bool colorContinuation =
                barrier.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
                barrier.srcStageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT &&
                barrier.dstStageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT &&
                barrier.srcAccessMask == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT &&
                barrier.dstAccessMask == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

            constexpr VkPipelineStageFlags2 kDepthStages =
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            const bool depthContinuation =
                barrier.oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
                barrier.srcStageMask == kDepthStages &&
                barrier.dstStageMask == kDepthStages &&
                barrier.srcAccessMask == VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT &&
                barrier.dstAccessMask == VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            return colorContinuation || depthContinuation;
        }

        [[nodiscard]] bool AreOnlyAttachmentContinuationBarriers(std::span<const VkImageMemoryBarrier2> barriers) noexcept
        {
            return std::all_of(barriers.begin(), barriers.end(), [](const VkImageMemoryBarrier2& barrier)
            {
                return IsAttachmentContinuationBarrier(barrier);
            });
        }

        template <typename PassT>
        [[nodiscard]] bool CanMergeRasterContinuation(const PassT& pass) noexcept
        {
            return pass.BufferBarriers.empty() && AreOnlyAttachmentContinuationBarriers(pass.ImageBarriers);
        }
    }

    void RenderGraph::Compile(uint32_t frameIndex)
    {
        m_CompiledFrameIndex = frameIndex;
        ResolveTransientResources(frameIndex);
        CalculateBarriers();
        BuildSchedulerGraph(); // Always rebuild (cheap, reuses pools); keeps GetExecutionLayers() valid.

        const uint64_t shapeKey = ComputeShapeKey();

        if (m_CachedPlan && m_CachedPlan->ShapeKey == shapeKey)
        {
            // Cache hit: reuse packet assignments from previous frame.
            m_LastShapeKey = shapeKey;
            m_LastCacheHit = true;
            return;
        }

        // Cache miss: rebuild packets.
        m_LastShapeKey = shapeKey;
        m_LastCacheHit = false;
        Packetize();
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

                const bool prevWrite = IsBarrierWriteAccessCheck(res.LastUsageAccess);
                const bool currWrite = IsBarrierWriteAccessCheck(dstAccess);
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

                    auto alloc = m_Arena->New<VkImageMemoryBarrier2>(barrier);
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

                if (att->Info.LoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
                {
                    const bool hasPriorWrite = (res.FirstWritePass != ~0u && res.FirstWritePass < passIdx) ||
                                               (res.Type == ResourceType::Import);
                    if (!hasPriorWrite)
                    {
                        Core::Log::Warn(
                            "RenderGraph: pass '{}' uses LOAD on resource 0x{:08X} without a guaranteed prior write in this frame.",
                            pass.Name,
                            res.Name.Value);
                    }
                }

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
                const VkAccessFlags2 effectiveAccess = SanitizeAccessForBarrier(node->Access);
                VkImageLayout targetLayout = res.CurrentLayout;

                if ((effectiveAccess & (VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                else if ((effectiveAccess & (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                else if ((effectiveAccess & VK_ACCESS_2_TRANSFER_WRITE_BIT) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                else if ((effectiveAccess & VK_ACCESS_2_TRANSFER_READ_BIT) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                else if ((effectiveAccess & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                else if ((effectiveAccess & (VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT)) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                else if ((effectiveAccess & (VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT)) != 0)
                    targetLayout = VK_IMAGE_LAYOUT_GENERAL;

                pushImageBarrier(res, node->Stage, effectiveAccess, targetLayout);
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

                const VkAccessFlags2 effectiveAccess = SanitizeAccessForBarrier(node->Access);
                bool prevWrite = IsBarrierWriteAccessCheck(res.LastUsageAccess);
                bool currWrite = IsBarrierWriteAccessCheck(effectiveAccess);

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
                    barrier.dstAccessMask = effectiveAccess;

                    auto alloc = m_Arena->New<VkBufferMemoryBarrier2>(barrier);
                    if (alloc)
                    {
                        if (bufCount == 0) bufStart = *alloc;
                        bufCount++;
                    }
                }
                res.LastUsageStage = node->Stage;
                res.LastUsageAccess = effectiveAccess;
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
        if (!m_CachedPlan || m_CachedPlan->PacketLayers.empty())
        {
            // Fallback: if no cached plan, build one now.
            if (m_Scheduler.GetExecutionLayers().empty() && m_ActivePassCount > 0)
                BuildSchedulerGraph();
            Packetize();
        }

        m_LastPassTimings.clear();
        m_LastPassTimings.reserve(m_ActivePassCount);

        for (const auto& packetLayer : m_CachedPlan->PacketLayers)
        {
            if (!packetLayer.empty())
                ExecutePacketLayer(cmd, packetLayer);
        }

        // Note: CommandContext::Reset() is intentionally not called by RenderGraph.
        // The engine should reset thread-local pools at a known safe point (end-of-frame) once.
    }

    void RenderGraph::ExecutePacketLayer(VkCommandBuffer cmd, std::span<const ExecutionPacket> packets)
    {
        // 1. Record one secondary command buffer per packet in parallel.
        std::vector<VkCommandBuffer> secondaryCmds(packets.size(), VK_NULL_HANDLE);

        for (size_t pi = 0; pi < packets.size(); ++pi)
        {
            Core::Tasks::Scheduler::Dispatch([this, &packets, pi, &secondaryCmds]
            {
                const auto& pkt = packets[pi];

                RHI::SecondaryInheritanceInfo inherit{};
                if (pkt.IsRaster)
                {
                    inherit.ColorAttachmentFormats = std::span<const VkFormat>(
                        pkt.ColorFormats.data(), pkt.ColorFormats.size());
                    inherit.DepthAttachmentFormat = pkt.DepthFormat;
                    inherit.StencilAttachmentFormat = pkt.StencilFormat;
                    inherit.RasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
                    inherit.ViewMask = 0;
                }

                const uint64_t globalFrame = m_Device ? m_Device->GetGlobalFrameNumber() : 0ull;
                VkCommandBuffer sec = RHI::CommandContext::BeginSecondary(*m_Device, globalFrame, inherit);

                // Record all passes in the packet into this single secondary.
                for (uint32_t p = 0; p < pkt.PassCount; ++p)
                {
                    const uint32_t passIdx = pkt.FirstPass + p;
                    auto& pass = m_PassPool[passIdx];

                    // For multi-pass packets: intra-secondary barriers for non-first passes.
                    const bool skipAttachmentContinuationBarriers =
                        p > 0 && pkt.IsRaster && CanMergeRasterContinuation(pass);
                    if (p > 0 && !skipAttachmentContinuationBarriers
                        && (!pass.ImageBarriers.empty() || !pass.BufferBarriers.empty()))
                    {
                        VkDependencyInfo depInfo{};
                        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(pass.ImageBarriers.size());
                        depInfo.pImageMemoryBarriers = pass.ImageBarriers.data();
                        depInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(pass.BufferBarriers.size());
                        depInfo.pBufferMemoryBarriers = pass.BufferBarriers.data();
                        vkCmdPipelineBarrier2(sec, &depInfo);
                    }

                    if (pass.ExecuteFn)
                        pass.ExecuteFn(pass.ExecuteUserData, m_Registry, sec);
                }

                RHI::CommandContext::End(sec);
                secondaryCmds[pi] = sec;
            });
        }

        Core::Tasks::Scheduler::WaitForAll();

        // 2. Primary: barriers (first pass of each packet), rendering scope, execute secondaries.
        //    GPU timestamp scopes are written on the primary cmd buffer around each packet.
        //    CPU timing is measured around the barrier+render+execute block per packet.
        for (size_t pi = 0; pi < packets.size(); ++pi)
        {
            const auto& pkt = packets[pi];
            const uint32_t firstPassIdx = pkt.FirstPass;
            const auto& firstPass = m_PassPool[firstPassIdx];

            const auto cpuStart = std::chrono::high_resolution_clock::now();

            // GPU scope begin (on primary, before any barriers/rendering for this packet).
            uint32_t gpuScopeIdx = ~0u;
            if (m_GpuProfiler)
            {
                gpuScopeIdx = m_GpuProfiler->BeginScope(firstPass.Name);
                m_GpuProfiler->WriteScopeBegin(cmd, gpuScopeIdx, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
            }

            // Barriers for the first pass in the packet (hoisted to primary).
            if (!firstPass.ImageBarriers.empty() || !firstPass.BufferBarriers.empty())
            {
                VkDependencyInfo depInfo{};
                depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(firstPass.ImageBarriers.size());
                depInfo.pImageMemoryBarriers = firstPass.ImageBarriers.data();
                depInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(firstPass.BufferBarriers.size());
                depInfo.pBufferMemoryBarriers = firstPass.BufferBarriers.data();
                vkCmdPipelineBarrier2(cmd, &depInfo);
            }

            // Begin rendering scope (for raster packets).
            VkRenderingInfo renderInfo{};
            std::vector<VkRenderingAttachmentInfo> colorAtts;
            VkRenderingAttachmentInfo depthAtt{};
            bool hasDepth = false;

            if (pkt.IsRaster)
            {
                colorAtts.reserve(8);
                VkExtent2D renderArea{0, 0};

                // For merged raster packets, load ops + clear values come from
                // the first pass; store ops come from the last pass.
                const auto& lastPass = m_PassPool[pkt.FirstPass + pkt.PassCount - 1];

                // Build a quick lookup of store ops from the last pass's attachments.
                // Walk both lists in lockstep (they have matching structure — same
                // resource IDs in the same order, guaranteed by HasMatchingAttachments).
                const AttachmentNode* lastAtt = lastPass.AttachmentHead;

                for (AttachmentNode* att = firstPass.AttachmentHead; att != nullptr; att = att->Next)
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
                    info.storeOp = lastAtt ? lastAtt->Info.StoreOp : att->Info.StoreOp;
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

                    if (lastAtt) lastAtt = lastAtt->Next;
                }

                renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                renderInfo.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
                renderInfo.renderArea = {{0, 0}, renderArea};
                renderInfo.layerCount = 1;
                renderInfo.colorAttachmentCount = static_cast<uint32_t>(colorAtts.size());
                renderInfo.pColorAttachments = colorAtts.data();
                renderInfo.pDepthAttachment = hasDepth ? &depthAtt : nullptr;

                vkCmdBeginRendering(cmd, &renderInfo);
            }

            VkCommandBuffer sec = secondaryCmds[pi];
            if (sec != VK_NULL_HANDLE)
                vkCmdExecuteCommands(cmd, 1, &sec);

            if (pkt.IsRaster)
                vkCmdEndRendering(cmd);

            // GPU scope end (on primary, after rendering for this packet).
            if (m_GpuProfiler && gpuScopeIdx != ~0u)
            {
                m_GpuProfiler->WriteScopeEnd(cmd, gpuScopeIdx, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
            }

            const auto cpuEnd = std::chrono::high_resolution_clock::now();
            const uint64_t cpuNs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(cpuEnd - cpuStart).count());

            // Record per-pass timing (attribute wall time to first pass in packet).
            for (uint32_t p = 0; p < pkt.PassCount; ++p)
            {
                m_LastPassTimings.push_back(PassTiming{
                    m_PassPool[pkt.FirstPass + p].Name,
                    (p == 0) ? cpuNs : 0
                });
            }
        }
    }

    // --- Shape Hashing ---
    uint64_t RenderGraph::ComputeShapeKey() const
    {
        uint64_t h = 0xcbf29ce484222325ull;
        h = HashCombine64(h, static_cast<uint64_t>(m_ActivePassCount));
        for (uint32_t i = 0; i < m_ActivePassCount; ++i)
        {
            const auto& pass = m_PassPool[i];
            h = HashCombine64(h, std::hash<std::string>{}(pass.Name));
            for (AccessNode* node = pass.AccessHead; node != nullptr; node = node->Next)
            {
                h = HashCombine64(h, static_cast<uint64_t>(node->ID));
                h = HashCombine64(h, static_cast<uint64_t>(node->Stage));
                h = HashCombine64(h, static_cast<uint64_t>(node->Access));
            }
            for (AttachmentNode* att = pass.AttachmentHead; att != nullptr; att = att->Next)
            {
                h = HashCombine64(h, static_cast<uint64_t>(att->ID));
                h = HashCombine64(h, att->IsDepth ? 1ull : 0ull);
            }
        }
        return h;
    }

    // --- Packetization ---
    bool RenderGraph::HasLinearEdge(uint32_t src, uint32_t dst) const
    {
        // src must have exactly 1 dependent, and it must be dst.
        auto dependents = m_Scheduler.GetDependents(src);
        if (dependents.size() != 1 || dependents[0] != dst) return false;
        // dst must have indegree 1 (single predecessor).
        return m_Scheduler.GetIndegree(dst) == 1;
    }

    bool RenderGraph::HasMatchingAttachments(uint32_t passA, uint32_t passB) const
    {
        const auto& a = m_PassPool[passA];
        const auto& b = m_PassPool[passB];

        // Both must be raster (have attachments).
        if (a.AttachmentHead == nullptr || b.AttachmentHead == nullptr)
            return false;

        // Walk both linked lists and compare resource IDs and depth classification.
        const AttachmentNode* na = a.AttachmentHead;
        const AttachmentNode* nb = b.AttachmentHead;
        while (na != nullptr && nb != nullptr)
        {
            if (na->ID != nb->ID || na->IsDepth != nb->IsDepth)
                return false;
            na = na->Next;
            nb = nb->Next;
        }
        // Both lists must be the same length.
        return na == nullptr && nb == nullptr;
    }


    void RenderGraph::Packetize()
    {
        m_CachedPlan = CompiledRenderPlan{};
        m_CachedPlan->ShapeKey = m_LastShapeKey;
        if (m_ActivePassCount == 0)
        {
            m_LastPacketCount = 0;
            return;
        }

        constexpr uint32_t kInvalidPacket = ~0u;

        auto makePacket = [this](uint32_t passIdx)
        {
            ExecutionPacket pkt;
            pkt.FirstPass = passIdx;
            pkt.PassCount = 1;

            const auto& pass = m_PassPool[passIdx];
            pkt.IsRaster = (pass.AttachmentHead != nullptr);
            if (pkt.IsRaster)
            {
                for (AttachmentNode* att = pass.AttachmentHead; att != nullptr; att = att->Next)
                {
                    const auto& res = m_ResourcePool[att->ID];
                    if (att->IsDepth)
                        pkt.DepthFormat = res.Format;
                    else
                        pkt.ColorFormats.push_back(res.Format);
                }
            }
            return pkt;
        };

        auto canMergeIntoPacket = [this](const ExecutionPacket& packet, uint32_t passIdx)
        {
            const uint32_t packetEnd = packet.FirstPass + packet.PassCount - 1;
            const auto& pass = m_PassPool[passIdx];
            const bool isRaster = (pass.AttachmentHead != nullptr);

            if (passIdx != packetEnd + 1)
                return false;
            if (!HasLinearEdge(packetEnd, passIdx))
                return false;
            if (!pass.BufferBarriers.empty())
                return false;

            const bool imageBarriersCompatible =
                pass.ImageBarriers.empty() || (isRaster && packet.IsRaster && CanMergeRasterContinuation(pass));
            if (!imageBarriersCompatible)
                return false;

            if (!isRaster && !packet.IsRaster)
                return true;

            return isRaster && packet.IsRaster && HasMatchingAttachments(packet.FirstPass, passIdx);
        };

        std::vector<ExecutionPacket> packets;
        packets.reserve(m_ActivePassCount);

        std::vector<uint32_t> passToPacket(m_ActivePassCount, kInvalidPacket);

        for (uint32_t passIdx = 0; passIdx < m_ActivePassCount;)
        {
            const uint32_t packetIndex = static_cast<uint32_t>(packets.size());
            ExecutionPacket pkt = makePacket(passIdx);
            passToPacket[passIdx] = packetIndex;
            ++passIdx;

            while (passIdx < m_ActivePassCount && canMergeIntoPacket(pkt, passIdx))
            {
                passToPacket[passIdx] = packetIndex;
                ++pkt.PassCount;
                ++passIdx;
            }

            packets.push_back(std::move(pkt));
        }

        std::vector<std::vector<uint32_t>> packetDependents(packets.size());
        std::vector<uint32_t> packetIndegree(packets.size(), 0);

        for (uint32_t passIdx = 0; passIdx < m_ActivePassCount; ++passIdx)
        {
            const uint32_t srcPacket = passToPacket[passIdx];
            if (srcPacket == kInvalidPacket)
                continue;

            for (uint32_t depPass : m_Scheduler.GetDependents(passIdx))
            {
                if (depPass >= m_ActivePassCount)
                    continue;

                const uint32_t dstPacket = passToPacket[depPass];
                if (dstPacket == kInvalidPacket || dstPacket == srcPacket)
                    continue;

                auto& deps = packetDependents[srcPacket];
                if (std::find(deps.begin(), deps.end(), dstPacket) != deps.end())
                    continue;

                deps.push_back(dstPacket);
                ++packetIndegree[dstPacket];
            }
        }

        std::vector<uint32_t> ready;
        ready.reserve(packets.size());
        for (uint32_t packetIdx = 0; packetIdx < packets.size(); ++packetIdx)
        {
            if (packetIndegree[packetIdx] == 0)
                ready.push_back(packetIdx);
        }

        while (!ready.empty())
        {
            std::sort(ready.begin(), ready.end());

            auto& packetLayer = m_CachedPlan->PacketLayers.emplace_back();
            packetLayer.reserve(ready.size());

            std::vector<uint32_t> nextReady;
            nextReady.reserve(packets.size());

            for (uint32_t packetIdx : ready)
            {
                packetLayer.push_back(packets[packetIdx]);
                for (uint32_t depPacket : packetDependents[packetIdx])
                {
                    if (--packetIndegree[depPacket] == 0)
                        nextReady.push_back(depPacket);
                }
            }

            ready = std::move(nextReady);
        }

        m_LastPacketCount = static_cast<uint32_t>(packets.size());
    }

    std::vector<RenderGraphDebugPass> RenderGraph::BuildDebugPassList() const
    {
        std::vector<RenderGraphDebugPass> out;
        out.reserve(m_ActivePassCount);

        const auto findResourceByImage = [this](VkImage image) -> ResourceID
        {
            if (image == VK_NULL_HANDLE)
                return ~0u;

            for (uint32_t resourceIndex = 0; resourceIndex < m_ActiveResourceCount; ++resourceIndex)
            {
                if (m_ResourcePool[resourceIndex].PhysicalImage == image)
                    return resourceIndex;
            }

            return ~0u;
        };

        const auto findResourceByBuffer = [this](VkBuffer buffer) -> ResourceID
        {
            if (buffer == VK_NULL_HANDLE)
                return ~0u;

            for (uint32_t resourceIndex = 0; resourceIndex < m_ActiveResourceCount; ++resourceIndex)
            {
                if (m_ResourcePool[resourceIndex].PhysicalBuffer == buffer)
                    return resourceIndex;
            }

            return ~0u;
        };

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
                dp.Attachments.push_back({res.Name,
                                          att->ID,
                                          res.Format,
                                          att->Info.LoadOp,
                                          att->Info.StoreOp,
                                          att->IsDepth,
                                          res.Type == ResourceType::Import});
            }

            for (const VkImageMemoryBarrier2& barrier : p.ImageBarriers)
            {
                const ResourceID resource = findResourceByImage(barrier.image);
                dp.ImageBarriers.push_back({resource,
                                            barrier.srcStageMask,
                                            barrier.srcAccessMask,
                                            barrier.dstStageMask,
                                            barrier.dstAccessMask,
                                            barrier.oldLayout,
                                            barrier.newLayout});
            }

            for (const VkBufferMemoryBarrier2& barrier : p.BufferBarriers)
            {
                const ResourceID resource = findResourceByBuffer(barrier.buffer);
                dp.BufferBarriers.push_back({resource,
                                             barrier.srcStageMask,
                                             barrier.srcAccessMask,
                                             barrier.dstStageMask,
                                             barrier.dstAccessMask});
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
            di.IsImported = (res.Type == ResourceType::Import);
            di.FirstWritePass = res.FirstWritePass;
            di.LastWritePass = res.LastWritePass;
            di.FirstReadPass = res.FirstReadPass;
            di.LastReadPass = res.LastReadPass;
            di.StartPass = res.StartPass;
            di.EndPass = res.EndPass;

            out.push_back(di);
        }

        return out;
    }
}
