module;
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
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

    // -------------------------------------------------------------------------
    // The Render Graph
    // -------------------------------------------------------------------------
    class RenderGraph
    {
    public:
        explicit RenderGraph(std::shared_ptr<RHI::VulkanDevice> device, Core::Memory::LinearArena& arena);
        ~RenderGraph();

        // 1. Setup Phase: Add a pass to the frame
        template <typename Data, typename SetupFn, typename ExecuteFn>
        void AddPass(const std::string& name, SetupFn&& setup, ExecuteFn&& execute)
        {
            static_assert(std::is_trivially_destructible_v<Data>,
                          "RenderGraph PassData must be trivially destructible (POD). "
                          "LinearArena does not call destructors! Do not use std::vector/std::string in PassData.");

            // IMPORTANT: Pass closure also lives in LinearArena, so it must be trivially destructible too.
            static_assert(std::is_trivially_destructible_v<ExecuteFn>,
                          "RenderGraph ExecuteFn must be trivially destructible because it is stored in LinearArena. "
                          "Avoid capturing std::string/std::vector/std::function/etc. Capture raw pointers/handles only.");

            // 1. Allocate Pass Data
            auto dataResult = m_Arena.New<Data>();
            if (!dataResult)
            {
                // Arena OOM (or invalid). Fail fast in debug; in release we just skip adding the pass.
                Core::Log::Error("RenderGraph::AddPass failed to allocate PassData from LinearArena");
                return;
            }
            Data* data = *dataResult;

            auto& pass = CreatePassInternal(name);
            RGBuilder builder(*this, static_cast<uint32_t>(m_Passes.size()) - 1);

            // 2. Run Setup (Immediate)
            setup(*data, builder);

            // 3. Store Execution Lambda in Arena
            struct PassClosure
            {
                ExecuteFn func;
                Data* dataPtr;
            };

            static_assert(std::is_trivially_destructible_v<PassClosure>,
                          "Internal error: PassClosure must be trivially destructible to live in LinearArena.");

            auto closureMem = m_Arena.New<PassClosure>(std::forward<ExecuteFn>(execute), data);
            if (!closureMem)
            {
                Core::Log::Error("RenderGraph::AddPass failed to allocate PassClosure from LinearArena");
                return;
            }

            PassClosure* closure = *closureMem;

            // 4. Type erase via function pointer thunk (no captures, arena-safe)
            pass.ExecuteFn = +[](void* userData, const RGRegistry& reg, VkCommandBuffer cmd)
            {
                auto* c = static_cast<PassClosure*>(userData);
                (c->func)(*(c->dataPtr), reg, cmd);
            };
            pass.ExecuteUserData = closure;
        }

        // 2. Compile Phase: Calculate Barriers
        void Compile(uint32_t frameIndex);

        // 3. Execute Phase: Record Commands
        void Execute(VkCommandBuffer cmd);

        // Reset for the next frame
        void Reset();

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
            std::string Name;

            // Replaced generic Reads/Writes with structured access info
            std::vector<ResourceAccess> Accesses;

            // Still track specific Raster attachments for BeginRendering
            struct Attachment
            {
                ResourceID ID{};
                RGAttachmentInfo Info{};
                bool IsDepth = false;
            };

            std::vector<Attachment> Attachments{};

            // Arena-safe execution (no std::function heap allocations)
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

        // Barrier storage: Index = Pass Index
        struct BarrierBatch
        {
            std::vector<VkImageMemoryBarrier2> ImageBarriers;
            std::vector<VkBufferMemoryBarrier2> BufferBarriers;
        };

        struct PooledImage
        {
            std::unique_ptr<RHI::VulkanImage> Resource;
            uint32_t LastFrameIndex;
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

        std::shared_ptr<RHI::VulkanDevice> m_Device;
        Core::Memory::LinearArena& m_Arena;

        std::vector<RGPass> m_Passes;
        std::vector<ResourceNode> m_Resources;
        std::vector<BarrierBatch> m_Barriers;

        std::unordered_map<Core::Hash::StringID, ResourceID> m_ResourceLookup;

        RGRegistry m_Registry;

        std::unordered_map<ImageCacheKey, PooledImageStack, ImageCacheKeyHash> m_ImagePool;
        std::unordered_map<BufferCacheKey, PooledBufferStack, BufferCacheKeyHash> m_BufferPool;

        RGPass& CreatePassInternal(const std::string& name);
        std::pair<ResourceID, bool> CreateResourceInternal(Core::Hash::StringID name, ResourceType type);

        RHI::VulkanImage* ResolveImage(uint32_t frameIndex, const ResourceNode& node);
        RHI::VulkanBuffer* ResolveBuffer(uint32_t frameIndex, const ResourceNode& node);
    };
}
