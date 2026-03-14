module;

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include "RHI.Vulkan.hpp"

module Graphics:Passes.SelectionOutline.Impl;

import :Passes.SelectionOutline;
import :Components;
import :RenderPipeline;
import :RenderGraph;
import :ShaderRegistry;
import Core.Hash;
import Core.Logging;
import Core.Filesystem;
import RHI;
import ECS;

using namespace Core::Hash;

#include "Graphics.PassUtils.hpp"

namespace Graphics::Passes
{
    namespace
    {
        template <typename Fn>
        void ForEachOutlineCandidateInSubtree(const entt::registry& registry, entt::entity root, Fn&& fn)
        {
            if (!registry.valid(root))
                return;

            std::vector<entt::entity> stack;
            stack.push_back(root);

            while (!stack.empty())
            {
                const entt::entity current = stack.back();
                stack.pop_back();

                fn(current);

                const auto* hierarchy = registry.try_get<ECS::Components::Hierarchy::Component>(current);
                if (!hierarchy)
                    continue;

                for (entt::entity child = hierarchy->FirstChild; child != entt::null;)
                {
                    entt::entity nextSibling = entt::null;
                    if (const auto* childHierarchy = registry.try_get<ECS::Components::Hierarchy::Component>(child))
                        nextSibling = childHierarchy->NextSibling;
                    stack.push_back(child);
                    child = nextSibling;
                }
            }
        }

        [[nodiscard]] bool CanEmitOutlinePickId(const entt::registry& registry, entt::entity entity)
        {
            return registry.valid(entity) &&
                   registry.all_of<ECS::Surface::Component, ECS::Components::Selection::PickID>(entity);
        }
    }

    uint32_t AppendOutlineRenderablePickIds(const entt::registry& registry,
                                            entt::entity root,
                                            std::span<uint32_t> outIds,
                                            uint32_t count)
    {
        if (count >= outIds.size())
            return count;

        ForEachOutlineCandidateInSubtree(registry, root,
            [&](entt::entity candidate)
            {
                if (count >= outIds.size() || !CanEmitOutlinePickId(registry, candidate))
                    return;

                const uint32_t pickId = registry.get<ECS::Components::Selection::PickID>(candidate).Value;
                if (pickId == 0u)
                    return;

                if (std::find(outIds.begin(), outIds.begin() + static_cast<std::ptrdiff_t>(count), pickId) !=
                    outIds.begin() + static_cast<std::ptrdiff_t>(count))
                    return;

                outIds[count++] = pickId;
            });

        return count;
    }

    uint32_t ResolveOutlineRenderablePickId(const entt::registry& registry, entt::entity root)
    {
        uint32_t result = 0u;
        ForEachOutlineCandidateInSubtree(registry, root,
            [&](entt::entity candidate)
            {
                if (result != 0u || !CanEmitOutlinePickId(registry, candidate))
                    return;
                result = registry.get<ECS::Components::Selection::PickID>(candidate).Value;
            });
        return result;
    }

    void SelectionOutlinePass::Initialize(RHI::VulkanDevice& device,
                                          RHI::DescriptorAllocator& descriptorPool,
                                          RHI::DescriptorLayout&)
    {
        m_Device = &device;
        m_DebugState.Initialized = true;

        m_Sampler = CreateNearestSampler(m_Device->GetLogicalDevice(), "SelectionOutline");
        m_DescriptorSetLayout = CreateSamplerDescriptorSetLayout(
            m_Device->GetLogicalDevice(), VK_SHADER_STAGE_FRAGMENT_BIT, "SelectionOutline");

        // Allocate per-frame descriptor sets
        m_DescriptorSets.resize(RHI::VulkanDevice::GetFramesInFlight());
        for (auto& set : m_DescriptorSets)
            set = descriptorPool.Allocate(m_DescriptorSetLayout);
        m_DebugState.DescriptorSetCount = static_cast<uint32_t>(m_DescriptorSets.size());

        // Create a dummy 1x1 R32_UINT image for initial descriptor binding
        m_DummyPickId = std::make_unique<RHI::VulkanImage>(
            *m_Device, 1, 1, 1,
            VK_FORMAT_R32_UINT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        m_DebugState.DummyPickIdAllocated = static_cast<bool>(m_DummyPickId);

        // Transition dummy to SHADER_READ_ONLY_OPTIMAL
        TransitionImageToShaderRead(*m_Device, m_DummyPickId->GetHandle(), VK_IMAGE_ASPECT_COLOR_BIT);

        // Initialize descriptor bindings with the dummy image
        for (auto& set : m_DescriptorSets)
        {
            UpdateImageDescriptor(m_Device->GetLogicalDevice(), set, 0,
                                  m_Sampler, m_DummyPickId->GetView(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // Keep the dummy alive until replaced in PostCompile.
    }

    void SelectionOutlinePass::AddPasses(RenderPassContext& ctx)
    {
        // Skip if no entities are selected or hovered
        auto& registry = ctx.Scene.GetRegistry();

        // Count selected entities and collect their PickIDs.
        // Selection can target non-renderable hierarchy parents from the editor tree,
        // while the PickID buffer is written by renderable descendants.
        uint32_t selectedCount = 0;
        uint32_t selectedIds[kMaxSelectedIds] = {};
        uint32_t hoveredId = 0;
        bool hasHovered = false;

        auto selectedView = registry.view<ECS::Components::Selection::SelectedTag>();

        for (auto entity : selectedView)
            selectedCount = AppendOutlineRenderablePickIds(registry, entity, selectedIds, selectedCount);

        auto hoveredView = registry.view<ECS::Components::Selection::HoveredTag>();

        for (auto entity : hoveredView)
        {
            hoveredId = ResolveOutlineRenderablePickId(registry, entity);
            hasHovered = hoveredId != 0u;
            if (hasHovered)
                break; // Only one hovered entity at a time
        }

        m_DebugState.LastFrameIndex = ctx.FrameIndex;
        m_DebugState.LastResolutionWidth = ctx.Resolution.width;
        m_DebugState.LastResolutionHeight = ctx.Resolution.height;
        m_DebugState.LastSelectedCount = selectedCount;
        m_DebugState.LastHoveredId = hoveredId;
        std::copy_n(selectedIds, kMaxSelectedIds, m_DebugState.LastSelectedIds.begin());
        m_DebugState.LastPassRequested = (selectedCount != 0u) || hasHovered;
        m_DebugState.LastPassAdded = false;
        m_DebugState.LastDescriptorPatched = false;
        m_DebugState.LastEntityIdHandleValid = false;
        m_DebugState.LastTargetHandleValid = false;
        m_DebugState.LastEntityIdHandle = 0;
        m_DebugState.LastTargetHandle = 0;
        m_DebugState.LastColorFormat = ctx.SwapchainFormat;

        // Clear the cached handle unless we add the pass.
        m_LastPickIdHandle = {};

        // Early out: nothing to outline
        if (selectedCount == 0 && !hasHovered)
            return;

        if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0)
            return;
        if (ctx.Resolution.width == ~0u || ctx.Resolution.height == ~0u)
            return;

        // Lazy pipeline build once we know swapchain format
        if (!m_Pipeline)
        {
            if (!m_ShaderRegistry)
            {
                Core::Log::Error("SelectionOutline: ShaderRegistry not configured.");
                return;
            }

            const auto [vertPath, fragPath] = ResolveShaderPaths(
                *m_ShaderRegistry,
                "Outline.Vert"_id,
                "Outline.Frag"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

            RHI::PipelineBuilder pb(MakeDeviceAlias(m_Device));
            pb.SetShaders(&vert, &frag);
            pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            pb.DisableDepthTest();
            pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            pb.EnableAlphaBlending();
            pb.SetColorFormats({ctx.SwapchainFormat});
            pb.AddDescriptorSetLayout(m_DescriptorSetLayout);

            VkPushConstantRange pcr{};
            pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pcr.offset = 0;
            pcr.size = 144; // 2*vec4(32) + float+uint+uint+uint(16) + 5*float+2*uint(28 → pad to 32) + 16*uint(64) = 144
            pb.AddPushConstantRange(pcr);

            auto built = pb.Build();
            if (built)
            {
                m_Pipeline = std::move(*built);
                m_DebugState.PipelineBuilt = true;
            }
            else
            {
                Core::Log::Error("SelectionOutline: failed to build pipeline (VkResult={})", (int)built.error());
                return;
            }
        }
        else
        {
            m_DebugState.PipelineBuilt = true;
        }

        const RGResourceHandle entityId = ctx.Blackboard.Get(RenderResource::EntityId);
        const RGResourceHandle target = GetPresentationTarget(ctx);
        m_DebugState.LastEntityIdHandleValid = entityId.IsValid();
        m_DebugState.LastTargetHandleValid = target.IsValid();
        m_DebugState.LastEntityIdHandle = entityId.IsValid() ? entityId.ID : 0u;
        m_DebugState.LastTargetHandle = target.IsValid() ? target.ID : 0u;

        if (!entityId.IsValid() || !target.IsValid())
            return;

        // Capture selection state for the execute lambda
        struct SelectionState
        {
            uint32_t SelectedCount;
            uint32_t HoveredId;
            uint32_t SelectedIds[kMaxSelectedIds];
        };

        SelectionState selState{};
        selState.SelectedCount = selectedCount;
        selState.HoveredId = hoveredId;
        std::copy_n(selectedIds, kMaxSelectedIds, selState.SelectedIds);

        ctx.Graph.AddPass<OutlinePassData>("SelectionOutline",
            [&](OutlinePassData& data, RGBuilder& builder)
            {
                data.EntityId = builder.Read(entityId,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                RGAttachmentInfo info{};
                info.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                info.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                data.Target = builder.WriteColor(target, info);

                m_LastPickIdHandle = data.EntityId;
                m_DebugState.LastPassAdded = true;
                m_DebugState.LastEntityIdHandleValid = data.EntityId.IsValid();
                m_DebugState.LastTargetHandleValid = data.Target.IsValid();
                m_DebugState.LastEntityIdHandle = data.EntityId.IsValid() ? data.EntityId.ID : 0u;
                m_DebugState.LastTargetHandle = data.Target.IsValid() ? data.Target.ID : 0u;
            },
            [&, selState, pipeline = m_Pipeline.get()](
                const OutlinePassData& data, const RGRegistry&, VkCommandBuffer cmd)
            {
                if (!pipeline) return;
                if (!data.EntityId.IsValid() || !data.Target.IsValid()) return;
                if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0) return;

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());
                SetViewportScissor(cmd, ctx.Resolution);
                vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

                VkDescriptorSet currentSet = VK_NULL_HANDLE;
                if (ctx.FrameIndex < m_DescriptorSets.size())
                    currentSet = m_DescriptorSets[ctx.FrameIndex];
                if (currentSet == VK_NULL_HANDLE) return;

                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline->GetLayout(),
                    0, 1, &currentSet,
                    0, nullptr);

                // Push constants matching the shader layout
                struct PushConstants
                {
                    glm::vec4 OutlineColor;         // 0
                    glm::vec4 HoverColor;           // 16
                    float OutlineWidth;             // 32
                    uint32_t SelectedCount;         // 36
                    uint32_t HoveredId;             // 40
                    uint32_t OutlineMode;           // 44
                    float SelectionFillAlpha;       // 48
                    float HoverFillAlpha;           // 52
                    float PulsePhase;               // 56
                    float PulseMin;                 // 60
                    float PulseMax;                 // 64
                    float GlowFalloff;              // 68
                    uint32_t _pad0;                 // 72
                    uint32_t _pad1;                 // 76
                    uint32_t SelectedIds[kMaxSelectedIds]; // 80..143
                } push{};
                static_assert(sizeof(PushConstants) == 144);

                push.OutlineColor = m_Settings.SelectionColor;
                push.HoverColor = m_Settings.HoverColor;
                push.OutlineWidth = m_Settings.OutlineWidth;
                push.SelectedCount = selState.SelectedCount;
                push.HoveredId = selState.HoveredId;
                push.OutlineMode = static_cast<uint32_t>(m_Settings.Mode);
                push.SelectionFillAlpha = m_Settings.SelectionFillAlpha;
                push.HoverFillAlpha = m_Settings.HoverFillAlpha;
                push.PulseMin = m_Settings.PulseMin;
                push.PulseMax = m_Settings.PulseMax;
                push.GlowFalloff = m_Settings.GlowFalloff;
                push._pad0 = 0;
                push._pad1 = 0;

                // Compute pulse phase from elapsed time
                {
                    using Clock = std::chrono::steady_clock;
                    static const auto sStart = Clock::now();
                    const float elapsed = std::chrono::duration<float>(Clock::now() - sStart).count();
                    push.PulsePhase = elapsed * m_Settings.PulseSpeed * 6.28318530718f;
                }

                std::copy_n(selState.SelectedIds, kMaxSelectedIds, push.SelectedIds);

                vkCmdPushConstants(cmd, pipeline->GetLayout(),
                    VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push);

                // Fullscreen triangle (3 vertices, no buffers)
                vkCmdDraw(cmd, 3, 1, 0, 0);
            });
    }

    void SelectionOutlinePass::PostCompile(uint32_t frameIndex,
                                           std::span<const RenderGraphDebugImage> debugImages)
    {
        m_DebugState.LastFrameIndex = frameIndex;
        m_DebugState.LastDescriptorPatched = false;
        if (!m_Device) return;
        if (frameIndex >= m_DescriptorSets.size()) return;
        if (!m_LastPickIdHandle.IsValid()) return;

        VkDescriptorSet currentSet = m_DescriptorSets[frameIndex];
        if (!currentSet) return;

        for (const auto& img : debugImages)
        {
            if (img.Resource == m_LastPickIdHandle.ID && img.View != VK_NULL_HANDLE)
            {
                UpdateImageDescriptor(m_Device->GetLogicalDevice(), currentSet, 0,
                                      m_Sampler, img.View,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                m_DebugState.LastDescriptorPatched = true;
                break;
            }
        }
    }

    void SelectionOutlinePass::OnResize(uint32_t width, uint32_t height)
    {
        ++m_DebugState.ResizeCount;
        m_DebugState.LastResizeWidth = width;
        m_DebugState.LastResizeHeight = height;
    }

    void SelectionOutlinePass::Shutdown()
    {
        if (!m_Device) return;
        m_DummyPickId.reset();
        m_DebugState.DummyPickIdAllocated = false;
        m_DebugState.PipelineBuilt = false;
        m_DebugState.Initialized = false;
        if (m_DescriptorSetLayout)
            vkDestroyDescriptorSetLayout(m_Device->GetLogicalDevice(), m_DescriptorSetLayout, nullptr);
        if (m_Sampler)
            vkDestroySampler(m_Device->GetLogicalDevice(), m_Sampler, nullptr);
    }
}
