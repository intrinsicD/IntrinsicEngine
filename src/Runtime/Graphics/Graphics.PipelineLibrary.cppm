module;

#include <memory>
#include <unordered_map>
#include <optional>

#include "RHI.Vulkan.hpp"

export module Graphics:PipelineLibrary;

import Core.Hash;
import RHI;
import :ShaderRegistry;

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
        void BuildDefaults(const ShaderRegistry& shaderRegistry,
                           VkFormat swapchainFormat,
                           VkFormat depthFormat);

        // Retrieve a pipeline by name.
        [[nodiscard]] std::optional<std::reference_wrapper<RHI::GraphicsPipeline>>
        TryGet(Core::Hash::StringID name);

        [[nodiscard]] std::optional<std::reference_wrapper<const RHI::GraphicsPipeline>>
        TryGet(Core::Hash::StringID name) const;

        [[nodiscard]] RHI::GraphicsPipeline& GetOrDie(Core::Hash::StringID name);
        [[nodiscard]] const RHI::GraphicsPipeline& GetOrDie(Core::Hash::StringID name) const;

        [[nodiscard]] bool Contains(Core::Hash::StringID name) const { return m_Pipelines.contains(name); }

        // Stage 1: set=2 layout for instance + visibility SSBOs used by Forward shaders.
        [[nodiscard]] VkDescriptorSetLayout GetStage1InstanceSetLayout() const { return m_Stage1InstanceSetLayout; }

        // Stage 3: compute culling pipeline + set layout (set = 0 in compute pipeline).
        [[nodiscard]] VkDescriptorSetLayout GetCullSetLayout() const { return m_CullSetLayout; }
        [[nodiscard]] RHI::ComputePipeline* GetCullPipeline() const { return m_CullPipeline.get(); }

        // GPUScene: scatter updates pipeline + set layout (set = 0).
        [[nodiscard]] VkDescriptorSetLayout GetSceneUpdateSetLayout() const { return m_SceneUpdateSetLayout; }
        [[nodiscard]] RHI::ComputePipeline* GetSceneUpdatePipeline() const { return m_SceneUpdatePipeline.get(); }

    private:
        std::shared_ptr<RHI::VulkanDevice> m_DeviceOwner;
        RHI::VulkanDevice* m_Device = nullptr;

        RHI::BindlessDescriptorSystem& m_Bindless;
        RHI::DescriptorLayout& m_GlobalSetLayout;

        std::unordered_map<Core::Hash::StringID, std::unique_ptr<RHI::GraphicsPipeline>> m_Pipelines;

        VkDescriptorSetLayout m_Stage1InstanceSetLayout = VK_NULL_HANDLE;

        VkDescriptorSetLayout m_CullSetLayout = VK_NULL_HANDLE;
        std::unique_ptr<RHI::ComputePipeline> m_CullPipeline;

        VkDescriptorSetLayout m_SceneUpdateSetLayout = VK_NULL_HANDLE;
        std::unique_ptr<RHI::ComputePipeline> m_SceneUpdatePipeline;
    };
    using namespace Core::Hash;
    // Canonical pipeline IDs used across the engine.
    // (You can move these into a dedicated IDs module later.)
    inline constexpr StringID kPipeline_Forward = "Pipeline.Forward"_id;
    inline constexpr StringID kPipeline_Picking = "Pipeline.Picking"_id;
}
