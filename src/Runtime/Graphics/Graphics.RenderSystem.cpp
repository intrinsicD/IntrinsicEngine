module;

#include <cstring>
#include <vector>
#include <algorithm>
#include <memory>
#include <string>
#include <cstdio>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include "RHI.Vulkan.hpp"
#include <imgui.h>

module Graphics:RenderSystem.Impl;

import :RenderSystem;
import :Camera;
import :Components;

import :RenderPipeline;
import :Passes.Picking;
import :Passes.Forward;
import :Passes.DebugView;
import :Passes.ImGui;
import :ShaderRegistry;
import :PipelineLibrary;

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

    RenderSystem::RenderSystem(const RenderSystemConfig& config,
                               std::shared_ptr<RHI::VulkanDevice> device,
                               RHI::VulkanSwapchain& swapchain,
                               RHI::SimpleRenderer& renderer,
                               RHI::BindlessDescriptorSystem& bindlessSystem,
                               RHI::DescriptorAllocator& descriptorPool,
                               RHI::DescriptorLayout& descriptorLayout,
                               PipelineLibrary& pipelineLibrary,
                               const ShaderRegistry& shaderRegistry,
                               Core::Memory::LinearArena& frameArena,
                               Core::Memory::ScopeStack& frameScope,
                               GeometryPool& geometryStorage,
                               MaterialSystem& materialSystem)
        : m_Config(config),
          m_ShaderRegistry(&shaderRegistry),
          m_PipelineLibrary(&pipelineLibrary),
          m_DeviceOwner(std::move(device)),
          m_Device(m_DeviceOwner.get()),
          m_Swapchain(swapchain),
          m_Renderer(renderer),
          m_BindlessSystem(bindlessSystem),
          m_RenderGraph(m_DeviceOwner, frameArena, frameScope),
          m_GeometryStorage(geometryStorage),
          m_MaterialSystem(materialSystem),
          m_DescriptorPool(descriptorPool)
    {
        m_DepthImages.resize(renderer.GetFramesInFlight());

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(m_Device->GetPhysicalDevice(), &props);
        m_MinUboAlignment = props.limits.minUniformBufferOffsetAlignment;

        const size_t cameraDataSize = sizeof(RHI::CameraBufferObject);
        const size_t alignedSize = PadUniformBufferSize(cameraDataSize, m_MinUboAlignment);

        m_GlobalUBO = std::make_unique<RHI::VulkanBuffer>(
            *m_Device,
            alignedSize * renderer.GetFramesInFlight(),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            static_cast<VmaMemoryUsage>(VMA_MEMORY_USAGE_CPU_TO_GPU));

        m_GlobalDescriptorSet = descriptorPool.Allocate(descriptorLayout.GetHandle());

        if (m_GlobalDescriptorSet != VK_NULL_HANDLE && m_GlobalUBO && m_GlobalUBO->GetHandle() != VK_NULL_HANDLE)
        {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = m_GlobalUBO->GetHandle();
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(RHI::CameraBufferObject);

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = m_GlobalDescriptorSet;
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 1, &descriptorWrite, 0, nullptr);
        }
        else
        {
            Core::Log::Error("RenderSystem: Failed to initialize Global UBO or Descriptor Set");
        }

        // Picking: one 4-byte host-visible readback buffer per frame-in-flight.
        auto NumFramesInFlight = renderer.GetFramesInFlight();
        m_PickReadbackBuffers.resize(NumFramesInFlight);
        m_FrameHasPendingReadback.resize(NumFramesInFlight, false);
        for (auto& buf : m_PickReadbackBuffers)
        {
            buf = std::make_unique<RHI::VulkanBuffer>(
                *m_Device,
                sizeof(uint32_t),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                static_cast<VmaMemoryUsage>(VMA_MEMORY_USAGE_GPU_TO_CPU));

            if (void* ptr = buf->Map())
            {
                std::memset(ptr, 0, sizeof(uint32_t));
                buf->Unmap();
            }
        }

        // Create features
        m_PickingPass = std::make_unique<Passes::PickingPass>();
        m_PickingPass->Initialize(*m_Device, descriptorPool, descriptorLayout);
        m_PickingPass->SetPipeline(&m_PipelineLibrary->GetOrDie(kPipeline_Picking));
        m_PickingPass->SetReadbackBuffers(m_PickReadbackBuffers);

        m_ForwardPass = std::make_unique<Passes::ForwardPass>();
        m_ForwardPass->Initialize(*m_Device, descriptorPool, descriptorLayout);
        m_ForwardPass->SetPipeline(&m_PipelineLibrary->GetOrDie(kPipeline_Forward));

        m_DebugViewPass = std::make_unique<Passes::DebugViewPass>();
        m_DebugViewPass->Initialize(*m_Device, descriptorPool, descriptorLayout);
        m_DebugViewPass->SetShaderRegistry(shaderRegistry);

        m_ImGuiPass = std::make_unique<Passes::ImGuiPass>();
        m_ImGuiPass->Initialize(*m_Device, descriptorPool, descriptorLayout);

        // Register UI panel (reuses cached lists maintained in RenderSystem)
        Interface::GUI::RegisterPanel("Render Target Viewer",
                                      [this]()
                                      {
                                          ImGui::Checkbox("Enable Debug View", &m_DebugView.Enabled);

                                          if (!m_DebugView.Enabled)
                                          {
                                              ImGui::TextDisabled(
                                                  "Debug view disabled. Enable to visualize render targets.");
                                              return;
                                          }

                                          ImGui::Checkbox("Show debug view in viewport", &m_DebugView.ShowInViewport);
                                          ImGui::Separator();

                                          for (const auto& pass : m_LastDebugPasses)
                                          {
                                              if (!ImGui::TreeNode(pass.Name))
                                                  continue;

                                              for (const auto& att : pass.Attachments)
                                              {
                                                  const bool isSelected = (att.ResourceName == m_DebugView.
                                                      SelectedResource);
                                                  char label[128];
                                                  snprintf(label, sizeof(label), "0x%08X%s", att.ResourceName.Value,
                                                           att.IsDepth ? " (Depth)" : "");

                                                  if (ImGui::Selectable(label, isSelected))
                                                  {
                                                      m_DebugView.SelectedResource = att.ResourceName;
                                                      m_DebugView.SelectedResourceId = att.Resource;
                                                  }
                                              }

                                              ImGui::TreePop();
                                          }

                                          ImGui::Separator();
                                          ImGui::DragFloat("Depth Near", &m_DebugView.DepthNear, 0.01f, 1e-4f, 10.0f,
                                                           "%.4f", ImGuiSliderFlags_AlwaysClamp);
                                          ImGui::DragFloat("Depth Far", &m_DebugView.DepthFar, 1.0f, 1.0f, 100000.0f,
                                                           "%.1f", ImGuiSliderFlags_AlwaysClamp);

                                          if (m_DebugViewPass)
                                          {
                                              // RenderSystem tracks current frame index via m_Renderer.
                                              // We need the texture ID specific to the current frame to avoid race conditions.
                                              uint32_t currentFrame = m_Renderer.GetCurrentFrameIndex();
                                              void* texId = m_DebugViewPass->GetImGuiTextureId(currentFrame);

                                              if (texId)
                                              {
                                                  ImGui::TextUnformatted("Preview (resolved RGBA8)");
                                                  ImVec2 avail = ImGui::GetContentRegionAvail();
                                                  float w = avail.x;
                                                  float h = (w > 0.0f) ? w * 9.0f / 16.0f : 0.0f;
                                                  ImGui::Image(texId, ImVec2(w, h));
                                              }
                                              else
                                              {
                                                  ImGui::TextUnformatted("No debug image yet.");
                                              }
                                          }
                                      },
                                      true);

        // RenderGraph transient GPU memory allocator (pages persist, bump resets each frame).
        m_TransientAllocator = std::make_unique<RHI::TransientAllocator>(*m_Device);
        m_RenderGraph.SetTransientAllocator(*m_TransientAllocator);
    }

    RenderSystem::~RenderSystem()
    {
        // Ensure allocator is destroyed before VulkanDevice and after GPU idle.
        // Engine already waits idle before tearing down RenderSystem, but this is a cheap safety net.
        if (m_Device) vkDeviceWaitIdle(m_Device->GetLogicalDevice());
        m_TransientAllocator.reset();

        if (m_DebugViewPass)
        {
            m_DebugViewPass->Shutdown();
            m_DebugViewPass.reset();
        }
    }

    RenderSystem::PickResultGpu RenderSystem::GetLastPickResult() const
    {
        return m_LastPickResult;
    }

    std::optional<RenderSystem::PickResultGpu> RenderSystem::TryConsumePickResult()
    {
        if (!m_HasPendingConsumedResult)
            return std::nullopt;

        m_HasPendingConsumedResult = false;
        return m_PendingConsumedResult;
    }

    void RenderSystem::RequestPick(uint32_t x, uint32_t y)
    {
        m_PendingPick = {true, x, y, m_Renderer.GetCurrentFrameIndex()};
    }

    void RenderSystem::OnUpdate(ECS::Scene& scene, const CameraComponent& camera,
                                Core::Assets::AssetManager& assetManager)
    {
        const uint64_t currentFrame = m_Device->GetGlobalFrameNumber();
        m_GeometryStorage.ProcessDeletions(currentFrame);

        Interface::GUI::BeginFrame();
        Interface::GUI::DrawGUI();

        m_Renderer.BeginFrame();

        if (!m_Renderer.IsFrameInProgress())
        {
            Interface::GUI::EndFrame();
            return;
        }

        const uint32_t frameIndex = m_Renderer.GetCurrentFrameIndex();

        if (m_FrameHasPendingReadback[frameIndex])
        {
            // Map and read
            void* mapped = m_PickReadbackBuffers[frameIndex]->Map();
            if (mapped)
            {
                uint32_t entityID = *static_cast<uint32_t*>(mapped);
                m_PickReadbackBuffers[frameIndex]->Unmap();

                // Publish result
                m_PendingConsumedResult = {entityID != 0, entityID};
                m_HasPendingConsumedResult = true;
                m_LastPickResult = m_PendingConsumedResult;

                // Log for debug
                if (entityID != 0)
                    Core::Log::Info("GPU Pick Hit: Entity ID {}", entityID);
            }
            m_FrameHasPendingReadback[frameIndex] = false;
        }

        // If we are about to record a pick command this frame, mark it for readback next time
        if (m_PendingPick.Pending)
        {
            m_FrameHasPendingReadback[frameIndex] = true;
        }

        RHI::CameraBufferObject ubo{};
        ubo.View = camera.ViewMatrix;
        ubo.Proj = camera.ProjectionMatrix;

        const size_t cameraDataSize = sizeof(RHI::CameraBufferObject);
        const size_t alignedSize = PadUniformBufferSize(cameraDataSize, m_MinUboAlignment);
        const size_t dynamicOffset = frameIndex * alignedSize;

        char* dataPtr = static_cast<char*>(m_GlobalUBO->Map());
        memcpy(dataPtr + dynamicOffset, &ubo, cameraDataSize);

        m_RenderGraph.Reset();

        const auto extent = m_Swapchain.GetExtent();
        const uint32_t imageIndex = m_Renderer.GetImageIndex();

        RenderBlackboard blackboard;

        // Frame setup pass: import swapchain & depth and seed blackboard.
        struct FrameSetupData
        {
            RGResourceHandle Backbuffer;
            RGResourceHandle Depth;
        };

        m_RenderGraph.AddPass<FrameSetupData>("FrameSetup",
                                              [&](FrameSetupData& data, RGBuilder& builder)
                                              {
                                                  VkImage swapImage = m_Renderer.GetSwapchainImage(imageIndex);
                                                  VkImageView swapView = m_Renderer.GetSwapchainImageView(imageIndex);

                                                  data.Backbuffer = builder.ImportTexture(
                                                      "Backbuffer"_id,
                                                      swapImage,
                                                      swapView,
                                                      m_Swapchain.GetImageFormat(),
                                                      extent,
                                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

                                                  // Ensure depth image exists and import
                                                  auto& depthImg = m_DepthImages[frameIndex];
                                                  if (!depthImg || depthImg->GetWidth() != extent.width || depthImg->
                                                      GetHeight() != extent.height)
                                                  {
                                                      VkFormat depthFormat = RHI::VulkanImage::FindDepthFormat(
                                                          *m_Device);
                                                      depthImg = std::make_unique<RHI::VulkanImage>(
                                                          *m_Device, extent.width, extent.height, 1,
                                                          depthFormat,
                                                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                          VK_IMAGE_USAGE_SAMPLED_BIT,
                                                          VK_IMAGE_ASPECT_DEPTH_BIT);
                                                  }

                                                  data.Depth = builder.ImportTexture(
                                                      "SceneDepth"_id,
                                                      depthImg->GetHandle(),
                                                      depthImg->GetView(),
                                                      depthImg->GetFormat(),
                                                      extent,
                                                      VK_IMAGE_LAYOUT_UNDEFINED);

                                                  blackboard.Add("Backbuffer"_id, data.Backbuffer);
                                                  blackboard.Add("SceneDepth"_id, data.Depth);
                                              },
                                              [](const FrameSetupData&, const RGRegistry&, VkCommandBuffer)
                                              {
                                              });

        RenderPassContext ctx{
            m_RenderGraph,
            blackboard,
            scene,
            assetManager,
            m_GeometryStorage,
            m_MaterialSystem,
            frameIndex,
            extent,
            imageIndex,
            m_Swapchain.GetImageFormat(),
            m_Renderer,
            m_GlobalUBO.get(),
            m_GlobalDescriptorSet,
            dynamicOffset,
            m_BindlessSystem,
            {m_PendingPick.Pending, m_PendingPick.X, m_PendingPick.Y},
            {
                m_DebugView.Enabled, m_DebugView.ShowInViewport, m_DebugView.SelectedResource, m_DebugView.DepthNear,
                m_DebugView.DepthFar
            },
            m_LastDebugImages,
            m_LastDebugPasses
        };

        if (m_PickingPass) m_PickingPass->AddPasses(ctx);
        if (m_ForwardPass) m_ForwardPass->AddPasses(ctx);

        if (m_DebugViewPass && m_DebugView.Enabled && m_DebugView.ShowInViewport)
            m_DebugViewPass->AddPasses(ctx);

        if (m_ImGuiPass) m_ImGuiPass->AddPasses(ctx);

        if (m_DebugViewPass && m_DebugView.Enabled && !m_DebugView.ShowInViewport)
            m_DebugViewPass->AddPasses(ctx);

        m_RenderGraph.Compile(frameIndex);

        m_LastDebugPasses = m_RenderGraph.BuildDebugPassList();
        m_LastDebugImages = m_RenderGraph.BuildDebugImageList();

        if (m_DebugViewPass)
            m_DebugViewPass->PostCompile(frameIndex, m_LastDebugImages);

        m_RenderGraph.Execute(m_Renderer.GetCommandBuffer());
        m_Renderer.EndFrame();

        m_PendingPick.Pending = false;
    }

    void RenderSystem::OnResize()
    {
        m_RenderGraph.Trim();

        for (auto& img : m_DepthImages)
        {
            img.reset();
        }

        auto extent = m_Swapchain.GetExtent();
        if (m_PickingPass) m_PickingPass->OnResize(extent.width, extent.height);
        if (m_DebugViewPass) m_DebugViewPass->OnResize(extent.width, extent.height);
    }
}
