module;
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
#include <span>
#include "RHI.Vulkan.hpp"

export module Graphics:RenderGraph;

import RHI;
import Core;

export namespace Graphics
{
    // -------------------------------------------------------------------------
    // Handles & Resources
    // -------------------------------------------------------------------------
    using ResourceID = uint32_t;
    constexpr ResourceID kInvalidResource = ~0u;

    struct RGResourceHandle
    {
        ResourceID ID = kInvalidResource;
        [[nodiscard]] bool IsValid() const { return ID != kInvalidResource; }
    };

    enum class ResourceType { Texture, Buffer, Import };

    struct RGAttachmentInfo
    {
        VkAttachmentLoadOp LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkClearValue ClearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    };

    struct RGTextureDesc
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        VkFormat Format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    };

    struct RGBufferDesc
    {
        size_t Size = 0;
        VkBufferUsageFlags Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    };

    // -------------------------------------------------------------------------
    // The Pass Builder (User Interface)
    // -------------------------------------------------------------------------
    class RenderGraph;

    class RGBuilder
    {
    public:
        explicit RGBuilder(RenderGraph& graph, uint32_t passIndex)
            : m_Graph(graph), m_PassIndex(passIndex)
        {
        }

        // --- General Access (Compute / Copy / Raster) ---
        // Declare a Read dependency. Default: Fragment Shader Read.
        RGResourceHandle Read(RGResourceHandle resource,
                              VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                              VkAccessFlags2 access = VK_ACCESS_2_SHADER_READ_BIT);

        // Declare a Write dependency. Default: Compute Shader Write.
        RGResourceHandle Write(RGResourceHandle resource,
                               VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               VkAccessFlags2 access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        RGResourceHandle WriteColor(RGResourceHandle resource, RGAttachmentInfo info);
        RGResourceHandle WriteDepth(RGResourceHandle resource, RGAttachmentInfo info);

        RGResourceHandle CreateTexture(Core::Hash::StringID name, const RGTextureDesc& desc);
        RGResourceHandle CreateBuffer(Core::Hash::StringID name, const RGBufferDesc& desc);

        RGResourceHandle ImportTexture(Core::Hash::StringID name, VkImage image, VkImageView view, VkFormat format,
                                       VkExtent2D extent, VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED);
        RGResourceHandle ImportBuffer(Core::Hash::StringID name, RHI::VulkanBuffer& buffer);

        [[nodiscard]] VkExtent2D GetTextureExtent(RGResourceHandle handle) const;

    private:
        RenderGraph& m_Graph;
        uint32_t m_PassIndex;
    };

    // -------------------------------------------------------------------------
    // The Execution Context (What the pass receives during execution)
    // -------------------------------------------------------------------------
    class RGRegistry
    {
    public:
        // Get the physical Vulkan bindable object
        [[nodiscard]] VkImage GetImage(RGResourceHandle handle) const;
        [[nodiscard]] VkImageView GetImageView(RGResourceHandle handle) const;
        [[nodiscard]] VkBuffer GetBuffer(RGResourceHandle handle) const;

        // Internal use (populating the registry)
        void RegisterImage(ResourceID id, VkImage img, VkImageView view);
        void RegisterBuffer(ResourceID id, VkBuffer buffer);

    private:
        struct PhysicalImage
        {
            VkImage Image;
            VkImageView View;
        };

        std::vector<PhysicalImage> m_PhysicalImages;
        std::vector<VkBuffer> m_PhysicalBuffers;
    };

    struct RenderGraphDebugImage
    {
        Core::Hash::StringID Name{};
        ResourceID Resource{};

        VkExtent2D Extent{0u, 0u};
        VkFormat Format{VK_FORMAT_UNDEFINED};
        VkImageUsageFlags Usage{0};
        VkImageAspectFlags Aspect{0};
        VkImageLayout CurrentLayout{VK_IMAGE_LAYOUT_UNDEFINED};

        VkImage Image{VK_NULL_HANDLE};
        VkImageView View{VK_NULL_HANDLE};

        uint32_t StartPass{~0u};
        uint32_t EndPass{0u};
    };

    struct RenderGraphDebugPass
    {
        const char* Name = "";
        uint32_t PassIndex = 0;

        struct Attachment
        {
            Core::Hash::StringID ResourceName{};
            ResourceID Resource{};
            bool IsDepth = false;
        };

        std::vector<Attachment> Attachments{};
    };

    struct AccessNode
    {
        ResourceID ID;
        VkPipelineStageFlags2 Stage;
        VkAccessFlags2 Access;
        AccessNode* Next = nullptr;
    };

    struct AttachmentNode
    {
        ResourceID ID;
        RGAttachmentInfo Info;
        bool IsDepth = false;
        AttachmentNode* Next = nullptr;
    };

    // -------------------------------------------------------------------------
    // The Render Graph
    // -------------------------------------------------------------------------
    class RenderGraph
    {
    public:
        explicit RenderGraph(std::shared_ptr<RHI::VulkanDevice> device,
                             Core::Memory::LinearArena& arena,
                             Core::Memory::ScopeStack& scope);
        ~RenderGraph();

        // 1. Setup Phase: Add a pass to the frame
        template <typename Data, typename SetupFn, typename ExecuteFn>
        void AddPass(const std::string& name, SetupFn&& setup, ExecuteFn&& execute)
        {
            static_assert(std::is_trivially_destructible_v<Data>,
                          "RenderGraph PassData must be trivially destructible (POD). "
                          "LinearArena does not call destructors! Do not use std::vector/std::string in PassData.");

            // NOTE: ExecuteFn is stored in ScopeStack, so it may capture non-trivial objects (std::string/shared_ptr/etc.).

            // 1. Allocate Pass Data (POD-only)
            auto dataResult = m_Arena.New<Data>();
            if (!dataResult)
            {
                Core::Log::Error("RenderGraph::AddPass failed to allocate PassData from LinearArena");
                return;
            }
            Data* data = *dataResult;

            auto& pass = CreatePassInternal(name);
            RGBuilder builder(*this, static_cast<uint32_t>(m_ActivePassCount - 1));

            // 2. Run Setup (Immediate)
            setup(*data, builder);

            // 3. Store Execution Lambda in ScopeStack (destructor-safe)
            struct PassClosure
            {
                ExecuteFn Func;
                Data* DataPtr;
            };

            auto closureMem = m_Scope.New<PassClosure>(std::forward<ExecuteFn>(execute), data);
            if (!closureMem)
            {
                Core::Log::Error("RenderGraph::AddPass failed to allocate PassClosure from ScopeStack");
                return;
            }

            PassClosure* closure = *closureMem;

            // 4. Type erase via function pointer thunk (no captures)
            pass.ExecuteFn = +[](void* userData, const RGRegistry& reg, VkCommandBuffer cmd)
            {
                auto* c = static_cast<PassClosure*>(userData);
                (c->Func)(*(c->DataPtr), reg, cmd);
            };
            pass.ExecuteUserData = closure;
        }

        // 2. Compile Phase: Calculate Barriers
        void Compile(uint32_t frameIndex);

        // 3. Execute Phase: Record Commands
        void Execute(VkCommandBuffer cmd);

        // Reset for the next frame
        void Reset();

        // Clear transient resource pools (images/buffers).
        // Intended for swapchain resize or other events that invalidate most cached extents/sizes.
        void Trim();

        // Debug/introspection for UI: valid after Compile() until Reset().
        [[nodiscard]] std::vector<RenderGraphDebugPass> BuildDebugPassList() const;
        [[nodiscard]] std::vector<RenderGraphDebugImage> BuildDebugImageList() const;

        // Inject externally-owned transient allocator (must outlive RenderGraph).
        // This avoids file-static allocators that can outlive VkDevice at shutdown.
        void SetTransientAllocator(RHI::TransientAllocator& allocator) { m_TransientAllocator = &allocator; }

        // Debug/introspection for tests/tools: valid after Compile() until Reset().
        [[nodiscard]] const std::vector<std::vector<uint32_t>>& GetExecutionLayers() const { return m_ExecutionLayers; }

        // Internal methods for Builder
        friend class RGBuilder;

    private:
        using RGExecuteFn = void(*)(void*, const RGRegistry&, VkCommandBuffer);

        struct ResourceAccess
        {
            ResourceID ID;
            VkPipelineStageFlags2 Stage;
            VkAccessFlags2 Access;
        };

        struct RGPass
        {
            std::string Name; // std::string is the only non-POD part remaining (acceptable for debug names)

            // Inputs (Linked Lists in Arena)
            AccessNode* AccessHead = nullptr;
            AccessNode* AccessTail = nullptr; // Track tail for O(1) append

            AttachmentNode* AttachmentHead = nullptr;
            AttachmentNode* AttachmentTail = nullptr;

            // Outputs (Spans in Arena, populated during Compile)
            std::span<VkImageMemoryBarrier2> ImageBarriers;
            std::span<VkBufferMemoryBarrier2> BufferBarriers;

            // Execution
            using RGExecuteFn = void(*)(void*, const RGRegistry&, VkCommandBuffer);
            RGExecuteFn ExecuteFn = nullptr;
            void* ExecuteUserData = nullptr;
        };

        struct ResourceNode
        {
            Core::Hash::StringID Name;
            ResourceType Type{};

            // Shared State Tracking
            VkPipelineStageFlags2 LastUsageStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            VkAccessFlags2 LastUsageAccess = 0;

            // --- Image Specific ---
            VkImageLayout CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImage PhysicalImage = VK_NULL_HANDLE;
            VkImageView PhysicalView = VK_NULL_HANDLE;
            VkExtent2D Extent = {0, 0};
            VkFormat Format = VK_FORMAT_UNDEFINED;
            VkImageUsageFlags Usage = 0;
            VkImageAspectFlags Aspect = 0;

            // --- Buffer Specific ---
            VkBuffer PhysicalBuffer = VK_NULL_HANDLE;
            size_t BufferSize = 0;
            VkBufferUsageFlags BufferUsage = 0;

            uint32_t StartPass = ~0u;
            uint32_t EndPass = 0;
        };

        struct PooledImage
        {
            std::unique_ptr<RHI::VulkanImage> Resource;
            uint32_t LastFrameIndex;
            uint64_t LastUsedGlobalFrame = 0;
            std::vector<std::pair<uint32_t, uint32_t>> ActiveIntervals{};
        };

        struct ImageCacheKey
        {
            VkFormat Format;
            uint32_t Width;
            uint32_t Height;
            VkImageUsageFlags Usage;
            bool operator==(const ImageCacheKey&) const = default;
        };

        struct ImageCacheKeyHash
        {
            std::size_t operator()(const ImageCacheKey& k) const
            {
                // Simple hash combine
                std::size_t h = std::hash<uint32_t>()(k.Width);
                h ^= std::hash<uint32_t>()(k.Height) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<uint32_t>()(static_cast<uint32_t>(k.Format)) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<uint32_t>()(k.Usage) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };

        struct PooledBuffer
        {
            std::unique_ptr<RHI::VulkanBuffer> Resource;
            uint32_t LastFrameIndex;
            uint64_t LastUsedGlobalFrame = 0;
            std::vector<std::pair<uint32_t, uint32_t>> ActiveIntervals{};
        };

        struct BufferCacheKey
        {
            size_t Size;
            VkBufferUsageFlags Usage;
            bool operator==(const BufferCacheKey&) const = default;
        };

        struct BufferCacheKeyHash
        {
            std::size_t operator()(const BufferCacheKey& k) const
            {
                return std::hash<size_t>()(k.Size) ^ std::hash<uint32_t>()(k.Usage);
            }
        };

        struct PooledImageStack
        {
            std::vector<PooledImage> Images;
        };

        struct PooledBufferStack
        {
            std::vector<PooledBuffer> Buffers;
        };

        struct MemoryChunk
        {
            VkDeviceMemory Memory = VK_NULL_HANDLE;
            VkDeviceSize Size = 0;
            VkDeviceSize BaseOffset = 0; // Sub-allocation offset within Memory
            uint32_t MemoryTypeBits = 0;

            // Track the lifetime of this chunk (Frame index)
            uint32_t LastFrameIndex = 0;
            uint64_t LastUsedGlobalFrame = 0;

            // List of time intervals this memory is occupied in the current frame
            std::vector<std::pair<uint32_t, uint32_t>> AllocatedIntervals;
        };

        std::shared_ptr<RHI::VulkanDevice> m_Device;
        Core::Memory::LinearArena& m_Arena;      // POD pass data
        Core::Memory::ScopeStack& m_Scope;       // destructor-safe pass closures

        RHI::TransientAllocator* m_TransientAllocator = nullptr; // non-owning

        // Change per-frame vectors to persistent pools we recycle each Reset()
        std::vector<RGPass> m_PassPool;
        uint32_t m_ActivePassCount = 0;

        std::vector<ResourceNode> m_ResourcePool;
        uint32_t m_ActiveResourceCount = 0;

        std::unordered_map<Core::Hash::StringID, ResourceID> m_ResourceLookup;

        RGRegistry m_Registry;

        std::unordered_map<ImageCacheKey, PooledImageStack, ImageCacheKeyHash> m_ImagePool;
        std::unordered_map<BufferCacheKey, PooledBufferStack, BufferCacheKeyHash> m_BufferPool;
        std::unordered_map<uint32_t, std::vector<std::unique_ptr<MemoryChunk>>> m_MemoryPool;


        RGPass& CreatePassInternal(const std::string& name);
        std::pair<ResourceID, bool> CreateResourceInternal(Core::Hash::StringID name, ResourceType type);

        RHI::VulkanImage* ResolveImage(uint32_t frameIndex, const ResourceNode& node);
        RHI::VulkanBuffer* ResolveBuffer(uint32_t frameIndex, const ResourceNode& node);
        VkDeviceMemory AllocateOrReuseMemory(const VkMemoryRequirements& reqs, uint32_t startPass, uint32_t endPass, uint32_t frameIndex);

        struct DependencyInfo
        {
            std::vector<uint32_t> DependsOn{};
            std::vector<uint32_t> Dependents{};
            uint32_t Indegree = 0;
        };

        std::vector<DependencyInfo> m_AdjacencyList{};
        std::vector<std::vector<uint32_t>> m_ExecutionLayers{};

        void BuildAdjacencyList();
        void TopologicalSortIntoLayers();
    };
}
