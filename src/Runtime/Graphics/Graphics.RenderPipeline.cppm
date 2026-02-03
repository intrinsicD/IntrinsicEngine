module;

#include <unordered_map>
#include <memory>
#include <span>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

export module Graphics:RenderPipeline;

import :RenderGraph;
import :MaterialSystem;
import :Geometry;
import :ShaderRegistry;
import :PipelineLibrary;
import Core;
import ECS;
import RHI;

export namespace Graphics
{
    // Forward declaration (keeps include graph minimal).
    class GPUScene;

    // ---------------------------------------------------------------------
    // RenderBlackboard
    // ---------------------------------------------------------------------
    // A tiny, low-ceremony frame-local dictionary for sharing RenderGraph
    // resource handles between features.
    //
    // NOTE: We keep it StringID-keyed (not std::string) to avoid allocations
    // and because the engine already uses hashed IDs heavily.
    struct RenderBlackboard
    {
        std::unordered_map<Core::Hash::StringID, RGResourceHandle> Resources;

        void Add(Core::Hash::StringID name, RGResourceHandle handle)
        {
            Resources[name] = handle;
        }

        [[nodiscard]] RGResourceHandle Get(Core::Hash::StringID name) const
        {
            if (auto it = Resources.find(name); it != Resources.end())
                return it->second;
            return {};
        }
    };

    // ---------------------------------------------------------------------
    // Frame Context
    // ---------------------------------------------------------------------
    struct RenderPassContext
    {
        RenderGraph& Graph;
        RenderBlackboard& Blackboard;

        // Scene
        ECS::Scene& Scene;
        const Core::Assets::AssetManager& AssetManager;
        GeometryPool& GeometryStorage;
        MaterialSystem& MaterialSystem;

        // Retained-mode GPU scene (owned by RenderSystem).
        GPUScene* GpuScene = nullptr;

        // Frame
        uint32_t FrameIndex = 0;
        VkExtent2D Resolution{};

        // Swapchain/backbuffer import context
        uint32_t SwapchainImageIndex = 0;
        VkFormat SwapchainFormat = VK_FORMAT_UNDEFINED;

        // Command submission helpers
        RHI::SimpleRenderer& Renderer;

        // Global GPU state
        RHI::VulkanBuffer* GlobalCameraUBO = nullptr;
        VkDescriptorSet GlobalDescriptorSet = VK_NULL_HANDLE;
        size_t GlobalCameraDynamicOffset = 0; // byte offset (dynamic UBO)

        RHI::BindlessDescriptorSystem& Bindless;

        // Interaction state
        struct PickRequestState
        {
            bool Pending = false;
            uint32_t X = 0;
            uint32_t Y = 0;
        } PickRequest;

        // Debug state
        struct DebugState
        {
            bool Enabled = false;
            bool ShowInViewport = false;
            Core::Hash::StringID SelectedResource = Core::Hash::StringID{"PickID"};
            float DepthNear = 0.1f;
            float DepthFar = 1000.0f;
        } Debug;

        // Previous frame compiled debug lists (used because features run before compile).
        std::span<const RenderGraphDebugImage> PrevFrameDebugImages{};
        std::span<const RenderGraphDebugPass> PrevFrameDebugPasses{};

        // CPU camera matrices for passes that need analytic camera data (e.g., frustum culling).
        // These mirror what is uploaded to RHI::CameraBufferObject each frame.
        glm::mat4 CameraView{1.0f};
        glm::mat4 CameraProj{1.0f};

        // Picking readback destination for *this frame slot* (owned by RenderSystem).
        // The PickingPass uses this for vkCmdCopyImageToBuffer.
        RHI::VulkanBuffer* PickReadbackBuffer = nullptr;
    };

    // ---------------------------------------------------------------------
    // Feature interface
    // ---------------------------------------------------------------------
    class IRenderFeature
    {
    public:
        virtual ~IRenderFeature() = default;

        // Called once at startup (create pipelines, persistent GPU resources).
        virtual void Initialize(RHI::VulkanDevice& device,
                                RHI::DescriptorAllocator& descriptorPool,
                                RHI::DescriptorLayout& globalLayout) = 0;

        // Called every frame to inject passes into the graph.
        virtual void AddPasses(RenderPassContext& ctx) = 0;

        // Optional lifecycle.
        virtual void Shutdown() {}
        virtual void OnResize(uint32_t width, uint32_t height) { (void)width; (void)height; }
    };

    // ---------------------------------------------------------------------
    // RenderPipeline interface (owns features, hot-swappable)
    // ---------------------------------------------------------------------
    class RenderPipeline
    {
    public:
        virtual ~RenderPipeline() = default;

        // Called when this pipeline becomes active.
        virtual void Initialize(RHI::VulkanDevice& device,
                                RHI::DescriptorAllocator& descriptorPool,
                                RHI::DescriptorLayout& globalLayout,
                                const ShaderRegistry& shaderRegistry,
                                PipelineLibrary& pipelineLibrary) = 0;

        virtual void Shutdown() {}

        // Declare passes for this frame.
        virtual void SetupFrame(RenderPassContext& ctx) = 0;

        virtual void OnResize(uint32_t width, uint32_t height) { (void)width; (void)height; }

        // Called after RenderGraph::Compile() but before Execute().
        virtual void PostCompile(uint32_t frameIndex,
                                 std::span<const RenderGraphDebugImage> debugImages,
                                 std::span<const RenderGraphDebugPass> debugPasses)
        {
            (void)frameIndex;
            (void)debugImages;
            (void)debugPasses;
        }
    };
}
