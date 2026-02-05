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
import :Interaction;
import :Presentation;
import :GlobalResources;

import Core;
import RHI;
import ECS;
import Interface;

using namespace Core::Hash;

namespace Graphics
{
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
        : m_Config(config)
          , m_DeviceOwner(std::move(device))
          , m_Device(m_DeviceOwner.get())
          , m_Swapchain(swapchain)
          , m_Renderer(renderer)
          // Sub-Systems (must match declaration order)
          , m_GlobalResources(m_DeviceOwner, descriptorPool, descriptorLayout, bindlessSystem, shaderRegistry,
                              pipelineLibrary, renderer.GetFramesInFlight())
          , m_Presentation(m_DeviceOwner, swapchain, renderer)
          , m_Interaction({.MaxFramesInFlight = renderer.GetFramesInFlight()}, m_DeviceOwner)
          , m_RenderGraph(m_DeviceOwner, frameArena, frameScope)
          , m_GeometryStorage(geometryStorage)
          , m_MaterialSystem(materialSystem)

    {
        // Register UI panel
        Interface::GUI::RegisterPanel("Render Target Viewer",
                                      [this]()
                                      {
                                          auto& debugView = m_Interaction.GetDebugViewStateMut();
                                          ImGui::Checkbox("Enable Debug View", &debugView.Enabled);

                                          if (!debugView.Enabled)
                                          {
                                              ImGui::TextDisabled(
                                                  "Debug view disabled. Enable to visualize render targets.");
                                              return;
                                          }

                                          ImGui::Checkbox("Show debug view in viewport", &debugView.ShowInViewport);
                                          ImGui::Separator();

                                          for (const auto& pass : m_LastDebugPasses)
                                          {
                                              if (!ImGui::TreeNode(pass.Name))
                                                  continue;

                                              for (const auto& att : pass.Attachments)
                                              {
                                                  const bool isSelected = (att.ResourceName == debugView.
                                                      SelectedResource);
                                                  char label[128];
                                                  snprintf(label, sizeof(label), "0x%08X%s", att.ResourceName.Value,
                                                           att.IsDepth ? " (Depth)" : "");

                                                  if (ImGui::Selectable(label, isSelected))
                                                  {
                                                      debugView.SelectedResource = att.ResourceName;
                                                  }
                                              }

                                              ImGui::TreePop();
                                          }

                                          ImGui::Separator();
                                          ImGui::DragFloat("Depth Near", &debugView.DepthNear, 0.01f, 1e-4f, 10.0f,
                                                           "%.4f", ImGuiSliderFlags_AlwaysClamp);
                                          ImGui::DragFloat("Depth Far", &debugView.DepthFar, 1.0f, 1.0f, 100000.0f,
                                                           "%.1f", ImGuiSliderFlags_AlwaysClamp);
                                      },
                                      true);

        m_RenderGraph.SetTransientAllocator(m_GlobalResources.GetTransientAllocator());
        m_GpuScene = nullptr;
    }

    RenderSystem::~RenderSystem()
    {
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
    }

    void RenderSystem::RequestPipelineSwap(std::unique_ptr<RenderPipeline> pipeline)
    {
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
            m_ActivePipeline->Initialize(*m_Device,
                                         m_GlobalResources.GetDescriptorPool(),
                                         m_GlobalResources.GetDescriptorLayout(),
                                         m_GlobalResources.GetShaderRegistry(),
                                         m_GlobalResources.GetPipelineLibrary());
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

    void RenderSystem::RequestPick(uint32_t x, uint32_t y)
    {
        m_Interaction.RequestPick(x, y, m_Presentation.GetFrameIndex(), m_Device->GetGlobalFrameNumber());
    }

    RenderSystem::PickResultGpu RenderSystem::GetLastPickResult() const
    {
        auto res = m_Interaction.GetLastPickResult();
        return {res.HasHit, res.EntityID};
    }

    std::optional<RenderSystem::PickResultGpu> RenderSystem::TryConsumePickResult()
    {
        if (auto res = m_Interaction.TryConsumePickResult())
        {
            return RenderSystem::PickResultGpu{res->HasHit, res->EntityID};
        }
        return std::nullopt;
    }

    void RenderSystem::OnUpdate(ECS::Scene& scene, const CameraComponent& camera,
                                Core::Assets::AssetManager& assetManager)
    {
        const uint64_t currentFrame = m_Device->GetGlobalFrameNumber();
        m_GeometryStorage.ProcessDeletions(currentFrame);
        GarbageCollectRetiredPipelines();

        // ---------------------------------------------------------
        // 1. Interaction & WSI
        // ---------------------------------------------------------
        m_Interaction.ProcessReadbacks(currentFrame);

        Interface::GUI::BeginFrame();
        Interface::GUI::DrawGUI();

        if (!m_Presentation.BeginFrame())
        {
            Interface::GUI::EndFrame();
            return;
        }

        const uint32_t frameIndex = m_Presentation.GetFrameIndex();
        const auto extent = m_Presentation.GetResolution();

        // ---------------------------------------------------------
        // 2. Global Resources (Camera, Allocators)
        // ---------------------------------------------------------
        m_GlobalResources.BeginFrame(frameIndex);
        m_GlobalResources.Update(camera, frameIndex);

        ApplyPendingPipelineSwap(extent.width, extent.height);

        // ---------------------------------------------------------
        // 3. Prepare Render Graph
        // ---------------------------------------------------------
        m_RenderGraph.Reset();
        const uint32_t imageIndex = m_Presentation.GetImageIndex();
        RenderBlackboard blackboard;

        const auto& pendingPick = m_Interaction.GetPendingPick();
        const auto& debugView = m_Interaction.GetDebugViewState();

        // Frame setup pass
        struct FrameSetupData
        {
            RGResourceHandle Backbuffer;
            RGResourceHandle Depth;
        };
        m_RenderGraph.AddPass<FrameSetupData>("FrameSetup",
                                              [&](FrameSetupData& data, RGBuilder& builder)
                                              {
                                                  data.Backbuffer = builder.ImportTexture(
                                                      "Backbuffer"_id,
                                                      m_Presentation.GetBackbuffer(),
                                                      m_Presentation.GetBackbufferView(),
                                                      m_Presentation.GetBackbufferFormat(),
                                                      extent,
                                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

                                                  auto& depthImg = m_Presentation.GetDepthBuffer();
                                                  data.Depth = builder.ImportTexture(
                                                      "SceneDepth"_id,
                                                      depthImg.GetHandle(),
                                                      depthImg.GetView(),
                                                      depthImg.GetFormat(),
                                                      extent,
                                                      VK_IMAGE_LAYOUT_UNDEFINED);

                                                  blackboard.Add("Backbuffer"_id, data.Backbuffer);
                                                  blackboard.Add("SceneDepth"_id, data.Depth);
                                              },
                                              [](const FrameSetupData&, const RGRegistry&, VkCommandBuffer)
                                              {
                                              });

        // GPUScene update pass
        struct SceneUpdateData
        {
            int _dummy = 0;
        };
        m_RenderGraph.AddPass<SceneUpdateData>("SceneUpdate",
                                               [&](SceneUpdateData&, RGBuilder& builder)
                                               {
                                                   if (!m_GpuScene) return;
                                                   builder.Write(
                                                       builder.ImportBuffer(
                                                           "GPUScene.Scene"_id, m_GpuScene->GetSceneBuffer()),
                                                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                       VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                                                   builder.Write(
                                                       builder.ImportBuffer(
                                                           "GPUScene.Bounds"_id, m_GpuScene->GetBoundsBuffer()),
                                                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                       VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                                               },
                                               [this](const SceneUpdateData&, const RGRegistry&, VkCommandBuffer cmd)
                                               {
                                                   if (m_GpuScene) m_GpuScene->Sync(cmd);
                                               });

        // ---------------------------------------------------------
        // 4. Execute Pipeline
        // ---------------------------------------------------------
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
            m_GlobalResources.GetCameraUBO(),
            m_GlobalResources.GetGlobalDescriptorSet(),
            m_GlobalResources.GetDynamicUBOOffset(frameIndex),
            m_GlobalResources.GetBindlessSystem(),
            // Pass interaction state
            {pendingPick.Pending, pendingPick.X, pendingPick.Y},
            {
                debugView.Enabled, debugView.ShowInViewport, debugView.SelectedResource, debugView.DepthNear,
                debugView.DepthFar
            },
            m_LastDebugImages,
            m_LastDebugPasses,
            camera.ViewMatrix,
            camera.ProjectionMatrix,
            m_Interaction.GetReadbackBuffer(frameIndex)
        };

        if (m_ActivePipeline)
            m_ActivePipeline->SetupFrame(ctx);

        m_RenderGraph.Compile(frameIndex);

        m_LastDebugPasses = m_RenderGraph.BuildDebugPassList();
        m_LastDebugImages = m_RenderGraph.BuildDebugImageList();

        if (m_ActivePipeline)
            m_ActivePipeline->PostCompile(frameIndex, m_LastDebugImages, m_LastDebugPasses);

        m_RenderGraph.Execute(m_Presentation.GetCommandBuffer());

        // ---------------------------------------------------------
        // 5. Submit & Present
        // ---------------------------------------------------------
        m_Presentation.EndFrame();
    }

    void RenderSystem::OnResize()
    {
        m_RenderGraph.Trim();
        m_Presentation.OnResize();
        auto extent = m_Presentation.GetResolution();
        if (m_ActivePipeline) m_ActivePipeline->OnResize(extent.width, extent.height);
    }
}
