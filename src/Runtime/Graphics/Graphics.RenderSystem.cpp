module;
#include <cstring>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <RHI/RHI.Vulkan.hpp>

module Runtime.Graphics.RenderSystem;
import Runtime.RHI.Types;
import Runtime.ECS.Components;

namespace Runtime::Graphics
{
    inline size_t PadUniformBufferSize(size_t originalSize, size_t minAlignment)
    {
        if (minAlignment > 0)
        {
            return (originalSize + minAlignment - 1) & ~(minAlignment - 1);
        }
        return originalSize;
    }

    RenderSystem::RenderSystem(RHI::VulkanDevice& device,
                               RHI::VulkanSwapchain& swapchain,
                               RHI::SimpleRenderer& renderer,
                               RHI::GraphicsPipeline& pipeline)
        : m_Device(device), m_Swapchain(swapchain), m_Renderer(renderer), m_Pipeline(pipeline)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &props);
        m_MinUboAlignment = props.limits.minUniformBufferOffsetAlignment;

        // 2. Calculate aligned size for ONE frame
        size_t cameraDataSize = sizeof(RHI::CameraBufferObject);
        size_t alignedSize = PadUniformBufferSize(cameraDataSize, m_MinUboAlignment);
        m_CameraStride = alignedSize;

        // Create the UBO once here
        m_GlobalUBO = std::make_unique<RHI::VulkanBuffer>(
            device,
            alignedSize * RHI::SimpleRenderer::MAX_FRAMES_IN_FLIGHT,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        m_MappedCameraPtr = static_cast<char*>(m_GlobalUBO->Map());
    }

    RenderSystem::~RenderSystem()
    {
        if (m_GlobalUBO && m_MappedCameraPtr)
        {
            m_GlobalUBO->Unmap();
        }
    }

    void RenderSystem::OnUpdate(ECS::Scene& scene, const CameraData& camera)
    {
        // 1. Begin Frame
        m_Renderer.BeginFrame();
        if (m_Renderer.IsFrameInProgress())
        {
            // 2. Update Global UBO
            RHI::CameraBufferObject ubo{};
            ubo.view = camera.View;
            ubo.proj = camera.Proj;
            const size_t cameraDataSize = sizeof(RHI::CameraBufferObject);

            // Offset based on CURRENT FRAME
            uint32_t frameIndex = m_Renderer.GetCurrentFrameIndex();
            size_t offset = frameIndex * m_CameraStride;

            // Write to specific offset
            std::memcpy(m_MappedCameraPtr + offset, &ubo, cameraDataSize);

            m_Renderer.BindPipeline(m_Pipeline);

            auto extent = m_Swapchain.GetExtent();
            m_Renderer.SetViewport(extent.width, extent.height);

            // In a real engine, we would sort entities by Material to minimize state changes.
            auto view = scene.GetRegistry().view<
                ECS::TransformComponent, ECS::MeshRendererComponent>();

            for (auto [entity, transform, renderable] : view.each())
            {
                if (!renderable.MeshRef || !renderable.MaterialRef) continue;

                // Bind Material (Descriptor Set)
                // Note: We lazily update the descriptor to point to our Global UBO if needed, 
                // but ideally, we do this once when material is created. 
                // For this refactor, assume Material::WriteDescriptor was called during setup.

                VkDescriptorSet sets[] = {renderable.MaterialRef->GetDescriptorSet()};
                uint32_t dynamicOffset = static_cast<uint32_t>(offset);
                vkCmdBindDescriptorSets(
                    m_Renderer.GetCommandBuffer(),
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_Pipeline.GetLayout(),
                    0, 1, sets,
                    1, &dynamicOffset
                );

                // Push Constants
                RHI::MeshPushConstants push{};
                push.model = transform.GetTransform();

                vkCmdPushConstants(
                    m_Renderer.GetCommandBuffer(),
                    m_Pipeline.GetLayout(),
                    VK_SHADER_STAGE_VERTEX_BIT,
                    0, sizeof(push), &push
                );

                // Draw Mesh
                VkBuffer vBuffers[] = {renderable.MeshRef->GetVertexBuffer()->GetHandle()};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(m_Renderer.GetCommandBuffer(), 0, 1, vBuffers, offsets);
                vkCmdBindIndexBuffer(m_Renderer.GetCommandBuffer(), renderable.MeshRef->GetIndexBuffer()->GetHandle(),
                                     0, VK_INDEX_TYPE_UINT32);

                vkCmdDrawIndexed(m_Renderer.GetCommandBuffer(), renderable.MeshRef->GetIndexCount(), 1, 0, 0, 0);
            }

            m_Renderer.EndFrame();
        }
    }
}
