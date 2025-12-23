module;
#include <cstring>
#include <vector>
#include <algorithm>
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
                               RHI::BindlessDescriptorSystem& bindlessSystem,
                               RHI::DescriptorPool& descriptorPool,
                               RHI::DescriptorLayout& descriptorLayout,
                               RHI::GraphicsPipeline& pipeline,
                               Core::Memory::LinearArena& frameArena,
                               GeometryStorage& geometryStorage)
        : m_Device(device),
          m_Swapchain(swapchain),
          m_Renderer(renderer),
          m_BindlessSystem(bindlessSystem),
          m_Pipeline(pipeline),
          m_RenderGraph(device, frameArena),
          m_GeometryStorage(geometryStorage)
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
            alignedSize * renderer.GetFramesInFlight(),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        //In RenderSystem constructor, allocate a set from m_DescriptorPool using m_DescriptorLayout (passed in), pointing to m_GlobalUBO.
        // 2. Allocate Descriptor Set (Set 0)
        m_GlobalDescriptorSet = descriptorPool.Allocate(descriptorLayout.GetHandle());

        // 3. Update Descriptor Set to point to UBO
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_GlobalUBO->GetHandle();
        bufferInfo.offset = 0; // We use dynamic offsets, so base offset is 0
        bufferInfo.range = sizeof(RHI::CameraBufferObject); // Size of ONE view

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_GlobalDescriptorSet;
        descriptorWrite.dstBinding = 0; // Binding 0 = Camera
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 1, &descriptorWrite, 0, nullptr);
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

    struct RenderPacket
    {
        GeometryHandle GeoHandle;
        uint32_t TextureID;
        glm::mat4 Transform;

        // Comparison for sorting
        bool operator<(const RenderPacket& other) const
        {
            if (GeoHandle != other.GeoHandle) return GeoHandle < other.GeoHandle;
            return TextureID < other.TextureID;
        }
    };

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
                                                       depthDesc.Format = VK_FORMAT_D32_SFLOAT;
                                                       depthDesc.Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                                           | VK_IMAGE_USAGE_SAMPLED_BIT;
                                                       depthDesc.Aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
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
                                                       m_Renderer.BindPipeline(m_Pipeline);
                                                       m_Renderer.SetViewport(extent.width, extent.height);

                                                       // 1. Bind Set 0: Global Camera (Dynamic Offset)
                                                       uint32_t dynamicOffset = static_cast<uint32_t>(offset);
                                                       vkCmdBindDescriptorSets(
                                                           cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                           m_Pipeline.GetLayout(),
                                                           0, 1, &m_GlobalDescriptorSet,
                                                           1, &dynamicOffset
                                                       );

                                                       // 2. Bind Set 1: Bindless Textures (Static)
                                                       VkDescriptorSet globalTextures = m_BindlessSystem.GetGlobalSet();
                                                       vkCmdBindDescriptorSets(
                                                           cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                           m_Pipeline.GetLayout(),
                                                           1, 1, &globalTextures,
                                                           0, nullptr
                                                       );
                                                       auto view = scene.GetRegistry().view<
                                                           ECS::Transform::Component, ECS::MeshRenderer::Component>();
                                                       std::vector<RenderPacket> packets;
                                                       packets.reserve(view.size_hint());
                                                       for (auto [entity, transform, renderable] : view.each())
                                                       {
                                                           if (!renderable.Geometry.IsValid() ||
                                                               !renderable.MaterialRef)
                                                               continue;
                                                           packets.push_back({
                                                               renderable.Geometry,
                                                               renderable.MaterialRef->GetTextureIndex(),
                                                               transform.GetTransform()
                                                           });
                                                       }

                                                       // 2. Sort to minimize state changes (Batching)
                                                       std::sort(packets.begin(), packets.end());

                                                       // 3. Draw Loop
                                                       GeometryHandle currentGeoHandle = {};
                                                       GeometryGpuData* currentGeo = nullptr;

                                                       for (const auto& packet : packets)
                                                       {
                                                           // Resolve Geometry only when it changes
                                                           if (packet.GeoHandle != currentGeoHandle)
                                                           {
                                                               currentGeo = m_GeometryStorage.Get(packet.GeoHandle);
                                                               currentGeoHandle = packet.GeoHandle;

                                                               if (!currentGeo) continue; // Invalid handle?

                                                               // Bind Vertex Buffers
                                                               auto* vBuf = currentGeo->GetVertexBuffer()->GetHandle();
                                                               const auto& layout = currentGeo->GetLayout();
                                                               VkBuffer vBuffers[] = {vBuf, vBuf, vBuf};
                                                               VkDeviceSize offsets[] = {
                                                                   layout.PositionsOffset, layout.NormalsOffset,
                                                                   layout.AuxOffset
                                                               };
                                                               vkCmdBindVertexBuffers(cmd, 0, 3, vBuffers, offsets);

                                                               // Bind Index Buffer
                                                               if (currentGeo->GetIndexCount() > 0)
                                                               {
                                                                   vkCmdBindIndexBuffer(
                                                                       cmd, currentGeo->GetIndexBuffer()->GetHandle(),
                                                                       0, VK_INDEX_TYPE_UINT32);
                                                               }

                                                               // Set Topology
                                                               // (If topology varies per mesh, set it here. If mostly triangles, optimize)
                                                               // vkCmdSetPrimitiveTopology(cmd, ...);
                                                           }

                                                           if (!currentGeo) continue;

                                                           // Push Constants (Per Object)
                                                           RHI::MeshPushConstants push{};
                                                           push.model = packet.Transform;
                                                           push.textureID = packet.TextureID;

                                                           vkCmdPushConstants(cmd, m_Pipeline.GetLayout(),
                                                                              VK_SHADER_STAGE_VERTEX_BIT |
                                                                              VK_SHADER_STAGE_FRAGMENT_BIT,
                                                                              0, sizeof(push), &push);

                                                           // Issue Draw
                                                           if (currentGeo->GetIndexCount() > 0)
                                                           {
                                                               vkCmdDrawIndexed(
                                                                   cmd, currentGeo->GetIndexCount(), 1, 0, 0, 0);
                                                           }
                                                           else
                                                           {
                                                               // Point Cloud
                                                               uint32_t vertCount = static_cast<uint32_t>(currentGeo->
                                                                   GetLayout().PositionsSize / sizeof(glm::vec3));
                                                               vkCmdDraw(cmd, vertCount, 1, 0, 0);
                                                           }
                                                       }
                                                   }
            );

            m_RenderGraph.AddPass<ImGuiPassData>("ImGuiPass",
                                                 [&](ImGuiPassData& data, Graph::RGBuilder& builder)
                                                 {
                                                     Graph::RGAttachmentInfo colorInfo{};
                                                     colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                                                     colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;

                                                     data.Backbuffer = builder.WriteColor(
                                                         backbufferHandle, colorInfo);
                                                 },
                                                 [](const ImGuiPassData&, const Graph::RGRegistry&,
                                                    VkCommandBuffer cmd)
                                                 {
                                                     Interface::GUI::Render(cmd);
                                                 }
            );

            m_RenderGraph.Compile(frameIndex);
            m_RenderGraph.Execute(m_Renderer.GetCommandBuffer());
            m_Renderer.EndFrame();
        }
        else
        {
            Interface::GUI::EndFrame();
        }
    }
}
