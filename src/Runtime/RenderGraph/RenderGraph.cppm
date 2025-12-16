module;
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
#include <RHI/RHI.Vulkan.hpp>

export module Runtime.RenderGraph;

import Runtime.RHI.Device;
import Runtime.RHI.Image;
import Runtime.RHI.Buffer;
import Runtime.RHI.CommandUtils;
import Core.Memory;
import Core.Logging;

export namespace Runtime::Graph
{
    // -------------------------------------------------------------------------
    // Handles & Resources
    // -------------------------------------------------------------------------
    using ResourceID = uint32_t;
    constexpr ResourceID kInvalidResource = ~0u;

    struct RGResourceHandle
    {
        ResourceID ID = kInvalidResource;
        bool IsValid() const { return ID != kInvalidResource; }
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
    };

    struct RGBufferDesc
    {
        size_t Size = 0;
        VkBufferUsageFlags Usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        VmaMemoryUsage Memory = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
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

        // Declare intention to READ a resource
        RGResourceHandle Read(RGResourceHandle resource);

        // Declare intention to WRITE to a resource (RenderTarget / Storage)
        RGResourceHandle Write(RGResourceHandle resource);

        // Rasterization specific writes
        RGResourceHandle WriteColor(RGResourceHandle resource, RGAttachmentInfo info);
        RGResourceHandle WriteDepth(RGResourceHandle resource, RGAttachmentInfo info);

        // Create a new transient texture managed by the graph
        RGResourceHandle CreateTexture(const std::string& name, const RGTextureDesc& desc);

        // Create a transient buffer managed by the graph
        RGResourceHandle CreateBuffer(const std::string& name, const RGBufferDesc& desc);

        // Import an existing Vulkan Image (e.g., Swapchain Backbuffer)
        RGResourceHandle ImportTexture(const std::string& name, VkImage image, VkImageView view, VkFormat format,
                                       VkExtent2D extent);

        // Declare buffer usage for a pass
        RGResourceHandle ReadBuffer(RGResourceHandle resource);
        RGResourceHandle WriteBuffer(RGResourceHandle resource);

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

        struct PhysicalBuffer
        {
            VkBuffer Buffer;
        };

        std::vector<PhysicalImage> m_PhysicalImages;
        std::vector<PhysicalBuffer> m_PhysicalBuffers;
    };

    using RGExecuteFn = std::function<void(const RGRegistry&, VkCommandBuffer)>;

    // -------------------------------------------------------------------------
    // The Render Graph
    // -------------------------------------------------------------------------
    class RenderGraph
    {
    public:
        explicit RenderGraph(std::shared_ptr<RHI::VulkanDevice> device, Core::Memory::LinearArena& arena);
        ~RenderGraph();

        // 1. Setup Phase: Add a pass to the frame
        template <typename Data>
        void AddPass(const std::string& name,
                     std::function<void(Data&, RGBuilder&)> setup,
                     std::function<void(const Data&, const RGRegistry&, VkCommandBuffer)> execute)
        {
            static_assert(std::is_trivially_destructible_v<Data>,
    "RenderGraph PassData must be trivially destructible (POD) because the LinearArena does not call destructors.");
            auto& pass = CreatePassInternal(name);
            // Allocate data on a linear arena (simulated here with heap for brevity)
            auto allocResult = m_Arena.New<Data>();
            if (!allocResult)
            {
                Core::Log::Error("RenderGraph: Frame Arena Out of Memory!");
                Core::Log::Error("  Pass Name: {}", name);
                Core::Log::Error("  Requested Size: {}", sizeof(Data));
                Core::Log::Error("  Arena used: {} / {} bytes", m_Arena.GetUsed(), m_Arena.GetTotal());
                std::exit(1);
            }

            Data* data = *allocResult;

            RGBuilder builder(*this, (uint32_t)m_Passes.size() - 1);

            setup(*data, builder);

            pass.Execute = [=](const RGRegistry& reg, VkCommandBuffer cmd)
            {
                execute(*data, reg, cmd);
            };
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
        struct RGPass
        {
            std::string Name;
            std::vector<ResourceID> Reads;
            std::vector<ResourceID> Writes;
            std::vector<ResourceID> Creates;

            // Rasterization Info
            struct Attachment
            {
                ResourceID ID;
                RGAttachmentInfo Info;
                bool IsDepth = false;
            };

            std::vector<Attachment> Attachments;

            RGExecuteFn Execute;
        };

        struct ResourceNode
        {
            std::string Name;
            ResourceType Type;
            // State tracking for barriers
            VkImageLayout CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkPipelineStageFlags2 CurrentStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            VkAccessFlags2 CurrentAccess = 0;

            // If imported
            VkImage PhysicalImage = VK_NULL_HANDLE;
            VkImageView PhysicalView = VK_NULL_HANDLE;
            VkExtent2D Extent = {0, 0};
            VkFormat Format = VK_FORMAT_UNDEFINED;

            // Buffer info
            VkBuffer PhysicalBuffer = VK_NULL_HANDLE;
            size_t BufferSize = 0;
            VkBufferUsageFlags BufferUsage = 0;
            VmaMemoryUsage BufferMemory = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
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
            bool IsFree;
        };

        struct PooledBuffer
        {
            std::unique_ptr<RHI::VulkanBuffer> Resource;
            uint32_t LastFrameIndex;
            bool IsFree;
            size_t Size;
            VkBufferUsageFlags Usage;
            VmaMemoryUsage Memory;
        };

        std::shared_ptr<RHI::VulkanDevice> m_Device;
        Core::Memory::LinearArena& m_Arena;

        std::vector<RGPass> m_Passes;
        std::vector<ResourceNode> m_Resources;
        std::vector<BarrierBatch> m_Barriers;

        std::unordered_map<std::string, ResourceID> m_ResourceLookup;

        RGRegistry m_Registry;
        std::vector<PooledImage> m_ImagePool;
        std::vector<PooledBuffer> m_BufferPool;

        RGPass& CreatePassInternal(const std::string& name);
        std::pair<ResourceID, bool> CreateResourceInternal(const std::string& name, ResourceType type);
        RHI::VulkanImage* AllocateImage(uint32_t frameIndex, uint32_t width, uint32_t height, VkFormat format,
                                        VkImageUsageFlags usage, VkImageAspectFlags aspect);
        RHI::VulkanBuffer* AllocateBuffer(uint32_t frameIndex, size_t size, VkBufferUsageFlags usage,
                                          VmaMemoryUsage memoryUsage);
    };
}
