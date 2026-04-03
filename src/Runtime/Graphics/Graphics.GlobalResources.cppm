module;
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

export module Graphics.GlobalResources;

import RHI.Bindless;
import RHI.Buffer;
import RHI.Descriptors;
import RHI.Device;
import RHI.Image;
import RHI.TransientAllocator;
import Graphics.Camera;
import Graphics.RenderPipeline;
import Graphics.ShaderRegistry;
import Graphics.PipelineLibrary;

export namespace Graphics
{
    // ---------------------------------------------------------------------
    // Global Resources
    // ---------------------------------------------------------------------
    // Holds shared GPU resources that are constant across the frame or
    // shared by all passes (Camera UBOs, Transient Allocators, Global Sets).
    class GlobalResources
    {
    public:
        GlobalResources(std::shared_ptr<RHI::VulkanDevice> device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout& descriptorLayout,
                        RHI::BindlessDescriptorSystem& bindlessSystem,
                        const ShaderRegistry& shaderRegistry,
                        PipelineLibrary& pipelineLibrary,
                        uint32_t framesInFlight);
        ~GlobalResources();

        // Called at the start of the frame to update camera + lighting data.
        void Update(const CameraComponent& camera, const LightEnvironmentPacket& lighting, uint32_t frameIndex);

        // Called to reset per-frame transient allocators.
        void BeginFrame(uint32_t frameIndex);

        // Update the shadow atlas sampler binding in the global descriptor set.
        // Called after render graph compilation when the actual atlas image view
        // is available. Pass VK_NULL_HANDLE to bind the dummy (shadows disabled).
        void UpdateShadowAtlasBinding(VkImageView atlasView);

        // -----------------------------------------------------------------
        // Accessors
        // -----------------------------------------------------------------
        [[nodiscard]] RHI::VulkanBuffer* GetCameraUBO() const { return m_CameraUBO.get(); }
        [[nodiscard]] VkDescriptorSet GetGlobalDescriptorSet() const { return m_GlobalDescriptorSet; }
        [[nodiscard]] uint32_t GetDynamicUBOOffset(uint32_t frameIndex) const;

        [[nodiscard]] RHI::TransientAllocator& GetTransientAllocator() { return *m_TransientAllocator; }
        
        [[nodiscard]] RHI::BindlessDescriptorSystem& GetBindlessSystem() const { return m_BindlessSystem; }
        [[nodiscard]] const ShaderRegistry& GetShaderRegistry() const { return m_ShaderRegistry; }
        [[nodiscard]] PipelineLibrary& GetPipelineLibrary() const { return m_PipelineLibrary; }
        [[nodiscard]] RHI::DescriptorAllocator& GetDescriptorPool() const { return m_DescriptorPool; }
        [[nodiscard]] RHI::DescriptorLayout& GetDescriptorLayout() const { return m_DescriptorLayout; }

    private:
        std::shared_ptr<RHI::VulkanDevice> m_Device;
        
        // References to Engine-owned systems
        RHI::DescriptorAllocator& m_DescriptorPool;
        RHI::DescriptorLayout& m_DescriptorLayout;
        RHI::BindlessDescriptorSystem& m_BindlessSystem;
        const ShaderRegistry& m_ShaderRegistry;
        PipelineLibrary& m_PipelineLibrary;

        // Owned Resources
        std::unique_ptr<RHI::VulkanBuffer> m_CameraUBO;
        VkDescriptorSet m_GlobalDescriptorSet = VK_NULL_HANDLE;
        
        // Transient GPU memory (page allocator).
        std::unique_ptr<RHI::TransientAllocator> m_TransientAllocator;

        // Shadow atlas comparison sampler for hardware-accelerated PCF.
        VkSampler m_ShadowComparisonSampler = VK_NULL_HANDLE;

        // 1x1 dummy depth image for safe initial shadow atlas binding.
        std::unique_ptr<RHI::VulkanImage> m_DummyShadowImage;

        size_t m_MinUboAlignment = 0;
        size_t m_CameraDataSize = 0;
        size_t m_CameraAlignedSize = 0;
    };
}
