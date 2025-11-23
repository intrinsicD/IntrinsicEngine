module;
#include <functional>
#include <string>
#include <vector>
#include <variant>
#include <RHI/RHI.Vulkan.hpp>

export module Runtime.RenderGraph;

import Runtime.RHI.Device;
import Runtime.RHI.Image;
import Runtime.RHI.Buffer;
import Runtime.RHI.CommandUtils;

export namespace Runtime::Graph
{
    // -------------------------------------------------------------------------
    // Handles & Resources
    // -------------------------------------------------------------------------
    using ResourceID = uint32_t;
    constexpr ResourceID kInvalidResource = ~0u;

    struct RGResourceHandle {
        ResourceID ID = kInvalidResource;
        bool IsValid() const { return ID != kInvalidResource; }
    };

    enum class ResourceType { Texture, Buffer, Import };

    struct RGTextureDesc {
        uint32_t Width = 0, Height = 0;
        VkFormat Format = VK_FORMAT_UNDEFINED;
    };

    // -------------------------------------------------------------------------
    // The Pass Builder (User Interface)
    // -------------------------------------------------------------------------
    class RenderGraph;

    class RGBuilder {
    public:
        explicit RGBuilder(RenderGraph& graph, uint32_t passIndex) 
            : m_Graph(graph), m_PassIndex(passIndex) {}

        // Declare intention to READ a resource
        RGResourceHandle Read(RGResourceHandle resource);

        // Declare intention to WRITE to a resource (RenderTarget / Storage)
        RGResourceHandle Write(RGResourceHandle resource);

        // Create a new transient texture managed by the graph
        RGResourceHandle CreateTexture(const std::string& name, const RGTextureDesc& desc);
        
        // Import an existing Vulkan Image (e.g., Swapchain Backbuffer)
        RGResourceHandle ImportTexture(const std::string& name, VkImage image, VkImageView view, VkFormat format, VkExtent2D extent);

        [[nodiscard]] VkExtent2D GetTextureExtent(RGResourceHandle handle) const;

    private:
        RenderGraph& m_Graph;
        uint32_t m_PassIndex;
    };

    // -------------------------------------------------------------------------
    // The Execution Context (What the pass receives during execution)
    // -------------------------------------------------------------------------
    class RGRegistry {
    public:
        // Get the physical Vulkan bindable object
        [[nodiscard]] VkImage GetImage(RGResourceHandle handle) const;
        [[nodiscard]] VkImageView GetImageView(RGResourceHandle handle) const;
        
        // Internal use (populating the registry)
        void RegisterImage(ResourceID id, VkImage img, VkImageView view);

    private:
        struct PhysicalImage { VkImage Image; VkImageView View; };
        std::vector<PhysicalImage> m_PhysicalImages;
    };

    using RGExecuteFn = std::function<void(const RGRegistry&, VkCommandBuffer)>;

    // -------------------------------------------------------------------------
    // The Render Graph
    // -------------------------------------------------------------------------
    class RenderGraph {
    public:
        RenderGraph(RHI::VulkanDevice& device);
        ~RenderGraph();

        // 1. Setup Phase: Add a pass to the frame
        template<typename Data>
        void AddPass(const std::string& name, 
                     std::function<void(Data&, RGBuilder&)> setup,
                     std::function<void(const Data&, const RGRegistry&, VkCommandBuffer)> execute)
        {
            auto& pass = CreatePassInternal(name);
            // Allocate data on a linear arena (simulated here with heap for brevity)
            auto* data = new Data(); 
            RGBuilder builder(*this, (uint32_t)m_Passes.size() - 1);
            
            setup(*data, builder);

            pass.Execute = [=](const RGRegistry& reg, VkCommandBuffer cmd) {
                execute(*data, reg, cmd);
                delete data; // Cleanup
            };
        }

        // 2. Compile Phase: Calculate Barriers
        void Compile();

        // 3. Execute Phase: Record Commands
        void Execute(VkCommandBuffer cmd);

        // Reset for the next frame
        void Reset();

        // Internal methods for Builder
        friend class RGBuilder;
        
    private:
        struct RGPass {
            std::string Name;
            std::vector<ResourceID> Reads;
            std::vector<ResourceID> Writes;
            std::vector<ResourceID> Creates;
            RGExecuteFn Execute;
        };

        struct ResourceNode {
            std::string Name;
            ResourceType Type;
            // State tracking for barriers
            VkImageLayout CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED; 
            
            // If imported
            VkImage PhysicalImage = VK_NULL_HANDLE;
            VkImageView PhysicalView = VK_NULL_HANDLE;
            VkExtent2D Extent = {0,0};
        };

        // Barrier storage: Index = Pass Index
        struct BarrierBatch {
            std::vector<VkImageMemoryBarrier2> ImageBarriers;
        };

        RHI::VulkanDevice& m_Device;
        std::vector<RGPass> m_Passes;
        std::vector<ResourceNode> m_Resources;
        std::vector<BarrierBatch> m_Barriers;
        RGRegistry m_Registry;

        RGPass& CreatePassInternal(const std::string& name);
        ResourceID CreateResourceInternal(const std::string& name, ResourceType type);
    };
}