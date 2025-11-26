module;
#include <cstring>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include "RHI/RHI.Vulkan.hpp"
#include <imgui.h>

module Runtime.Graphics.RenderSystem;

import Core.Logging;
import Core.Memory;
import Core.Assets;
import Runtime.RHI.Types;
import Runtime.ECS.Components;
import Runtime.Graphics.Camera;
import Runtime.Interface.GUI;

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

    RenderSystem::RenderSystem(std::shared_ptr<RHI::VulkanDevice> device,
                               RHI::VulkanSwapchain& swapchain,
                               RHI::SimpleRenderer& renderer,
                               RHI::GraphicsPipeline& pipeline,
                               Core::Memory::LinearArena& frameArena)
        : m_Device(device),
          m_Swapchain(swapchain),
          m_Renderer(renderer),
          m_Pipeline(pipeline),
          m_RenderGraph(device, frameArena)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device->GetPhysicalDevice(), &props);
        m_MinUboAlignment = props.limits.minUniformBufferOffsetAlignment;

        // 2. Calculate aligned size for ONE frame
        size_t cameraDataSize = sizeof(RHI::CameraBufferObject);
        size_t alignedSize = PadUniformBufferSize(cameraDataSize, m_MinUboAlignment);

        // Create the UBO once here
        m_GlobalUBO = std::make_unique<RHI::VulkanBuffer>(
            device,
            alignedSize * RHI::SimpleRenderer::MAX_FRAMES_IN_FLIGHT,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
    }

    RenderSystem::~RenderSystem()
    {
        //keep for persistent camera data mapping = nullptr on destruction?
    }

    struct ImGuiPassData
    {
        Graph::RGResourceHandle Backbuffer;
    };

    struct ForwardPassData
    {
        Graph::RGResourceHandle Color;
        Graph::RGResourceHandle Depth;
    };

    void RenderSystem::OnUpdate(ECS::Scene& scene, const Camera& camera, Core::Assets::AssetManager& assetManager)
    {
        Interface::GUI::BeginFrame();

        {
            ImGui::Begin("Renderer Stats", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("Application Average: %.3f ms/frame (%.1f FPS)",
                        1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

            static glm::vec3 sunDir = {1.0, 1.0, 1.0};
            ImGui::DragFloat3("Sun Direction", &sunDir.x, 0.1f);

            // Camera info
            ImGui::Separator();
            glm::vec3 camPos = camera.Position;
            ImGui::Text("Camera Pos: %.2f, %.2f, %.2f", camPos.x, camPos.y, camPos.z);

            ImGui::End();
        }

        m_Renderer.BeginFrame();

        if (m_Renderer.IsFrameInProgress())
        {
            RHI::CameraBufferObject ubo{};
            ubo.view = camera.ViewMatrix;
            ubo.proj = camera.ProjectionMatrix;

            size_t cameraDataSize = sizeof(RHI::CameraBufferObject);
            size_t alignedSize = PadUniformBufferSize(cameraDataSize, m_MinUboAlignment);
            uint32_t frameIndex = m_Renderer.GetCurrentFrameIndex();
            size_t offset = frameIndex * alignedSize;

            char* data = (char*)m_GlobalUBO->Map();
            memcpy(data + offset, &ubo, cameraDataSize);
            m_GlobalUBO->Unmap();

            m_RenderGraph.Reset();

            auto extent = m_Swapchain.GetExtent();
            uint32_t imageIndex = m_Renderer.GetImageIndex();

            Graph::RGResourceHandle forwardOutput;

            m_RenderGraph.AddPass<ForwardPassData>("ForwardPass",
                                                   [&](ForwardPassData& data, Graph::RGBuilder& builder)
                                                   {
                                                       VkImage swapImage = m_Renderer.GetSwapchainImage(imageIndex);
                                                       VkImageView swapView = m_Renderer.GetSwapchainImageView(
                                                           imageIndex);

                                                       // Use explicit format from swapchain
                                                       auto importedColor = builder.ImportTexture(
                                                           "Backbuffer", swapImage, swapView,
                                                           m_Swapchain.GetImageFormat(), extent);

                                                       Graph::RGTextureDesc depthDesc{};
                                                       depthDesc.Width = extent.width;
                                                       depthDesc.Height = extent.height;
                                                       // We don't set format here, RenderGraph will auto-detect
                                                       auto depth = builder.CreateTexture("DepthBuffer", depthDesc);

                                                       Graph::RGAttachmentInfo colorInfo{};
                                                       colorInfo.ClearValue = {{{0.1f, 0.3f, 0.6f, 1.0f}}};
                                                       colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                                       colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;

                                                       Graph::RGAttachmentInfo depthInfo{};
                                                       depthInfo.ClearValue.depthStencil = {1.0f, 0};

                                                       data.Color = builder.WriteColor(importedColor, colorInfo);
                                                       data.Depth = builder.WriteDepth(depth, depthInfo);

                                                       forwardOutput = data.Color;
                                                   },
                                                   [&, offset](const ForwardPassData&, const Graph::RGRegistry&,
                                                               VkCommandBuffer cmd)
                                                   {
                                                       m_Renderer.BindPipeline(m_Pipeline);
                                                       m_Renderer.SetViewport(extent.width, extent.height);

                                                       auto view = scene.GetRegistry().view<
                                                           ECS::TransformComponent, ECS::MeshRendererComponent>();
                                                       for (auto [entity, transform, renderable] : view.each())
                                                       {
                                                           if (!renderable.MeshRef || !renderable.MaterialRef) continue;

                                                           VkDescriptorSet sets[] = {
                                                               renderable.MaterialRef->GetDescriptorSet()
                                                           };
                                                           // Check for overflow before casting to uint32_t
                                                           if (offset > static_cast<size_t>(std::numeric_limits<
                                                               uint32_t>::max()))
                                                           {
                                                               Core::Log::Error(
                                                                   "UBO offset overflow! Offset {} exceeds uint32_t max",
                                                                   offset);
                                                               continue;
                                                           }
                                                           uint32_t dynamicOffset = static_cast<uint32_t>(offset);
                                                           vkCmdBindDescriptorSets(
                                                               cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                               m_Pipeline.GetLayout(), 0, 1, sets, 1, &dynamicOffset);

                                                           RHI::MeshPushConstants push{};
                                                           push.model = transform.GetTransform();
                                                           vkCmdPushConstants(
                                                               cmd, m_Pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT,
                                                               0, sizeof(push), &push);

                                                           VkBuffer vBuffers[] = {
                                                               renderable.MeshRef->GetVertexBuffer()->GetHandle()
                                                           };
                                                           VkDeviceSize offsets[] = {0};
                                                           vkCmdBindVertexBuffers(cmd, 0, 1, vBuffers, offsets);
                                                           vkCmdBindIndexBuffer(
                                                               cmd, renderable.MeshRef->GetIndexBuffer()->GetHandle(),
                                                               0, VK_INDEX_TYPE_UINT32);
                                                           vkCmdDrawIndexed(
                                                               cmd, renderable.MeshRef->GetIndexCount(), 1, 0, 0, 0);
                                                       }
                                                   }
            );

            m_RenderGraph.AddPass<ImGuiPassData>("ImGuiPass",
                                                 [&](ImGuiPassData& data, Graph::RGBuilder& builder)
                                                 {
                                                     // Re-import backbuffer? No, we need to handle dependency.
                                                     // RGBuilder doesn't have a generic "GetResourceByName", so we rely on importing
                                                     // or storing the handle in the class.
                                                     // However, we can just re-import the swapchain logic since the RG resolves tracking.
                                                     // But ideally, we chain the handle.

                                                     // Hack for prototype: Re-import swapchain image.
                                                     // In a real RG, ForwardPassData would return the handle, we pass it here.
                                                     // Since we are inside OnUpdate, we can just grab the handle if we stored it outside,
                                                     // but we didn't.
                                                     // Let's just import "Backbuffer" again. The RenderGraph names resolve to IDs.
                                                     // If RG implementation uses names for lookup, this works.
                                                     // If not, we should store ForwardPassData outside.

                                                     VkImage swapImage = m_Renderer.GetSwapchainImage(imageIndex);
                                                     VkImageView swapView = m_Renderer.
                                                         GetSwapchainImageView(imageIndex);

                                                     // We need to Load (keep content) and Store (present)
                                                     Graph::RGAttachmentInfo colorInfo{};
                                                     colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                                                     colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;

                                                     auto backbuffer = builder.ImportTexture(
                                                         "Backbuffer", swapImage, swapView,
                                                         m_Swapchain.GetImageFormat(), extent);
                                                     data.Backbuffer = builder.WriteColor(backbuffer, colorInfo);
                                                 },
                                                 [](const ImGuiPassData&, const Graph::RGRegistry&, VkCommandBuffer cmd)
                                                 {
                                                     Interface::GUI::Render(cmd);
                                                 }
            );

            m_RenderGraph.Compile(frameIndex);
            m_RenderGraph.Execute(m_Renderer.GetCommandBuffer());
            m_Renderer.EndFrame();
        }
    }
}
