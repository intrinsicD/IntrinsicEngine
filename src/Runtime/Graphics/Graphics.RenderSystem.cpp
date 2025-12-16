module;
#include <cstring>
#include <limits>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
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

    struct DrawInstance
    {
        glm::mat4 Model;
    };

    struct DrawBatch
    {
        GeometryPtr Geometry;
        MaterialPtr Material;
        std::vector<DrawInstance> Instances;
    };

    struct DrawKey
    {
        GeometryPtr Geometry;
        MaterialPtr Material;

        bool operator==(const DrawKey&) const = default;
    };

    struct DrawKeyHash
    {
        size_t operator()(const DrawKey& key) const noexcept
        {
            size_t seed = reinterpret_cast<size_t>(key.Geometry.get());
            seed ^= reinterpret_cast<size_t>(key.Material.get()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    static void AppendInstancesFromScene(ECS::Scene& scene, std::vector<DrawBatch>& batches)
    {
        std::unordered_map<DrawKey, uint32_t, DrawKeyHash> batchLookup;

        auto view = scene.GetRegistry().view<ECS::Transform::Component, ECS::MeshRenderer::Component>();
        for (auto [entity, transform, renderable] : view.each())
        {
            if (!renderable.GeometryRef || !renderable.MaterialRef)
                continue;

            DrawKey key{renderable.GeometryRef, renderable.MaterialRef};
            auto iter = batchLookup.find(key);
            if (iter == batchLookup.end())
            {
                uint32_t index = static_cast<uint32_t>(batches.size());
                batchLookup.emplace(key, index);

                DrawBatch& batch = batches.emplace_back();
                batch.Geometry = renderable.GeometryRef;
                batch.Material = renderable.MaterialRef;
                batch.Instances.push_back({transform.GetTransform()});
            }
            else
            {
                batches[iter->second].Instances.push_back({transform.GetTransform()});
            }
        }
    }

    static void SubmitBatches(const std::vector<DrawBatch>& batches,
                              RHI::SimpleRenderer& renderer,
                              RHI::GraphicsPipeline& pipeline,
                              uint32_t dynamicOffset,
                              VkCommandBuffer cmd)
    {
        renderer.BindPipeline(pipeline);

        for (const DrawBatch& batch : batches)
        {
            if (!batch.Geometry || !batch.Material || batch.Instances.empty())
                continue;

            VkDescriptorSet sets[] = {
                batch.Material->GetDescriptorSet()
            };
            vkCmdBindDescriptorSets(
                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline.GetLayout(), 0, 1, sets, 1, &dynamicOffset);

            RHI::MeshPushConstants push{};

            auto* geo = batch.Geometry.get();
            auto* vBuf = geo->GetVertexBuffer()->GetHandle();
            const auto& layout = geo->GetLayout();

            VkBuffer vBuffers[] = {
                vBuf, vBuf, vBuf
            };
            VkDeviceSize offsets[] = {
                layout.PositionsOffset,
                layout.NormalsOffset,
                layout.AuxOffset
            };
            vkCmdBindVertexBuffers(cmd, 0, 3, vBuffers, offsets);

            VkPrimitiveTopology vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            switch (geo->GetTopology())
            {
            case PrimitiveTopology::Lines: vkTopo = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
            case PrimitiveTopology::Points: vkTopo = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
            default: break;
            }
            vkCmdSetPrimitiveTopology(cmd, vkTopo);

            if (geo->GetIndexCount() > 0)
            {
                vkCmdBindIndexBuffer(cmd, geo->GetIndexBuffer()->GetHandle(), 0, VK_INDEX_TYPE_UINT32);

                for (const DrawInstance& instance : batch.Instances)
                {
                    push.model = instance.Model;
                    vkCmdPushConstants(cmd, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                    vkCmdDrawIndexed(cmd, geo->GetIndexCount(), 1, 0, 0, 0);
                }
            }
            else
            {
                uint32_t vertCount = static_cast<uint32_t>(layout.PositionsSize / sizeof(glm::vec3));
                for (const DrawInstance& instance : batch.Instances)
                {
                    push.model = instance.Model;
                    vkCmdPushConstants(cmd, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                    vkCmdDraw(cmd, vertCount, 1, 0, 0);
                }
            }
        }
    }

    void RenderSystem::OnUpdate(ECS::Scene& scene, const CameraComponent& camera)
    {
        Interface::GUI::BeginFrame();

        Interface::GUI::DrawGUI();

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

            char* dataPtr = static_cast<char*>(m_GlobalUBO->Map());
            memcpy(dataPtr + offset, &ubo, cameraDataSize);

            m_RenderGraph.Reset();

            auto extent = m_Swapchain.GetExtent();
            uint32_t imageIndex = m_Renderer.GetImageIndex();

            std::vector<DrawBatch> drawList;
            AppendInstancesFromScene(scene, drawList);

            Graph::RGResourceHandle backbufferHandle{};

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
                                                       backbufferHandle = data.Color;
                                                   },
                                                   [&, offset](const ForwardPassData&, const Graph::RGRegistry&,
                                                               VkCommandBuffer cmd)
                                                   {
                                                       m_Renderer.SetViewport(extent.width, extent.height);

                                                       if (offset > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
                                                       {
                                                           Core::Log::Error(
                                                               "UBO offset overflow! Offset {} exceeds uint32_t max",
                                                               offset);
                                                           return;
                                                       }

                                                       SubmitBatches(drawList, m_Renderer, m_Pipeline,
                                                                     static_cast<uint32_t>(offset), cmd);
                                                   }
            );

            m_RenderGraph.AddPass<ImGuiPassData>("ImGuiPass",
                                                 [&](ImGuiPassData& data, Graph::RGBuilder& builder)
                                                 {
                                                     Graph::RGAttachmentInfo colorInfo{};
                                                     colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                                                     colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;

                                                     data.Backbuffer = builder.WriteColor(backbufferHandle, colorInfo);
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
