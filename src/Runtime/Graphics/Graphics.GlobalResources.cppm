module;
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

export module Graphics:GlobalResources;

import RHI;
import :Camera;
import :ShaderRegistry;
import :PipelineLibrary;
import Core;

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

        // Called at the start of the frame to update camera data.
        void Update(const CameraComponent& camera, uint32_t frameIndex);

        // Called to reset per-frame transient allocators.
        void BeginFrame(uint32_t frameIndex);

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

        size_t m_MinUboAlignment = 0;
        size_t m_CameraDataSize = 0;
        size_t m_CameraAlignedSize = 0;
    };
}
