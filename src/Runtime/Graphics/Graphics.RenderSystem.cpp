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
        : m_Device(device), m_Swapchain(swapchain), m_Renderer(renderer), m_Pipeline(pipeline), m_RenderGraph(device)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &props);
        m_MinUboAlignment = props.limits.minUniformBufferOffsetAlignment;

        // 2. Calculate aligned size for ONE frame
        size_t cameraDataSize = sizeof(RHI::CameraBufferObject);
        size_t alignedSize = PadUniformBufferSize(cameraDataSize, m_MinUboAlignment);

        // Create the UBO once here
        m_GlobalUBO = new RHI::VulkanBuffer(
            device,
            alignedSize * RHI::SimpleRenderer::MAX_FRAMES_IN_FLIGHT,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
    }

    RenderSystem::~RenderSystem()
    {
        delete m_GlobalUBO;
    }

    struct ForwardPassData
    {
        Graph::RGResourceHandle Color;
        Graph::RGResourceHandle Depth;
    };

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

            size_t cameraDataSize = sizeof(RHI::CameraBufferObject);
            size_t alignedSize = PadUniformBufferSize(cameraDataSize, m_MinUboAlignment);
            uint32_t frameIndex = m_Renderer.GetCurrentFrameIndex();
            size_t offset = frameIndex * alignedSize;

            char* data = (char*)m_GlobalUBO->Map();
            memcpy(data + offset, &ubo, cameraDataSize);
            m_GlobalUBO->Unmap();

            // 3. Setup Render Graph
            m_RenderGraph.Reset();

            auto extent = m_Swapchain.GetExtent();
            uint32_t imageIndex = m_Renderer.GetImageIndex();

            m_RenderGraph.AddPass<ForwardPassData>("ForwardPass",
                [&](ForwardPassData& data, Graph::RGBuilder& builder)
                {
                    VkImage swapImage = m_Renderer.GetSwapchainImage(imageIndex);
                    VkImageView swapView = m_Renderer.GetSwapchainImageView(imageIndex);

                    // Use explicit format from swapchain
                    auto importedColor = builder.ImportTexture("Backbuffer", swapImage, swapView, m_Swapchain.GetImageFormat(), extent);

                    Graph::RGTextureDesc depthDesc{};
                    depthDesc.Width = extent.width;
                    depthDesc.Height = extent.height;
                    // We don't set format here, RenderGraph will auto-detect
                    auto depth = builder.CreateTexture("DepthBuffer", depthDesc);

                    Graph::RGAttachmentInfo colorInfo{};
                    colorInfo.ClearValue = {{{0.1f, 0.3f, 0.6f, 1.0f}}};

                    Graph::RGAttachmentInfo depthInfo{};
                    depthInfo.ClearValue.depthStencil = {1.0f, 0};

                    data.Color = builder.WriteColor(importedColor, colorInfo);
                    data.Depth = builder.WriteDepth(depth, depthInfo);
                },
                [&, offset](const ForwardPassData&, const Graph::RGRegistry&, VkCommandBuffer cmd)
                {
                    m_Renderer.BindPipeline(m_Pipeline);
                    m_Renderer.SetViewport(extent.width, extent.height);

                    auto view = scene.GetRegistry().view<ECS::TransformComponent, ECS::MeshRendererComponent>();
                    for (auto [entity, transform, renderable] : view.each())
                    {
                        if (!renderable.MeshRef || !renderable.MaterialRef) continue;

                        VkDescriptorSet sets[] = {renderable.MaterialRef->GetDescriptorSet()};
                        uint32_t dynamicOffset = static_cast<uint32_t>(offset);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetLayout(), 0, 1, sets, 1, &dynamicOffset);

                        RHI::MeshPushConstants push{};
                        push.model = transform.GetTransform();
                        vkCmdPushConstants(cmd, m_Pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

                        VkBuffer vBuffers[] = {renderable.MeshRef->GetVertexBuffer()->GetHandle()};
                        VkDeviceSize offsets[] = {0};
                        vkCmdBindVertexBuffers(cmd, 0, 1, vBuffers, offsets);
                        vkCmdBindIndexBuffer(cmd, renderable.MeshRef->GetIndexBuffer()->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(cmd, renderable.MeshRef->GetIndexCount(), 1, 0, 0, 0);
                    }
                }
            );

            // 4. Compile & Execute with Frame Index
            m_RenderGraph.Compile(frameIndex);
            m_RenderGraph.Execute(m_Renderer.GetCommandBuffer());

            // 5. End Frame
            m_Renderer.EndFrame();
        }
    }
}
