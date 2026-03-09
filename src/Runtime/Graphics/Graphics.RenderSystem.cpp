module;

#include <cstring>
#include <vector>
#include <algorithm>
#include <memory>
#include <string>
#include <format>
#include <cstdio>
#include <array>
#include <span>
#include <unordered_map>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <imgui.h>

#include "RHI.Vulkan.hpp"

module Graphics:RenderSystem.Impl;

import :RenderSystem;
import :Camera;
import :Components;
import :RenderPipeline;
import :RenderGraph;
import :Geometry;
import :MaterialSystem;
import :ShaderRegistry;
import :PipelineLibrary;
import :GPUScene;
import :Interaction;
import :Presentation;
import :GlobalResources;
import :Passes.SelectionOutlineSettings;
import :Pipelines;

import Core.Hash;
import Core.Memory;
import Core.Assets;
import Core.Logging;
import Core.Telemetry;
import RHI;
import ECS;
import Interface;

using namespace Core::Hash;

namespace Graphics
{
    namespace
    {
        [[nodiscard]] bool HasSelectionWork(ECS::Scene& scene)
        {
            auto& registry = scene.GetRegistry();
            return !registry.view<ECS::Components::Selection::SelectedTag>().empty() ||
                   !registry.view<ECS::Components::Selection::HoveredTag>().empty();
        }

        void LogRenderAudit(const std::vector<RenderGraphDebugPass>& passes,
                            const std::vector<RenderGraphDebugImage>& images)
        {
            Core::Log::Info("RenderAudit: {} passes, {} images", passes.size(), images.size());
            for (const auto& pass : passes)
            {
                Core::Log::Info("RenderAudit.Pass[{}] {}", pass.PassIndex, pass.Name);
                for (const auto& att : pass.Attachments)
                {
                    Core::Log::Info("  attachment name=0x{:08X} resource={} format={} loadOp={} storeOp={} depth={} imported={}",
                                    att.ResourceName.Value,
                                    att.Resource,
                                    static_cast<int>(att.Format),
                                    static_cast<int>(att.LoadOp),
                                    static_cast<int>(att.StoreOp),
                                    att.IsDepth,
                                    att.IsImported);
                }
            }

            for (const auto& image : images)
            {
                Core::Log::Info(
                    "RenderAudit.Image name=0x{:08X} resource={} extent={}x{} format={} usage=0x{:08X} layout={} imported={} firstWrite={} lastWrite={} firstRead={} lastRead={} startPass={} endPass={}",
                    image.Name.Value,
                    image.Resource,
                    image.Extent.width,
                    image.Extent.height,
                    static_cast<int>(image.Format),
                    static_cast<uint32_t>(image.Usage),
                    static_cast<int>(image.CurrentLayout),
                    image.IsImported,
                    image.FirstWritePass,
                    image.LastWritePass,
                    image.FirstReadPass,
                    image.LastReadPass,
                    image.StartPass,
                    image.EndPass);
            }
        }

        [[nodiscard]] const char* ResolveRenderResourceDebugName(Core::Hash::StringID id)
        {
            using enum RenderResource;
            for (RenderResource r : {SceneDepth, EntityId, PrimitiveId,
                 SceneNormal, Albedo, Material0, SceneColorHDR,
                 SceneColorLDR, SelectionMask, SelectionOutline})
            {
                if (GetRenderResourceName(r) != id)
                    continue;
                switch (r)
                {
                case SceneDepth:       return "SceneDepth";
                case EntityId:         return "EntityId";
                case PrimitiveId:      return "PrimitiveId";
                case SceneNormal:      return "SceneNormal";
                case Albedo:           return "Albedo";
                case Material0:        return "Material0";
                case SceneColorHDR:    return "SceneColorHDR";
                case SceneColorLDR:    return "SceneColorLDR";
                case SelectionMask:    return "SelectionMask";
                case SelectionOutline: return "SelectionOutline";
                }
            }
            if (id == "Backbuffer"_id) return "Backbuffer";
            if (id == "PostLdrTemp"_id) return "PostLdrTemp";
            if (id == "SMAAEdges"_id) return "SMAAEdges";
            if (id == "SMAAWeights"_id) return "SMAAWeights";
            return nullptr;
        }

        [[nodiscard]] const char* FormatVkFormatName(VkFormat fmt)
        {
            switch (fmt)
            {
            case VK_FORMAT_R32_UINT:              return "R32_UINT";
            case VK_FORMAT_R8_UNORM:              return "R8_UNORM";
            case VK_FORMAT_R8G8_UNORM:            return "RG8_UNORM";
            case VK_FORMAT_R8G8B8A8_UNORM:        return "RGBA8";
            case VK_FORMAT_R8G8B8A8_SRGB:         return "RGBA8_SRGB";
            case VK_FORMAT_B8G8R8A8_UNORM:        return "BGRA8";
            case VK_FORMAT_B8G8R8A8_SRGB:         return "BGRA8_SRGB";
            case VK_FORMAT_R16G16B16A16_SFLOAT:   return "RGBA16F";
            case VK_FORMAT_D32_SFLOAT:            return "D32F";
            case VK_FORMAT_D24_UNORM_S8_UINT:     return "D24S8";
            case VK_FORMAT_D32_SFLOAT_S8_UINT:    return "D32FS8";
            default:                              return "Unknown";
            }
        }

        [[nodiscard]] bool CompiledPassNameContains(std::span<const RenderGraphDebugPass> passes, std::string_view needle)
        {
            return std::any_of(passes.begin(), passes.end(),
                               [&](const RenderGraphDebugPass& pass)
                               {
                                   return std::string_view(pass.Name).find(needle) != std::string_view::npos;
                               });
        }
    }

    // -----------------------------------------------------------------
    // ValidateCompiledGraph — exported structured validation
    // -----------------------------------------------------------------
    RenderGraphValidationResult ValidateCompiledGraph(
        const FrameRecipe& recipe,
        std::span<const RenderGraphDebugPass> passes,
        std::span<const RenderGraphDebugImage> images,
        std::span<const ImportedResourceWritePolicy> writePolicies)
    {
        RenderGraphValidationResult result{};

        auto addDiag = [&](RenderGraphValidationSeverity sev, std::string msg)
        {
            result.Diagnostics.push_back({sev, std::move(msg)});
        };

        // --- 1. Required-resource existence and producer checks ---

        auto findImage = [&](RenderResource resource) -> const RenderGraphDebugImage*
        {
            const Core::Hash::StringID name = GetRenderResourceName(resource);
            for (const auto& image : images)
            {
                if (image.Name == name)
                    return &image;
            }
            return nullptr;
        };

        for (RenderResource resource : {
                 RenderResource::SceneDepth,
                 RenderResource::EntityId,
                 RenderResource::PrimitiveId,
                 RenderResource::SceneNormal,
                 RenderResource::Albedo,
                 RenderResource::Material0,
                 RenderResource::SceneColorHDR,
                 RenderResource::SceneColorLDR,
                 RenderResource::SelectionMask,
                 RenderResource::SelectionOutline,
             })
        {
            if (!recipe.Requires(resource))
                continue;

            const auto def = GetRenderResourceDefinition(resource);
            const auto* image = findImage(resource);
            if (!image)
            {
                // A required resource missing from the compiled graph is an error.
                addDiag(RenderGraphValidationSeverity::Error,
                        std::format("required resource 0x{:08X} is missing from the compiled image list.",
                                    def.Name.Value));
                continue;
            }

            if (def.Lifetime != RenderResourceLifetime::Imported && image->FirstWritePass == ~0u)
            {
                // A required transient resource without a producer is an error.
                addDiag(RenderGraphValidationSeverity::Error,
                        std::format("required transient resource 0x{:08X} has no producer in this frame.",
                                    def.Name.Value));
            }
        }

        // --- 2. Per-pass attachment analysis ---

        struct WriteSummary
        {
            Core::Hash::StringID Name{};
            bool IsImported = false;
            bool IsDepth = false;
            uint32_t InitializeCount = 0;
            uint32_t AttachmentWrites = 0;
        };
        std::unordered_map<ResourceID, WriteSummary> writeSummaries;

        // Build the default write policies if none were provided.
        const auto defaultPolicies = writePolicies.empty()
            ? GetDefaultImportedWritePolicies()
            : std::vector<ImportedResourceWritePolicy>{};
        const auto effectivePolicies = writePolicies.empty()
            ? std::span<const ImportedResourceWritePolicy>(defaultPolicies)
            : writePolicies;

        for (const auto& pass : passes)
        {
            for (const auto& att : pass.Attachments)
            {
                auto& summary = writeSummaries[att.Resource];
                summary.Name = att.ResourceName;
                summary.IsImported = att.IsImported;
                summary.IsDepth = att.IsDepth;
                ++summary.AttachmentWrites;
                if (att.LoadOp != VK_ATTACHMENT_LOAD_OP_LOAD)
                    ++summary.InitializeCount;

                // Check imported-resource write policies.
                if (att.IsImported)
                {
                    for (const auto& policy : effectivePolicies)
                    {
                        if (att.ResourceName == policy.ResourceName &&
                            !policy.AuthorizedWriter.empty() &&
                            std::string_view(pass.Name) != policy.AuthorizedWriter)
                        {
                            addDiag(RenderGraphValidationSeverity::Error,
                                    std::format("imported resource 0x{:08X} is written by '{}' but only '{}' is authorized.",
                                                att.ResourceName.Value,
                                                pass.Name,
                                                policy.AuthorizedWriter));
                        }
                    }
                }
            }
        }

        // --- 3. Multiple-initialization check for transient resources ---

        for (const auto& [resource, summary] : writeSummaries)
        {
            if (summary.IsImported)
                continue;

            if (summary.InitializeCount > 1)
            {
                addDiag(RenderGraphValidationSeverity::Warning,
                        std::format("resource 0x{:08X} is re-initialized {} times in one frame; check for multiple first-writers.",
                                    summary.Name.Value,
                                    summary.InitializeCount));
            }
        }

        return result;
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
        : m_Config(config)
          , m_DeviceOwner(std::move(device))
          , m_Device(m_DeviceOwner.get())
          , m_Swapchain(swapchain)
          , m_Renderer(renderer)
          , m_FrameScope(frameScope)
          // Sub-Systems (must match declaration order)
          , m_GlobalResources(m_DeviceOwner, descriptorPool, descriptorLayout, bindlessSystem, shaderRegistry,
                              pipelineLibrary, renderer.GetFramesInFlight())
          , m_Presentation(m_DeviceOwner, swapchain, renderer)
          , m_Interaction({.MaxFramesInFlight = renderer.GetFramesInFlight()}, m_DeviceOwner)
          , m_RenderGraph(m_DeviceOwner, frameArena, frameScope)
          , m_GeometryStorage(geometryStorage)
          , m_MaterialSystem(materialSystem)

    {
        // Wire GPU profiler into RenderGraph for per-pass timestamp scoping.
        if (auto* profiler = renderer.GetGpuProfiler())
            m_RenderGraph.SetGpuProfiler(profiler);
        // Register UI panel
        Interface::GUI::RegisterPanel("Render Target Viewer",
                                      [this]()
                                      {
                                          auto& debugView = m_Interaction.GetDebugViewStateMut();
                                          const VkExtent2D presentationExtent = m_Presentation.GetResolution();
                                          const VkExtent2D swapchainExtent = m_Swapchain.GetExtent();
                                          const auto lastPick = m_Interaction.GetLastPickResult();
                                          const auto pipelineDebug = m_ActivePipeline
                                              ? std::optional<RenderPipelineDebugState>(m_ActivePipeline->GetDebugState())
                                              : std::nullopt;
                                          const auto* outlineDebug = m_ActivePipeline ? m_ActivePipeline->GetSelectionOutlineDebugState() : nullptr;
                                          const auto* postDebug = m_ActivePipeline ? m_ActivePipeline->GetPostProcessDebugState() : nullptr;

                                          auto drawBool = [](const char* label, bool value)
                                          {
                                              ImGui::Text("%s:", label);
                                              ImGui::SameLine();
                                              ImGui::TextColored(value ? ImVec4(0.45f, 0.9f, 0.45f, 1.0f) : ImVec4(0.95f, 0.45f, 0.45f, 1.0f),
                                                                 value ? "true" : "false");
                                          };

                                          auto drawRecipeFlag = [&](const char* label, bool value)
                                          {
                                              ImGui::TableNextRow();
                                              ImGui::TableSetColumnIndex(0);
                                              ImGui::TextUnformatted(label);
                                              ImGui::TableSetColumnIndex(1);
                                              ImGui::TextUnformatted(value ? "yes" : "no");
                                          };

                                          auto drawHandleLine = [&](const char* label, bool valid, uint32_t handle)
                                          {
                                              ImGui::Text("%s: %s", label, valid ? "valid" : "invalid");
                                              if (valid)
                                              {
                                                  ImGui::SameLine();
                                                  ImGui::TextDisabled("(resource=%u)", handle);
                                              }
                                          };

                                          auto drawFeatureRow = [&](const char* name,
                                                                    const RenderPipelineFeatureDebugState& state,
                                                                    bool compiled)
                                          {
                                              ImGui::TableNextRow();
                                              ImGui::TableSetColumnIndex(0);
                                              ImGui::TextUnformatted(name);
                                              ImGui::TableSetColumnIndex(1);
                                              ImGui::TextUnformatted(state.Exists ? "yes" : "no");
                                              ImGui::TableSetColumnIndex(2);
                                              ImGui::TextUnformatted(state.Enabled ? "yes" : "no");
                                              ImGui::TableSetColumnIndex(3);
                                              ImGui::TextUnformatted(compiled ? "yes" : "no");
                                          };

                                          if (ImGui::CollapsingHeader("Frame State", ImGuiTreeNodeFlags_DefaultOpen))
                                          {
                                              ImGui::Text("Global Frame: %llu", static_cast<unsigned long long>(m_Device->GetGlobalFrameNumber()));
                                              ImGui::Text("Presentation Frame/Image: %u / %u", m_Presentation.GetFrameIndex(), m_Presentation.GetImageIndex());
                                              ImGui::Text("Last Built Frame/Image: %u / %u", m_LastBuiltFrameIndex, m_LastBuiltImageIndex);
                                              ImGui::Text("Swapchain Extent: %ux%u", swapchainExtent.width, swapchainExtent.height);
                                              ImGui::Text("Presentation Extent: %ux%u", presentationExtent.width, presentationExtent.height);
                                              ImGui::Text("Last Graph Extent: %ux%u", m_LastBuiltGraphExtent.width, m_LastBuiltGraphExtent.height);
                                              ImGui::Text("Swapchain Format: %s", FormatVkFormatName(m_Swapchain.GetImageFormat()));
                                              ImGui::Text("Backbuffer Format: %s", FormatVkFormatName(m_Presentation.GetBackbufferFormat()));
                                              ImGui::Text("Resize Count: %u", m_ResizeCount);
                                              ImGui::Text("Last Resize Extent: %ux%u", m_LastResizeExtent.width, m_LastResizeExtent.height);
                                              ImGui::Text("Last Resize Global Frame: %llu", static_cast<unsigned long long>(m_LastResizeGlobalFrame));
                                              ImGui::Text("Last Pick Result: hit=%s entity=%u", lastPick.HasHit ? "true" : "false", lastPick.EntityID);
                                              ImGui::Text("Compiled Passes / Images: %zu / %zu", m_LastDebugPasses.size(), m_LastDebugImages.size());

                                              if (ImGui::BeginTable("##frame_recipe", 2,
                                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                                              {
                                                  ImGui::TableSetupColumn("Recipe Flag");
                                                  ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                                                  ImGui::TableHeadersRow();
                                                  drawRecipeFlag("Depth", m_LastFrameRecipe.Depth);
                                                  drawRecipeFlag("EntityId", m_LastFrameRecipe.EntityId);
                                                  drawRecipeFlag("PrimitiveId", m_LastFrameRecipe.PrimitiveId);
                                                  drawRecipeFlag("Normals", m_LastFrameRecipe.Normals);
                                                  drawRecipeFlag("MaterialChannels", m_LastFrameRecipe.MaterialChannels);
                                                  drawRecipeFlag("Post", m_LastFrameRecipe.Post);
                                                  drawRecipeFlag("SceneColorLDR", m_LastFrameRecipe.SceneColorLDR);
                                                  drawRecipeFlag("Selection", m_LastFrameRecipe.Selection);
                                                  drawRecipeFlag("DebugVisualization", m_LastFrameRecipe.DebugVisualization);
                                                  ImGui::EndTable();
                                              }
                                          }

                                          if (pipelineDebug && ImGui::CollapsingHeader("Pipeline Feature State", ImGuiTreeNodeFlags_DefaultOpen))
                                          {
                                              drawBool("Has Feature Registry", pipelineDebug->HasFeatureRegistry);
                                              drawBool("Path Dirty", pipelineDebug->PathDirty);
                                              if (ImGui::BeginTable("##pipeline_features", 4,
                                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                                              {
                                                  ImGui::TableSetupColumn("Feature");
                                                  ImGui::TableSetupColumn("Exists", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                                                  ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                                                  ImGui::TableSetupColumn("Compiled", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                                                  ImGui::TableHeadersRow();
                                                  drawFeatureRow("PickingPass", pipelineDebug->PickingPass,
                                                                 CompiledPassNameContains(m_LastDebugPasses, "Pick"));
                                                  drawFeatureRow("SurfacePass", pipelineDebug->SurfacePass,
                                                                 CompiledPassNameContains(m_LastDebugPasses, "Surface"));
                                                  drawFeatureRow("SelectionOutlinePass", pipelineDebug->SelectionOutlinePass,
                                                                 CompiledPassNameContains(m_LastDebugPasses, "SelectionOutline"));
                                                  drawFeatureRow("LinePass", pipelineDebug->LinePass,
                                                                 CompiledPassNameContains(m_LastDebugPasses, "Line"));
                                                  drawFeatureRow("PointPass", pipelineDebug->PointPass,
                                                                 CompiledPassNameContains(m_LastDebugPasses, "Point"));
                                                  drawFeatureRow("PostProcessPass", pipelineDebug->PostProcessPass,
                                                                 CompiledPassNameContains(m_LastDebugPasses, "Post."));
                                                  drawFeatureRow("DebugViewPass", pipelineDebug->DebugViewPass,
                                                                 CompiledPassNameContains(m_LastDebugPasses, "DebugView"));
                                                  drawFeatureRow("ImGuiPass", pipelineDebug->ImGuiPass,
                                                                 CompiledPassNameContains(m_LastDebugPasses, "ImGui"));
                                                  ImGui::EndTable();
                                              }
                                          }

                                          if (outlineDebug && ImGui::CollapsingHeader("Selection Outline Internals", ImGuiTreeNodeFlags_DefaultOpen))
                                          {
                                              drawBool("Initialized", outlineDebug->Initialized);
                                              drawBool("Shader Registry Configured", outlineDebug->ShaderRegistryConfigured);
                                              drawBool("Pipeline Built", outlineDebug->PipelineBuilt);
                                              drawBool("Dummy Pick ID Allocated", outlineDebug->DummyPickIdAllocated);
                                              drawBool("Last Pass Requested", outlineDebug->LastPassRequested);
                                              drawBool("Last Pass Added", outlineDebug->LastPassAdded);
                                              drawBool("Last Descriptor Patched", outlineDebug->LastDescriptorPatched);
                                              ImGui::Text("Descriptor Sets: %u", outlineDebug->DescriptorSetCount);
                                              ImGui::Text("Last Frame Index: %u", outlineDebug->LastFrameIndex);
                                              ImGui::Text("Last Resolution: %ux%u", outlineDebug->LastResolutionWidth, outlineDebug->LastResolutionHeight);
                                              ImGui::Text("Last Color Format: %s", FormatVkFormatName(outlineDebug->LastColorFormat));
                                              ImGui::Text("Last Selected Count / Hovered ID: %u / %u",
                                                          outlineDebug->LastSelectedCount, outlineDebug->LastHoveredId);
                                              drawHandleLine("EntityId Handle", outlineDebug->LastEntityIdHandleValid, outlineDebug->LastEntityIdHandle);
                                              drawHandleLine("Target Handle", outlineDebug->LastTargetHandleValid, outlineDebug->LastTargetHandle);
                                              ImGui::Text("Resize Count: %u", outlineDebug->ResizeCount);
                                              ImGui::Text("Last Resize: %ux%u", outlineDebug->LastResizeWidth, outlineDebug->LastResizeHeight);
                                              if (outlineDebug->LastSelectedCount > 0)
                                              {
                                                  ImGui::TextUnformatted("Selected Pick IDs:");
                                                  for (uint32_t i = 0; i < std::min(outlineDebug->LastSelectedCount, Passes::kSelectionOutlineDebugMaxSelectedIds); ++i)
                                                      ImGui::BulletText("[%u] %u", i, outlineDebug->LastSelectedIds[i]);
                                              }
                                          }

                                          if (postDebug && ImGui::CollapsingHeader("Post Process Internals", ImGuiTreeNodeFlags_DefaultOpen))
                                          {
                                              drawBool("Initialized", postDebug->Initialized);
                                              drawBool("Shader Registry Configured", postDebug->ShaderRegistryConfigured);
                                              drawBool("Dummy Sampled Allocated", postDebug->DummySampledAllocated);
                                              drawBool("ToneMap Pipeline Built", postDebug->ToneMapPipelineBuilt);
                                              drawBool("FXAA Pipeline Built", postDebug->FXAAPipelineBuilt);
                                              drawBool("SMAA Edge Pipeline Built", postDebug->SMAAEdgePipelineBuilt);
                                              drawBool("SMAA Blend Pipeline Built", postDebug->SMAABlendPipelineBuilt);
                                              drawBool("SMAA Resolve Pipeline Built", postDebug->SMAAResolvePipelineBuilt);
                                              drawBool("Bloom Down Pipeline Built", postDebug->BloomDownPipelineBuilt);
                                              drawBool("Bloom Up Pipeline Built", postDebug->BloomUpPipelineBuilt);
                                              drawBool("Histogram Pipeline Built", postDebug->HistogramPipelineBuilt);
                                              drawBool("Bloom Enabled", postDebug->BloomEnabled);
                                              drawBool("Histogram Enabled", postDebug->HistogramEnabled);
                                              drawBool("FXAA Enabled", postDebug->FXAAEnabled);
                                              drawBool("SMAA Enabled", postDebug->SMAAEnabled);
                                              ImGui::Text("Last Frame Index: %u", postDebug->LastFrameIndex);
                                              ImGui::Text("Last Resolution: %ux%u", postDebug->LastResolutionWidth, postDebug->LastResolutionHeight);
                                              ImGui::Text("Last Output Format: %s", FormatVkFormatName(postDebug->LastOutputFormat));
                                              drawHandleLine("SceneColor Handle", postDebug->LastSceneColorHandleValid, postDebug->LastSceneColorHandle);
                                              drawHandleLine("PostLdr Handle", postDebug->LastPostLdrHandleValid, postDebug->LastPostLdrHandle);
                                              drawHandleLine("BloomMip0 Handle", postDebug->LastBloomMip0HandleValid, postDebug->LastBloomMip0Handle);
                                              drawHandleLine("SMAAEdges Handle", postDebug->LastSMAAEdgesHandleValid, postDebug->LastSMAAEdgesHandle);
                                              drawHandleLine("SMAAWeights Handle", postDebug->LastSMAAWeightsHandleValid, postDebug->LastSMAAWeightsHandle);
                                              ImGui::Text("Resize Count: %u", postDebug->ResizeCount);
                                              ImGui::Text("Last Resize: %ux%u", postDebug->LastResizeWidth, postDebug->LastResizeHeight);
                                              if (ImGui::TreeNode("Bloom Handle Arrays"))
                                              {
                                                  for (uint32_t mip = 0; mip < Passes::kPostProcessDebugBloomMipCount; ++mip)
                                                  {
                                                      ImGui::BulletText("Down[%u]=%u  UpSrc[%u]=%u",
                                                                        mip, postDebug->LastBloomDownHandles[mip],
                                                                        mip, postDebug->LastBloomUpSourceHandles[mip]);
                                                  }
                                                  ImGui::TreePop();
                                              }
                                          }

                                          ImGui::Separator();
                                          ImGui::Checkbox("Enable Debug View", &debugView.Enabled);
                                          ImGui::Checkbox("Show debug view in viewport", &debugView.ShowInViewport);
                                          ImGui::Checkbox("Disable GPU culling", &debugView.DisableCulling);
                                          if (!debugView.Enabled)
                                              ImGui::TextDisabled("Debug-view visualization is disabled; internal diagnostics remain available below.");
                                          ImGui::Separator();

                                          // --- Resource List (flat, with details) ---
                                          // Build a mapping from known RenderResource StringIDs to readable names.
                                          auto resolveName = [](Core::Hash::StringID id) -> const char*
                                          {
                                              return ResolveRenderResourceDebugName(id);
                                          };

                                          auto formatName = [](VkFormat fmt) -> const char*
                                          {
                                              return FormatVkFormatName(fmt);
                                          };

                                          // --- Pass-organized resource browser ---
                                          if (ImGui::CollapsingHeader("Per-Pass Attachments", ImGuiTreeNodeFlags_DefaultOpen))
                                          {
                                              for (const auto& pass : m_LastDebugPasses)
                                              {
                                                  if (!ImGui::TreeNode(pass.Name))
                                                      continue;

                                                  for (const auto& att : pass.Attachments)
                                                  {
                                                      const bool isSelected = (att.ResourceName == debugView.SelectedResource);
                                                      const char* name = resolveName(att.ResourceName);
                                                      char label[192];
                                                      if (name)
                                                          snprintf(label, sizeof(label), "%s [%s]%s",
                                                                   name, formatName(att.Format),
                                                                   att.IsDepth ? " (Depth)" : "");
                                                      else
                                                          snprintf(label, sizeof(label), "0x%08X [%s]%s",
                                                                   att.ResourceName.Value, formatName(att.Format),
                                                                   att.IsDepth ? " (Depth)" : "");

                                                      if (ImGui::Selectable(label, isSelected))
                                                          debugView.SelectedResource = att.ResourceName;

                                                      // Tooltip with details
                                                      if (ImGui::IsItemHovered())
                                                      {
                                                          ImGui::BeginTooltip();
                                                          ImGui::Text("Format: %s", formatName(att.Format));
                                                          ImGui::Text("Load: %s",
                                                              att.LoadOp == VK_ATTACHMENT_LOAD_OP_LOAD ? "LOAD" :
                                                              att.LoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? "CLEAR" : "DONT_CARE");
                                                          ImGui::Text("Store: %s",
                                                              att.StoreOp == VK_ATTACHMENT_STORE_OP_STORE ? "STORE" : "DONT_CARE");
                                                          if (att.IsImported)
                                                              ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Imported");
                                                          ImGui::EndTooltip();
                                                      }
                                                  }

                                                  ImGui::TreePop();
                                              }
                                          }

                                          // --- Resource lifetime summary ---
                                          if (ImGui::CollapsingHeader("Resource Lifetimes"))
                                          {
                                              auto estimateBytes = [](const RenderGraphDebugImage& img) -> uint64_t
                                              {
                                                  uint32_t bpp = 4; // default
                                                  switch (img.Format)
                                                  {
                                                  case VK_FORMAT_R32_UINT:              bpp = 4; break;
                                                  case VK_FORMAT_R8_UNORM:              bpp = 1; break;
                                                  case VK_FORMAT_R8G8B8A8_UNORM:
                                                  case VK_FORMAT_R8G8B8A8_SRGB:
                                                  case VK_FORMAT_B8G8R8A8_UNORM:
                                                  case VK_FORMAT_B8G8R8A8_SRGB:         bpp = 4; break;
                                                  case VK_FORMAT_R16G16B16A16_SFLOAT:   bpp = 8; break;
                                                  case VK_FORMAT_D32_SFLOAT:            bpp = 4; break;
                                                  case VK_FORMAT_D24_UNORM_S8_UINT:     bpp = 4; break;
                                                  case VK_FORMAT_D32_SFLOAT_S8_UINT:    bpp = 5; break;
                                                  default: break;
                                                  }
                                                  return uint64_t(img.Extent.width) * img.Extent.height * bpp;
                                              };

                                              uint64_t totalTransientBytes = 0;
                                              uint64_t totalImportedBytes  = 0;

                                              if (ImGui::BeginTable("##res_lifetimes", 6,
                                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
                                              {
                                                  ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthStretch);
                                                  ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                                                  ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                                                  ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                                                  ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                                                  ImGui::TableSetupColumn("Alive", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                                                  ImGui::TableHeadersRow();

                                                  for (const auto& img : m_LastDebugImages)
                                                  {
                                                      ImGui::TableNextRow();

                                                      // Name
                                                      ImGui::TableSetColumnIndex(0);
                                                      const char* readableName = resolveName(img.Name);
                                                      const bool isSelected = (img.Name == debugView.SelectedResource);
                                                      char nameLabel[128];
                                                      if (readableName)
                                                          snprintf(nameLabel, sizeof(nameLabel), "%s##%u", readableName, img.Name.Value);
                                                      else
                                                          snprintf(nameLabel, sizeof(nameLabel), "0x%08X", img.Name.Value);
                                                      if (ImGui::Selectable(nameLabel, isSelected, ImGuiSelectableFlags_SpanAllColumns))
                                                          debugView.SelectedResource = img.Name;

                                                      // Size
                                                      ImGui::TableSetColumnIndex(1);
                                                      ImGui::Text("%ux%u", img.Extent.width, img.Extent.height);

                                                      // Format
                                                      ImGui::TableSetColumnIndex(2);
                                                      ImGui::TextUnformatted(formatName(img.Format));

                                                      // Type
                                                      ImGui::TableSetColumnIndex(3);
                                                      ImGui::TextUnformatted(img.IsImported ? "Imported" : "Transient");

                                                      // Memory estimate
                                                      ImGui::TableSetColumnIndex(4);
                                                      const uint64_t bytes = estimateBytes(img);
                                                      if (bytes >= 1024 * 1024)
                                                          ImGui::Text("%.1f MB", bytes / (1024.0 * 1024.0));
                                                      else
                                                          ImGui::Text("%.0f KB", bytes / 1024.0);
                                                      if (img.IsImported)
                                                          totalImportedBytes += bytes;
                                                      else
                                                          totalTransientBytes += bytes;

                                                      // Alive range
                                                      ImGui::TableSetColumnIndex(5);
                                                      ImGui::Text("[%u,%u]", img.StartPass, img.EndPass);
                                                  }

                                                  ImGui::EndTable();
                                              }

                                              // Memory summary
                                              ImGui::TextDisabled("Transient: %.1f MB  |  Imported: %.1f MB",
                                                  totalTransientBytes / (1024.0 * 1024.0),
                                                  totalImportedBytes / (1024.0 * 1024.0));
                                          }

                                          // --- Resource Lifetime Timeline ---
                                          if (ImGui::CollapsingHeader("Lifetime Timeline"))
                                          {
                                              // Determine total pass count for scaling
                                              uint32_t maxPass = 0;
                                              for (const auto& img : m_LastDebugImages)
                                                  maxPass = std::max(maxPass, img.EndPass);
                                              const uint32_t totalPasses = maxPass + 1;

                                              if (totalPasses > 0 && !m_LastDebugImages.empty())
                                              {
                                                  // Pass name header
                                                  const float labelWidth = 120.0f;
                                                  const float barHeight  = 16.0f;
                                                  const float rowSpacing = 2.0f;
                                                  const float availWidth = ImGui::GetContentRegionAvail().x - labelWidth - 8.0f;
                                                  const float passWidth  = availWidth / static_cast<float>(totalPasses);

                                                  // Column headers: pass indices
                                                  ImGui::Dummy(ImVec2(labelWidth, 0.0f));
                                                  ImGui::SameLine();
                                                  ImVec2 headerPos = ImGui::GetCursorScreenPos();
                                                  for (uint32_t p = 0; p < totalPasses && p < m_LastDebugPasses.size(); p++)
                                                  {
                                                      float x = headerPos.x + p * passWidth;
                                                      if (passWidth >= 30.0f) // Only show labels if enough room
                                                      {
                                                          ImGui::GetWindowDrawList()->AddText(
                                                              ImVec2(x + 2.0f, headerPos.y),
                                                              IM_COL32(180, 180, 180, 200),
                                                              m_LastDebugPasses[p].Name);
                                                      }
                                                  }
                                                  ImGui::Dummy(ImVec2(availWidth, barHeight));

                                                  // Resource bars
                                                  auto* drawList = ImGui::GetWindowDrawList();
                                                  for (const auto& img : m_LastDebugImages)
                                                  {
                                                      // Resource name label
                                                      const char* rName = resolveName(img.Name);
                                                      char label[64];
                                                      if (rName)
                                                          snprintf(label, sizeof(label), "%s", rName);
                                                      else
                                                          snprintf(label, sizeof(label), "0x%08X", img.Name.Value);
                                                      ImGui::TextUnformatted(label);
                                                      ImGui::SameLine(labelWidth);

                                                      ImVec2 cursor = ImGui::GetCursorScreenPos();

                                                      // Background (full timeline)
                                                      drawList->AddRectFilled(
                                                          cursor,
                                                          ImVec2(cursor.x + availWidth, cursor.y + barHeight),
                                                          IM_COL32(40, 40, 40, 180));

                                                      // Alive range bar
                                                      if (img.StartPass <= img.EndPass)
                                                      {
                                                          float x0 = cursor.x + img.StartPass * passWidth;
                                                          float x1 = cursor.x + (img.EndPass + 1) * passWidth;
                                                          ImU32 barColor = img.IsImported
                                                              ? IM_COL32(80, 160, 220, 200)   // Blue for imported
                                                              : IM_COL32(100, 200, 100, 200); // Green for transient
                                                          drawList->AddRectFilled(
                                                              ImVec2(x0, cursor.y + 1),
                                                              ImVec2(x1, cursor.y + barHeight - 1),
                                                              barColor, 2.0f);

                                                          // Write range highlight (brighter overlay)
                                                          if (img.FirstWritePass != ~0u && img.LastWritePass != ~0u)
                                                          {
                                                              float wx0 = cursor.x + img.FirstWritePass * passWidth;
                                                              float wx1 = cursor.x + (img.LastWritePass + 1) * passWidth;
                                                              drawList->AddRectFilled(
                                                                  ImVec2(wx0, cursor.y + 1),
                                                                  ImVec2(wx1, cursor.y + barHeight / 2),
                                                                  IM_COL32(255, 200, 80, 120), 1.0f);
                                                          }
                                                      }

                                                      // Tooltip on hover
                                                      ImGui::Dummy(ImVec2(availWidth, barHeight + rowSpacing));
                                                      if (ImGui::IsItemHovered())
                                                      {
                                                          ImGui::BeginTooltip();
                                                          ImGui::Text("%s", label);
                                                          ImGui::Text("Alive: [%u, %u]", img.StartPass, img.EndPass);
                                                          if (img.FirstWritePass != ~0u)
                                                              ImGui::Text("Write: [%u, %u]", img.FirstWritePass, img.LastWritePass);
                                                          if (img.FirstReadPass != ~0u)
                                                              ImGui::Text("Read:  [%u, %u]", img.FirstReadPass, img.LastReadPass);
                                                          ImGui::Text("Size: %ux%u  Format: %s",
                                                              img.Extent.width, img.Extent.height, formatName(img.Format));
                                                          ImGui::Text("%s", img.IsImported ? "Imported" : "Transient");
                                                          ImGui::EndTooltip();
                                                      }
                                                  }

                                                  // Legend
                                                  ImGui::Spacing();
                                                  ImVec2 legendPos = ImGui::GetCursorScreenPos();
                                                  drawList->AddRectFilled(legendPos, ImVec2(legendPos.x + 12, legendPos.y + 12), IM_COL32(100, 200, 100, 200), 2.0f);
                                                  ImGui::Dummy(ImVec2(14, 12)); ImGui::SameLine();
                                                  ImGui::TextDisabled("Transient");
                                                  ImGui::SameLine();
                                                  legendPos = ImGui::GetCursorScreenPos();
                                                  drawList->AddRectFilled(legendPos, ImVec2(legendPos.x + 12, legendPos.y + 12), IM_COL32(80, 160, 220, 200), 2.0f);
                                                  ImGui::Dummy(ImVec2(14, 12)); ImGui::SameLine();
                                                  ImGui::TextDisabled("Imported");
                                                  ImGui::SameLine();
                                                  legendPos = ImGui::GetCursorScreenPos();
                                                  drawList->AddRectFilled(legendPos, ImVec2(legendPos.x + 12, legendPos.y + 6), IM_COL32(255, 200, 80, 120), 1.0f);
                                                  ImGui::Dummy(ImVec2(14, 12)); ImGui::SameLine();
                                                  ImGui::TextDisabled("Write range");
                                              }
                                              else
                                              {
                                                  ImGui::TextDisabled("No render graph data available.");
                                              }
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

    Passes::SelectionOutlineSettings* RenderSystem::GetSelectionOutlineSettings()
    {
        return m_ActivePipeline ? m_ActivePipeline->GetSelectionOutlineSettings() : nullptr;
    }

    Passes::PostProcessSettings* RenderSystem::GetPostProcessSettings()
    {
        return m_ActivePipeline ? m_ActivePipeline->GetPostProcessSettings() : nullptr;
    }

    const Passes::HistogramReadback* RenderSystem::GetHistogramReadback() const
    {
        return m_ActivePipeline ? m_ActivePipeline->GetHistogramReadback() : nullptr;
    }

    // -------------------------------------------------------------------------
    // OnUpdate sub-steps
    // -------------------------------------------------------------------------

    void RenderSystem::BeginFrame(uint64_t currentFrame)
    {
        m_GeometryStorage.ProcessDeletions(currentFrame);
        GarbageCollectRetiredPipelines();

        // Apply deferred bindless updates before any render graph recording.
        m_GlobalResources.GetBindlessSystem().FlushPending();

        // NOTE: ProcessReadbacks was moved to after AcquireFrame (fence wait) to ensure
        // the GPU has actually completed writing to the readback buffer before we read it.

        Interface::GUI::BeginFrame();
        Interface::GUI::DrawGUI();
    }

    bool RenderSystem::AcquireFrame()
    {
        if (!m_Presentation.BeginFrame())
        {
            Interface::GUI::EndFrame();
            return false;
        }
        return true;
    }

    void RenderSystem::UpdateGlobals(const CameraComponent& camera)
    {
        const uint32_t frameIndex = m_Presentation.GetFrameIndex();
        const auto extent = m_Presentation.GetResolution();

        m_GlobalResources.BeginFrame(frameIndex);
        m_GlobalResources.Update(camera, frameIndex);

        ApplyPendingPipelineSwap(extent.width, extent.height);
    }

    void RenderSystem::BuildGraph(ECS::Scene& scene, Core::Assets::AssetManager& assetManager,
                                  const CameraComponent& camera)
    {
        const uint32_t frameIndex = m_Presentation.GetFrameIndex();
        m_RenderGraph.Reset(frameIndex);

        // DebugDraw is transient per-frame and is reset by Engine::Run via
        // RenderOrchestrator::ResetFrameState() before client OnUpdate emits lines.
        // Do NOT clear here or we erase client-submitted debug lines (bounds/octree/kdtree)
        // right before pipeline passes consume them.

        const uint32_t imageIndex = m_Presentation.GetImageIndex();
        const auto extent = m_Presentation.GetResolution();
        m_LastBuiltFrameIndex = frameIndex;
        m_LastBuiltImageIndex = imageIndex;
        m_LastBuiltGraphExtent = extent;
        RenderBlackboard blackboard;

        const auto& pendingPick = m_Interaction.GetPendingPick();
        const auto& debugView = m_Interaction.GetDebugViewState();

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
            {},
            imageIndex,
            m_Swapchain.GetImageFormat(),
            m_Presentation.GetDepthBuffer().GetFormat(),
            VK_FORMAT_R16G16B16A16_SFLOAT,
            m_Renderer,
            m_GlobalResources.GetCameraUBO(),
            m_GlobalResources.GetGlobalDescriptorSet(),
            m_GlobalResources.GetDynamicUBOOffset(frameIndex),
            m_GlobalResources.GetBindlessSystem(),
            {pendingPick.Pending, pendingPick.X, pendingPick.Y},
            {
                debugView.Enabled, debugView.ShowInViewport, debugView.DisableCulling, debugView.SelectedResource,
                debugView.DepthNear, debugView.DepthFar
            },
            m_LastDebugImages,
            m_LastDebugPasses,
            camera.ViewMatrix,
            camera.ProjectionMatrix,
            m_Interaction.GetReadbackBuffer(frameIndex),
            m_DebugDraw
        };

        if (m_ActivePipeline)
            ctx.Recipe = m_ActivePipeline->BuildFrameRecipe(ctx);
        else
        {
            ctx.Recipe.Depth = true;
            ctx.Recipe.EntityId = pendingPick.Pending || HasSelectionWork(scene) || debugView.Enabled;
            ctx.Recipe.DebugVisualization = debugView.Enabled;
            ctx.Recipe.Selection = HasSelectionWork(scene);
            ctx.Recipe.LightingPath = FrameLightingPath::Forward;
            ctx.Recipe.Post = true;
            ctx.Recipe.SceneColorLDR = true;
        }
        m_LastFrameRecipe = ctx.Recipe;

        struct FrameSetupData
        {
            std::array<RGResourceHandle, 10> Resources{};
            RGResourceHandle Backbuffer;
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
                                                  blackboard.Add("Backbuffer"_id, data.Backbuffer);

                                                  for (RenderResource resource : {
                                                           RenderResource::SceneDepth,
                                                           RenderResource::EntityId,
                                                           RenderResource::PrimitiveId,
                                                           RenderResource::SceneNormal,
                                                           RenderResource::Albedo,
                                                           RenderResource::Material0,
                                                           RenderResource::SceneColorHDR,
                                                           RenderResource::SceneColorLDR,
                                                           RenderResource::SelectionMask,
                                                           RenderResource::SelectionOutline,
                                                       })
                                                  {
                                                      if (!ctx.Recipe.Requires(resource))
                                                          continue;

                                                      const auto index = static_cast<size_t>(resource);
                                                      const auto def = GetRenderResourceDefinition(resource);
                                                      if (def.Lifetime == RenderResourceLifetime::Imported)
                                                      {
                                                          auto& depthImg = m_Presentation.GetDepthBuffer();
                                                          data.Resources[index] = builder.ImportTexture(
                                                              def.Name,
                                                              depthImg.GetHandle(),
                                                              depthImg.GetView(),
                                                              depthImg.GetFormat(),
                                                              extent,
                                                              VK_IMAGE_LAYOUT_UNDEFINED);
                                                      }
                                                      else
                                                      {
                                                          data.Resources[index] = builder.CreateTexture(
                                                              def.Name,
                                                              BuildRenderResourceTextureDesc(resource, ctx));
                                                      }
                                                      blackboard.Add(resource, data.Resources[index]);
                                                  }
                                              },
                                              [](const FrameSetupData&, const RGRegistry&, VkCommandBuffer)
                                              {
                                              });

        // GPUScene update pass — syncs CPU-side scene data to GPU SSBOs.
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
                                               [this, frameIndex](const SceneUpdateData&, const RGRegistry&, VkCommandBuffer cmd)
                                               {
                                                   if (m_GpuScene) m_GpuScene->Sync(cmd, frameIndex);
                                               });

        auto stable = m_FrameScope.New<RenderPassContext>(ctx);
        if (stable)
        {
            RenderPassContext* stableCtx = *stable;
            if (m_ActivePipeline)
                m_ActivePipeline->SetupFrame(*stableCtx);
        }
        else
        {
            Core::Log::Error("RenderSystem::BuildGraph failed to allocate stable RenderPassContext from frame scope");
            if (m_ActivePipeline)
                m_ActivePipeline->SetupFrame(ctx);
        }
    }

    void RenderSystem::ExecuteGraph()
    {
        const uint32_t frameIndex = m_Presentation.GetFrameIndex();

        m_RenderGraph.Compile(frameIndex);

        m_LastDebugPasses = m_RenderGraph.BuildDebugPassList();
        m_LastDebugImages = m_RenderGraph.BuildDebugImageList();

        // Structured render graph validation with imported-resource write policies.
        const auto validationResult = ValidateCompiledGraph(
            m_LastFrameRecipe, m_LastDebugPasses, m_LastDebugImages);
        for (const auto& diag : validationResult.Diagnostics)
        {
            if (diag.Severity == RenderGraphValidationSeverity::Error)
                Core::Log::Error("RenderGraph validation: {}", diag.Message);
            else
                Core::Log::Warn("RenderGraph validation: {}", diag.Message);
        }

        if (m_Config.EnableRenderAuditLogging)
            LogRenderAudit(m_LastDebugPasses, m_LastDebugImages);

        if (m_ActivePipeline)
            m_ActivePipeline->PostCompile(frameIndex, m_LastDebugImages, m_LastDebugPasses);

        m_RenderGraph.Execute(m_Presentation.GetCommandBuffer());

        // Feed per-pass CPU timings from RenderGraph into Telemetry.
        const auto& passTimings = m_RenderGraph.GetLastPassTimings();
        if (!passTimings.empty())
        {
            std::vector<std::pair<std::string, uint64_t>> cpuTimings;
            cpuTimings.reserve(passTimings.size());
            for (const auto& pt : passTimings)
                cpuTimings.emplace_back(pt.Name, pt.CpuTimeNs);
            Core::Telemetry::TelemetrySystem::Get().MergePassCpuTimings(cpuTimings);
        }
    }

    void RenderSystem::EndFrame()
    {
        m_Presentation.EndFrame();
    }

    // -------------------------------------------------------------------------
    // OnUpdate — thin coordinator that calls sub-steps in order
    // -------------------------------------------------------------------------

    void RenderSystem::OnUpdate(ECS::Scene& scene, const CameraComponent& camera,
                                Core::Assets::AssetManager& assetManager)
    {
        const uint64_t currentFrame = m_Device->GetGlobalFrameNumber();

        BeginFrame(currentFrame);

        if (!AcquireFrame())
            return;

        // Process GPU pick readbacks AFTER the fence wait in AcquireFrame.
        // The fence guarantees that the GPU has completed writing to the readback buffer
        // for the previous use of this frame index. Using (currentFrame - framesInFlight)
        // as the completed frame ensures we only consume results that are definitely done.
        {
            const uint32_t framesInFlight = m_Renderer.GetFramesInFlight();
            const uint64_t safeCompleted = (currentFrame > framesInFlight)
                ? (currentFrame - framesInFlight)
                : 0;
            m_Interaction.ProcessReadbacks(safeCompleted);
        }

        UpdateGlobals(camera);
        BuildGraph(scene, assetManager, camera);
        ExecuteGraph();
        EndFrame();
    }

    void RenderSystem::OnResize()
    {
        ++m_ResizeCount;
        m_LastResizeExtent = m_Presentation.GetResolution();
        m_LastResizeGlobalFrame = m_Device ? m_Device->GetGlobalFrameNumber() : 0;
        m_RenderGraph.Trim();
        m_Presentation.OnResize();
        auto extent = m_Presentation.GetResolution();
        m_LastResizeExtent = extent;
        if (m_ActivePipeline) m_ActivePipeline->OnResize(extent.width, extent.height);
    }

    // -------------------------------------------------------------------------
    // DumpRenderGraphToString — human-readable render graph snapshot
    // -------------------------------------------------------------------------

    std::string RenderSystem::DumpRenderGraphToString() const
    {
        std::string out;
        out.reserve(4096);

        auto loadOpStr = [](VkAttachmentLoadOp op) -> const char* {
            switch (op) {
                case VK_ATTACHMENT_LOAD_OP_LOAD:      return "LOAD";
                case VK_ATTACHMENT_LOAD_OP_CLEAR:     return "CLEAR";
                case VK_ATTACHMENT_LOAD_OP_DONT_CARE: return "DONT_CARE";
                default:                              return "?";
            }
        };
        auto storeOpStr = [](VkAttachmentStoreOp op) -> const char* {
            switch (op) {
                case VK_ATTACHMENT_STORE_OP_STORE:     return "STORE";
                case VK_ATTACHMENT_STORE_OP_DONT_CARE: return "DONT_CARE";
                default:                               return "?";
            }
        };

        // --- Pass execution order ---
        out += std::format("=== Render Graph Dump ({} passes, {} images) ===\n\n",
                           m_LastDebugPasses.size(), m_LastDebugImages.size());

        out += "--- Pass Execution Order ---\n";
        for (const auto& pass : m_LastDebugPasses)
        {
            out += std::format("  [{}] {}\n", pass.PassIndex, pass.Name);
            for (const auto& att : pass.Attachments)
            {
                out += std::format("      {} 0x{:08X}  fmt={}  load={}  store={}  {}{}\n",
                                   att.IsDepth ? "depth" : "color",
                                   att.ResourceName.Value,
                                   static_cast<int>(att.Format),
                                   loadOpStr(att.LoadOp),
                                   storeOpStr(att.StoreOp),
                                   att.IsImported ? "[imported]" : "",
                                   att.LoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? " [initializer]" : "");
            }
        }

        // --- Resource lifetimes ---
        out += "\n--- Resource Lifetimes ---\n";
        for (const auto& img : m_LastDebugImages)
        {
            out += std::format("  0x{:08X}  {}x{}  fmt={}  {}  alive=[{},{}]",
                               img.Name.Value,
                               img.Extent.width, img.Extent.height,
                               static_cast<int>(img.Format),
                               img.IsImported ? "imported" : "transient",
                               img.StartPass, img.EndPass);
            if (img.FirstWritePass != ~0u || img.LastWritePass != ~0u)
                out += std::format("  write=[{},{}]", img.FirstWritePass, img.LastWritePass);
            if (img.FirstReadPass != ~0u || img.LastReadPass != ~0u)
                out += std::format("  read=[{},{}]", img.FirstReadPass, img.LastReadPass);
            out += "\n";
        }

        return out;
    }
}
