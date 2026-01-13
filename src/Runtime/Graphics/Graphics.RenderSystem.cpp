module;
#include <cstring>
#include <vector>
#include <algorithm>
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include "RHI.Vulkan.hpp"
#include <imgui.h>

module Graphics:RenderSystem.Impl;
import :RenderSystem;
import :Camera;
import :Components;
import Core;
import RHI;
import ECS;
import Interface;

using namespace Core::Hash;

namespace Graphics
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
        Core::Log::Info("RenderSystem: Starting constructor body...");
        m_DepthImages.resize(renderer.GetFramesInFlight());
        Core::Log::Info("RenderSystem: DepthImages resized.");

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
        Core::Log::Info("RenderSystem: UBO created.");

        // 2. Allocate Descriptor Set (Set 0)
        m_GlobalDescriptorSet = descriptorPool.Allocate(descriptorLayout.GetHandle());
        Core::Log::Info("RenderSystem: Descriptor set allocated.");

        if (m_GlobalDescriptorSet != VK_NULL_HANDLE && m_GlobalUBO && m_GlobalUBO->GetHandle() != VK_NULL_HANDLE)
        {
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
            Core::Log::Info("RenderSystem: Descriptor set updated successfully.");
        }
        else
        {
            Core::Log::Error("RenderSystem: Failed to initialize Global UBO or Descriptor Set");
        }
        Core::Log::Info("RenderSystem: Constructor complete.");
    }

    RenderSystem::~RenderSystem()
    {
        //keep for persistent camera data mapping = nullptr on destruction?
    }

    struct ImGuiPassData
    {
        RGResourceHandle Backbuffer;
    };

    struct ForwardPassData
    {
        RGResourceHandle Color;
        RGResourceHandle Depth;
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

    void RenderSystem::OnUpdate(ECS::Scene& scene, const CameraComponent& camera,
                                Core::Assets::AssetManager& assetManager)
    {
        // Process deferred geometry deletions at the start of the frame
        // This ensures GPU resources are freed only after FramesInFlight have passed
        uint64_t currentFrame = m_Device->GetGlobalFrameNumber();
        m_GeometryStorage.ProcessDeletions(currentFrame);

        Interface::GUI::BeginFrame();

        Interface::GUI::DrawGUI();

        m_Renderer.BeginFrame();

        if (m_Renderer.IsFrameInProgress())
        {
            RHI::CameraBufferObject ubo{};
            ubo.View = camera.ViewMatrix;
            ubo.Proj = camera.ProjectionMatrix;

            size_t cameraDataSize = sizeof(RHI::CameraBufferObject);
            size_t alignedSize = PadUniformBufferSize(cameraDataSize, m_MinUboAlignment);
            uint32_t frameIndex = m_Renderer.GetCurrentFrameIndex();
            size_t offset = frameIndex * alignedSize;

            char* dataPtr = static_cast<char*>(m_GlobalUBO->Map());
            memcpy(dataPtr + offset, &ubo, cameraDataSize);

            m_RenderGraph.Reset();

            auto extent = m_Swapchain.GetExtent();
            uint32_t imageIndex = m_Renderer.GetImageIndex();

            RGResourceHandle backbufferHandle{};

            m_RenderGraph.AddPass<ForwardPassData>("ForwardPass",
                                                   [&](ForwardPassData& data, RGBuilder& builder)
                                                   {
                                                       VkImage swapImage = m_Renderer.GetSwapchainImage(imageIndex);
                                                       VkImageView swapView = m_Renderer.GetSwapchainImageView(
                                                           imageIndex);

                                                       // Use explicit format from swapchain
                                                       auto importedColor = builder.ImportTexture(
                                                           "Backbuffer", swapImage, swapView,
                                                           m_Swapchain.GetImageFormat(), extent,
                                                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

                                                       // Check and create Depth Buffer if needed
                                                       auto& depthImg = m_DepthImages[frameIndex];
                                                       if (!depthImg || depthImg->GetWidth() != extent.width ||
                                                           depthImg->GetHeight() != extent.height)
                                                       {
                                                           VkFormat depthFormat = RHI::VulkanImage::FindDepthFormat(
                                                               *m_Device);
                                                           depthImg = std::make_unique<RHI::VulkanImage>(
                                                               m_Device, extent.width, extent.height, 1,
                                                               depthFormat,
                                                               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                               VK_IMAGE_USAGE_SAMPLED_BIT,
                                                               VK_IMAGE_ASPECT_DEPTH_BIT
                                                           );
                                                       }

                                                       auto depth = builder.ImportTexture(
                                                           "DepthBuffer", depthImg->GetHandle(), depthImg->GetView(),
                                                           depthImg->GetFormat(), extent,
                                                           VK_IMAGE_LAYOUT_UNDEFINED);

                                                       RGAttachmentInfo colorInfo{};
                                                       colorInfo.ClearValue = {{{0.1f, 0.3f, 0.6f, 1.0f}}};
                                                       colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                                       colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;

                                                       RGAttachmentInfo depthInfo{};
                                                       depthInfo.ClearValue.depthStencil = {1.0f, 0};

                                                       data.Color = builder.WriteColor(importedColor, colorInfo);
                                                       data.Depth = builder.WriteDepth(depth, depthInfo);
                                                       backbufferHandle = data.Color;
                                                   },
                                                   [renderer = &m_Renderer,
                                                       pipeline = &m_Pipeline,
                                                       bindless = &m_BindlessSystem,
                                                       globalSet0 = m_GlobalDescriptorSet,
                                                       geoStorage = &m_GeometryStorage,
                                                       scenePtr = &scene,
                                                       assets = &assetManager,
                                                       extent,
                                                       offset](const ForwardPassData&, const RGRegistry&,
                                                               VkCommandBuffer cmd)
                                                   {
                                                       renderer->BindPipeline(*pipeline);
                                                       renderer->SetViewport(extent.width, extent.height);

                                                       // 1. Bind Set 0: Global Camera (Dynamic Offset)
                                                       uint32_t dynamicOffset = static_cast<uint32_t>(offset);
                                                       vkCmdBindDescriptorSets(
                                                           cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                           pipeline->GetLayout(),
                                                           0, 1, &globalSet0,
                                                           1, &dynamicOffset
                                                       );

                                                       // 2. Bind Set 1: Bindless Textures (Static)
                                                       VkDescriptorSet globalTextures = bindless->GetGlobalSet();
                                                       vkCmdBindDescriptorSets(
                                                           cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                           pipeline->GetLayout(),
                                                           1, 1, &globalTextures,
                                                           0, nullptr
                                                       );

                                                       auto view = scenePtr->GetRegistry().view<
                                                           ECS::Components::Transform::Component,
                                                           ECS::MeshRenderer::Component>();
                                                       std::vector<RenderPacket> packets;
                                                       packets.reserve(view.size_hint());
                                                       for (auto [entity, transform, renderable] : view.each())
                                                       {
                                                           if (!renderable.Geometry.IsValid() || !renderable.Material.
                                                               IsValid())
                                                               continue;

                                                           auto matResult = assets->GetRaw<Material>(renderable.Material);

                                                           if (matResult.has_value())
                                                           {
                                                               Material* mat = *matResult;
                                                               packets.push_back({
                                                                   renderable.Geometry,
                                                                   mat->GetTextureIndex(),
                                                                   GetMatrix(transform)
                                                               });
                                                           }
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
                                                               // Hot path: use GetUnchecked for performance
                                                               currentGeo = geoStorage->GetUnchecked(packet.GeoHandle);
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
                                                               VkPrimitiveTopology vkTopo =
                                                                   VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                                                               switch (currentGeo->GetTopology())
                                                               {
                                                               case PrimitiveTopology::Points: vkTopo =
                                                                       VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                                                                   break;
                                                               case PrimitiveTopology::Lines: vkTopo =
                                                                       VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                                                                   break;
                                                               case PrimitiveTopology::Triangles:
                                                               default: vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                                                                   break;
                                                               }
                                                               vkCmdSetPrimitiveTopology(cmd, vkTopo);
                                                           }

                                                           if (!currentGeo) continue;

                                                           // Push Constants (Per Object)
                                                           RHI::MeshPushConstants push{};
                                                           push.Model = packet.Transform;
                                                           push.TextureID = packet.TextureID;

                                                           vkCmdPushConstants(cmd, pipeline->GetLayout(),
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
                                                 [&](ImGuiPassData& data, RGBuilder& builder)
                                                 {
                                                     RGAttachmentInfo colorInfo{};
                                                     colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                                                     colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;

                                                     data.Backbuffer = builder.WriteColor(
                                                         backbufferHandle, colorInfo);
                                                 },
                                                 [](const ImGuiPassData&, const RGRegistry&,
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
