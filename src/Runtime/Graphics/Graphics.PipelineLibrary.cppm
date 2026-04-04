module;

#include <memory>
#include <optional>
#include <functional>

#include "RHI.Vulkan.hpp"

export module Graphics.PipelineLibrary;

import Core.Hash;
import RHI.Bindless;
import RHI.ComputePipeline;
import RHI.Descriptors;
import RHI.Device;
import RHI.Pipeline;
import Graphics.ShaderRegistry;

export namespace Graphics
{
    // Named PSO library.
    // Contract: built during init, optionally rebuilt on shader-reload / swapchain format changes.
    // Fast path: Get() is O(1) hash map lookup.
    class PipelineLibrary
    {
    public:
        PipelineLibrary(std::shared_ptr<RHI::VulkanDevice> device,
                        RHI::BindlessDescriptorSystem& bindless,
                        RHI::DescriptorLayout& globalSetLayout);

        ~PipelineLibrary();

        // Build the engine's baseline pipelines (Forward + Picking), keyed by stable IDs.
        // This is intended to be called from the composition root.
        // sceneColorFormat: format for scene rendering passes (HDR R16G16B16A16_SFLOAT).
        // swapchainFormat: format for UI/overlay passes (Picking writes R32_UINT).
        void BuildDefaults(const ShaderRegistry& shaderRegistry,
                           VkFormat swapchainFormat,
                           VkFormat depthFormat,
                           VkFormat sceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT);

        // Rebuild all graphics pipelines from the current ShaderRegistry SPV paths.
        // Caller must ensure no GPU work referencing old pipelines is in flight.
        // Exits the process on critical pipeline build failure (same as BuildDefaults).
        void RebuildGraphicsPipelines(const ShaderRegistry& shaderRegistry,
                                      VkFormat swapchainFormat,
                                      VkFormat depthFormat,
                                      VkFormat sceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT);

        // Retrieve a pipeline by name.
        [[nodiscard]] std::optional<std::reference_wrapper<RHI::GraphicsPipeline>>
        TryGet(Core::Hash::StringID name);

        [[nodiscard]] std::optional<std::reference_wrapper<const RHI::GraphicsPipeline>>
        TryGet(Core::Hash::StringID name) const;

        [[nodiscard]] RHI::GraphicsPipeline& GetOrDie(Core::Hash::StringID name);
        [[nodiscard]] const RHI::GraphicsPipeline& GetOrDie(Core::Hash::StringID name) const;

        [[nodiscard]] bool Contains(Core::Hash::StringID name) const;

        // Stage 1: set=2 layout for instance + visibility SSBOs used by Forward shaders.
        [[nodiscard]] VkDescriptorSetLayout GetStage1InstanceSetLayout() const;

        // Stage 3: compute culling pipeline + set layout (set = 0 in compute pipeline).
        [[nodiscard]] VkDescriptorSetLayout GetCullSetLayout() const;
        [[nodiscard]] RHI::ComputePipeline* GetCullPipeline() const;

        // GPUScene: scatter updates pipeline + set layout (set = 0).
        [[nodiscard]] VkDescriptorSetLayout GetSceneUpdateSetLayout() const;
        [[nodiscard]] RHI::ComputePipeline* GetSceneUpdatePipeline() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
    using namespace Core::Hash;
    // Canonical pipeline IDs used across the engine.
    // (You can move these into a dedicated IDs module later.)
    inline constexpr StringID kPipeline_Surface = "Pipeline.Surface"_id;
    inline constexpr StringID kPipeline_SurfaceLines = "Pipeline.SurfaceLines"_id;
    inline constexpr StringID kPipeline_SurfacePoints = "Pipeline.SurfacePoints"_id;
    inline constexpr StringID kPipeline_Picking = "Pipeline.Picking"_id;
    inline constexpr StringID kPipeline_PickMesh = "Pipeline.PickMesh"_id;
    inline constexpr StringID kPipeline_PickLine = "Pipeline.PickLine"_id;
    inline constexpr StringID kPipeline_PickPoint = "Pipeline.PickPoint"_id;
    inline constexpr StringID kPipeline_SurfaceGBuffer = "Pipeline.SurfaceGBuffer"_id;
    inline constexpr StringID kPipeline_DebugSurface = "Pipeline.DebugSurface"_id;
    inline constexpr StringID kPipeline_DepthPrepass = "Pipeline.DepthPrepass"_id;
    inline constexpr StringID kPipeline_ShadowDepth = "Pipeline.ShadowDepth"_id;
}
