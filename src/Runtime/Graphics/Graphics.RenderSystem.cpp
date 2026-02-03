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
import :ShaderRegistry;
import :PipelineLibrary;
import :GPUScene;

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
          m_DescriptorPool(descriptorPool),
          m_DescriptorLayout(descriptorLayout),
          m_RenderGraph(m_DeviceOwner, frameArena, frameScope),
          m_GeometryStorage(geometryStorage),
          m_MaterialSystem(materialSystem)
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
        m_PickReadbackRequestFrame.resize(NumFramesInFlight, 0);
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

                                          // Pipeline-owned features may provide their own ImGui preview;
                                          // RenderSystem now only shows whatever was registered via Interface::GUI.
                                      },
                                      true);

        // RenderGraph transient GPU memory allocator (pages persist, bump resets each frame).
        m_TransientAllocator = std::make_unique<RHI::TransientAllocator>(*m_Device);
        m_RenderGraph.SetTransientAllocator(*m_TransientAllocator);

        // Retained-mode GPUScene (persistent instance + bounds SSBOs).
        // Ownership lives above RenderSystem (Runtime/Engine): game/systems queue updates, RenderSystem only consumes.
        m_GpuScene = nullptr;
    }

    RenderSystem::~RenderSystem()
    {
        // Ensure allocator is destroyed before VulkanDevice and after GPU idle.
        if (m_Device) vkDeviceWaitIdle(m_Device->GetLogicalDevice());

        if (m_ActivePipeline) m_ActivePipeline->Shutdown();
        if (m_PendingPipeline) m_PendingPipeline->Shutdown();
        for (auto& p : m_RetiredPipelines)
        {
            if (p.Pipeline) p.Pipeline->Shutdown();
        }

        m_RetiredPipelines.clear();
        m_ActivePipeline.reset();
        m_PendingPipeline.reset();

        m_TransientAllocator.reset();
    }

    void RenderSystem::RequestPipelineSwap(std::unique_ptr<RenderPipeline> pipeline)
    {
        // Overwrite any previously requested swap.
        if (m_PendingPipeline)
            m_PendingPipeline->Shutdown();

        m_PendingPipeline = std::move(pipeline);
    }

    void RenderSystem::ApplyPendingPipelineSwap(uint32_t width, uint32_t height)
    {
        if (!m_PendingPipeline)
            return;

        const uint64_t retireFrame = m_Device ? m_Device->GetGlobalFrameNumber() : 0;

        if (m_ActivePipeline)
        {
            m_RetiredPipelines.push_back({std::move(m_ActivePipeline), retireFrame});
        }

        m_ActivePipeline = std::move(m_PendingPipeline);

        if (m_ActivePipeline)
        {
            m_ActivePipeline->Initialize(*m_Device, m_DescriptorPool, m_DescriptorLayout,
                                         *m_ShaderRegistry, *m_PipelineLibrary);
            m_ActivePipeline->OnResize(width, height);
        }
    }

    void RenderSystem::GarbageCollectRetiredPipelines()
    {
        if (!m_Device)
            return;

        const uint64_t currentGlobalFrame = m_Device->GetGlobalFrameNumber();
        const uint32_t framesInFlight = m_Renderer.GetFramesInFlight();

        if (m_RetiredPipelines.empty())
            return;

        auto it = std::remove_if(m_RetiredPipelines.begin(), m_RetiredPipelines.end(),
                                 [&](RetiredPipeline& p)
                                 {
                                     if (!p.Pipeline)
                                         return true;
                                     if (currentGlobalFrame < p.RetireFrame + framesInFlight)
                                         return false;
                                     p.Pipeline->Shutdown();
                                     return true;
                                 });

        m_RetiredPipelines.erase(it, m_RetiredPipelines.end());
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
        m_PendingPick = {
            true, x, y,
            m_Renderer.GetCurrentFrameIndex(),
            m_Device->GetGlobalFrameNumber()
        };
    }

    void RenderSystem::OnUpdate(ECS::Scene& scene, const CameraComponent& camera,
                                Core::Assets::AssetManager& assetManager)
    {
        const uint64_t currentFrame = m_Device->GetGlobalFrameNumber();
        m_GeometryStorage.ProcessDeletions(currentFrame);

        // Safe-point GC for swapped-out pipelines.
        GarbageCollectRetiredPipelines();

        Interface::GUI::BeginFrame();
        Interface::GUI::DrawGUI();

        m_Renderer.BeginFrame();

        if (!m_Renderer.IsFrameInProgress())
        {
            Interface::GUI::EndFrame();
            return;
        }

        const uint32_t frameIndex = m_Renderer.GetCurrentFrameIndex();
        const uint64_t currentGlobalFrame = m_Device->GetGlobalFrameNumber();
        const uint32_t framesInFlight = m_Renderer.GetFramesInFlight();

        const auto extent = m_Swapchain.GetExtent();

        // Apply swap only once we know a frame is actually in-progress.
        ApplyPendingPipelineSwap(extent.width, extent.height);

        // Check if a prior pick recorded into this slot is now safe to read.
        if (m_PickReadbackRequestFrame[frameIndex] != 0)
        {
            const uint64_t requestFrame = m_PickReadbackRequestFrame[frameIndex];
            if (currentGlobalFrame >= requestFrame + framesInFlight)
            {
                m_PickReadbackBuffers[frameIndex]->Invalidate(0, sizeof(uint32_t));

                void* mapped = m_PickReadbackBuffers[frameIndex]->Map();
                if (mapped)
                {
                    uint32_t entityID = *static_cast<uint32_t*>(mapped);
                    m_PickReadbackBuffers[frameIndex]->Unmap();

                    m_PendingConsumedResult = {entityID != 0, entityID};
                    m_HasPendingConsumedResult = true;
                    m_LastPickResult = m_PendingConsumedResult;

                    if (entityID != 0)
                        Core::Log::Info("GPU Pick Hit: Entity ID {}", entityID);
                }
                m_PickReadbackRequestFrame[frameIndex] = 0;
            }
        }

        if (m_PendingPick.Pending)
        {
            m_PickReadbackRequestFrame[frameIndex] = currentGlobalFrame;
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

        // GPUScene update pass: scatter queued deltas into persistent SSBOs.
        struct SceneUpdateData { int _dummy = 0; };
        m_RenderGraph.AddPass<SceneUpdateData>("SceneUpdate",
                                              [&](SceneUpdateData&, RGBuilder& builder)
                                              {
                                                  if (!m_GpuScene)
                                                      return;

                                                  auto sceneBuf = builder.ImportBuffer("GPUScene.Scene"_id, m_GpuScene->GetSceneBuffer());
                                                  auto boundsBuf = builder.ImportBuffer("GPUScene.Bounds"_id, m_GpuScene->GetBoundsBuffer());

                                                  builder.Write(sceneBuf, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                                                  builder.Write(boundsBuf, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                                              },
                                              [this](const SceneUpdateData&, const RGRegistry&, VkCommandBuffer cmd)
                                              {
                                                  if (m_GpuScene)
                                                      m_GpuScene->Sync(cmd);
                                              });

        RenderPassContext ctx{
            m_RenderGraph,
            blackboard,
            scene,
            assetManager,
            m_GeometryStorage,
            m_MaterialSystem,
            m_GpuScene,
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
            m_LastDebugPasses,
            camera.ViewMatrix,
            camera.ProjectionMatrix,
            (frameIndex < m_PickReadbackBuffers.size()) ? m_PickReadbackBuffers[frameIndex].get() : nullptr
        };

        if (m_ActivePipeline)
            m_ActivePipeline->SetupFrame(ctx);

        m_RenderGraph.Compile(frameIndex);

        m_LastDebugPasses = m_RenderGraph.BuildDebugPassList();
        m_LastDebugImages = m_RenderGraph.BuildDebugImageList();

        if (m_ActivePipeline)
            m_ActivePipeline->PostCompile(frameIndex, m_LastDebugImages, m_LastDebugPasses);

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
        if (m_ActivePipeline)
            m_ActivePipeline->OnResize(extent.width, extent.height);
    }
}
