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
                               RHI::DescriptorAllocator& descriptorPool,
                               RHI::DescriptorLayout& descriptorLayout,
                               RHI::GraphicsPipeline& pipeline,
                               RHI::GraphicsPipeline& pickPipeline,
                               Core::Memory::LinearArena& frameArena,
                               Core::Memory::ScopeStack& frameScope,
                               GeometryStorage& geometryStorage)
        : m_Device(device),
          m_Swapchain(swapchain),
          m_Renderer(renderer),
          m_BindlessSystem(bindlessSystem),
          m_Pipeline(pipeline),
          m_PickPipeline(pickPipeline),
          m_RenderGraph(device, frameArena, frameScope),
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
            *device,
            alignedSize * renderer.GetFramesInFlight(),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            static_cast<VmaMemoryUsage>(VMA_MEMORY_USAGE_CPU_TO_GPU)
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

        // GPU picking: one 4-byte host-visible readback buffer per frame-in-flight.
        m_PickReadbackBuffers.resize(renderer.GetFramesInFlight());
        for (auto& buf : m_PickReadbackBuffers)
        {
            buf = std::make_unique<RHI::VulkanBuffer>(
                *device,
                sizeof(uint32_t),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                static_cast<VmaMemoryUsage>(VMA_MEMORY_USAGE_CPU_TO_GPU));
        }

        // -----------------------------------------------------------------
        // Debug-view setup (render-target inspection)
        // -----------------------------------------------------------------
        {
            const uint32_t frames = renderer.GetFramesInFlight();
            m_DebugViewImages.resize(frames);

            // Sampler (nearest is better for IDs)
            VkSamplerCreateInfo samp{};
            samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samp.magFilter = VK_FILTER_NEAREST;
            samp.minFilter = VK_FILTER_NEAREST;
            samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samp.minLod = 0.0f;
            samp.maxLod = 0.0f;
            samp.maxAnisotropy = 1.0f;
            VK_CHECK(vkCreateSampler(m_Device->GetLogicalDevice(), &samp, nullptr, &m_DebugViewSampler));

            // Descriptor set layout: three combined samplers (float/uint/depth)
            VkDescriptorSetLayoutBinding b0{};
            b0.binding = 0;
            b0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b0.descriptorCount = 1;
            b0.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding b1 = b0;
            b1.binding = 1;

            VkDescriptorSetLayoutBinding b2 = b0;
            b2.binding = 2;

            VkDescriptorSetLayoutBinding bindings[] = {b0, b1, b2};
            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 3;
            layoutInfo.pBindings = bindings;
            VK_CHECK(vkCreateDescriptorSetLayout(m_Device->GetLogicalDevice(), &layoutInfo, nullptr, &m_DebugViewSetLayout));

            // Allocate one descriptor set per frame-in-flight
            m_DebugViewSets.resize(renderer.GetFramesInFlight());
            for (auto& set : m_DebugViewSets)
            {
                set = descriptorPool.Allocate(m_DebugViewSetLayout);
            }

            // Create dummy 1x1 textures for all three formats to initialize all descriptor bindings.
            // This avoids validation errors when sampling from unbound/uninitialized descriptors.
            m_DebugViewDummyFloat = std::make_unique<RHI::VulkanImage>(
                *m_Device, 1, 1, 1,
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);

            m_DebugViewDummyUint = std::make_unique<RHI::VulkanImage>(
                *m_Device, 1, 1, 1,
                VK_FORMAT_R32_UINT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);

            m_DebugViewDummyDepth = std::make_unique<RHI::VulkanImage>(
                *m_Device, 1, 1, 1,
                VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT);

            // Transition dummy textures from UNDEFINED to the layouts we'll use for sampling.
            // This is required to avoid validation errors when the shader samples from them.
            {
                VkCommandBuffer cmd = RHI::CommandUtils::BeginSingleTimeCommands(*m_Device);

                // Transition float dummy to SHADER_READ_ONLY_OPTIMAL
                VkImageMemoryBarrier2 floatBarrier{};
                floatBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                floatBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                floatBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                floatBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                floatBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                floatBarrier.image = m_DebugViewDummyFloat->GetHandle();
                floatBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                floatBarrier.subresourceRange.baseMipLevel = 0;
                floatBarrier.subresourceRange.levelCount = 1;
                floatBarrier.subresourceRange.baseArrayLayer = 0;
                floatBarrier.subresourceRange.layerCount = 1;
                floatBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                floatBarrier.srcAccessMask = 0;
                floatBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                floatBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

                // Transition uint dummy to SHADER_READ_ONLY_OPTIMAL
                VkImageMemoryBarrier2 uintBarrier = floatBarrier;
                uintBarrier.image = m_DebugViewDummyUint->GetHandle();

                // Transition depth dummy to DEPTH_STENCIL_READ_ONLY_OPTIMAL
                VkImageMemoryBarrier2 depthBarrier = floatBarrier;
                depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                depthBarrier.image = m_DebugViewDummyDepth->GetHandle();
                depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                depthBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

                VkImageMemoryBarrier2 barriers[] = {floatBarrier, uintBarrier, depthBarrier};
                VkDependencyInfo depInfo{};
                depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                depInfo.imageMemoryBarrierCount = 3;
                depInfo.pImageMemoryBarriers = barriers;
                vkCmdPipelineBarrier2(cmd, &depInfo);

                RHI::CommandUtils::EndSingleTimeCommands(*m_Device, cmd);
            }

            // Initialize ALL descriptor bindings for each set with dummy textures
            // to avoid validation errors from accessing uninitialized descriptors.
            for (auto& set : m_DebugViewSets)
            {
                VkDescriptorImageInfo dummyFloatInfo{m_DebugViewSampler, m_DebugViewDummyFloat->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                VkDescriptorImageInfo dummyUintInfo{m_DebugViewSampler, m_DebugViewDummyUint->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                VkDescriptorImageInfo dummyDepthInfo{m_DebugViewSampler, m_DebugViewDummyDepth->GetView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};

                VkWriteDescriptorSet writes[3] = {};

                writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[0].dstSet = set;
                writes[0].dstBinding = 0;
                writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[0].descriptorCount = 1;
                writes[0].pImageInfo = &dummyFloatInfo;

                writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[1].dstSet = set;
                writes[1].dstBinding = 1;
                writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[1].descriptorCount = 1;
                writes[1].pImageInfo = &dummyUintInfo;

                writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[2].dstSet = set;
                writes[2].dstBinding = 2;
                writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[2].descriptorCount = 1;
                writes[2].pImageInfo = &dummyDepthInfo;

                vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 3, writes, 0, nullptr);
            }

            // Build fullscreen debug pipeline (dynamic rendering)
            // Use the swapchain format since when ShowInViewport is enabled, we render directly to it.
            RHI::ShaderModule vert(*m_Device, Core::Filesystem::GetShaderPath("shaders/debug_view.vert.spv"), RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, Core::Filesystem::GetShaderPath("shaders/debug_view.frag.spv"), RHI::ShaderStage::Fragment);

            RHI::PipelineBuilder pb(m_Device);
            pb.SetShaders(&vert, &frag);
            pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            pb.DisableDepthTest();
            pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            pb.SetColorFormats({m_Swapchain.GetImageFormat()});
            pb.AddDescriptorSetLayout(m_DebugViewSetLayout);

            VkPushConstantRange pcr{};
            pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pcr.offset = 0;
            pcr.size = sizeof(int) + sizeof(float) * 2;
            pb.AddPushConstantRange(pcr);

            auto built = pb.Build();
            if (built)
                m_DebugViewPipeline = std::move(*built);
            else
                Core::Log::Error("DebugView: failed to build pipeline (VkResult={})", (int)built.error());

            // Register UI panel
            Interface::GUI::RegisterPanel("Render Target Viewer",
                [this]()
                {
                    ImGui::Checkbox("Enable Debug View", &m_DebugView.Enabled);

                    if (!m_DebugView.Enabled)
                    {
                        ImGui::TextDisabled("Debug view disabled. Enable to visualize render targets.");
                        return;
                    }

                    ImGui::Checkbox("Show debug view in viewport", &m_DebugView.ShowInViewport);
                    ImGui::Separator();

                    // List passes and their attachments
                    for (const auto& pass : m_LastDebugPasses)
                    {
                        if (!ImGui::TreeNode(pass.Name))
                            continue;

                        for (const auto& att : pass.Attachments)
                        {
                            const bool isSelected = (att.ResourceName == m_DebugView.SelectedResource);
                            // We don't have StringID->string here; show numeric id.
                            char label[128];
                            snprintf(label, sizeof(label), "0x%08X%s", att.ResourceName.Value, att.IsDepth ? " (Depth)" : "");

                            if (ImGui::Selectable(label, isSelected))
                            {
                                m_DebugView.SelectedResource = att.ResourceName;
                                m_DebugView.SelectedResourceId = att.Resource;
                            }
                        }

                        ImGui::TreePop();
                    }

                    ImGui::Separator();
                    ImGui::DragFloat("Depth Near", &m_DebugView.DepthNear, 0.01f, 1e-4f, 10.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::DragFloat("Depth Far", &m_DebugView.DepthFar, 1.0f, 1.0f, 100000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);

                    if (m_DebugViewImGuiTexId)
                    {
                        ImGui::TextUnformatted("Preview (resolved RGBA8)");
                        ImVec2 avail = ImGui::GetContentRegionAvail();
                        float w = avail.x;
                        float h = (w > 0.0f) ? w * 9.0f / 16.0f : 0.0f;
                        ImGui::Image(m_DebugViewImGuiTexId, ImVec2(w, h));
                    }
                    else
                    {
                        ImGui::TextUnformatted("No debug image yet.");
                    }
                },
                true);
        }

        // ...existing code...
    }

    RenderSystem::~RenderSystem()
    {
        if (m_DebugViewImGuiTexId)
        {
            Interface::GUI::RemoveTexture(m_DebugViewImGuiTexId);
            m_DebugViewImGuiTexId = nullptr;
        }

        if (m_DebugViewSetLayout)
        {
            vkDestroyDescriptorSetLayout(m_Device->GetLogicalDevice(), m_DebugViewSetLayout, nullptr);
            m_DebugViewSetLayout = VK_NULL_HANDLE;
        }

        if (m_DebugViewSampler)
        {
            vkDestroySampler(m_Device->GetLogicalDevice(), m_DebugViewSampler, nullptr);
            m_DebugViewSampler = VK_NULL_HANDLE;
        }

        // keep for persistent camera data mapping = nullptr on destruction?
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

    struct PickPassData
    {
        RGResourceHandle IdBuffer;
        RGResourceHandle Depth;
    };

    struct PickCopyPassData
    {
        RGResourceHandle IdBuffer;
    };

    struct DebugViewResolvePassData
    {
        RGResourceHandle Src;
        RGResourceHandle Dst;
        bool IsDepth = false;
        VkFormat SrcFormat = VK_FORMAT_UNDEFINED;
    };

    struct RenderPacket
    {
        GeometryHandle GeoHandle;
        uint32_t TextureID;
        glm::mat4 Transform;
        bool IsSelected = false;

        // Comparison for sorting
        bool operator<(const RenderPacket& other) const
        {
            if (GeoHandle != other.GeoHandle) return GeoHandle < other.GeoHandle;
            if (TextureID != other.TextureID) return TextureID < other.TextureID;
            return IsSelected < other.IsSelected;
        }
    };

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
            RGResourceHandle pickIdHandle{};

            // --- Pick ID pass (offscreen R32_UINT) ---
            m_RenderGraph.AddPass<PickPassData>("PickID",
                                                [&](PickPassData& data, RGBuilder& builder)
                                                {
                                                    // Create/resize the ID buffer
                                                    RGTextureDesc idDesc{};
                                                    idDesc.Width = extent.width;
                                                    idDesc.Height = extent.height;
                                                    idDesc.Format = VK_FORMAT_R32_UINT;
                                                    idDesc.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                        VK_IMAGE_USAGE_SAMPLED_BIT; // needed for debug visualization
                                                    idDesc.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;

                                                    auto idTex = builder.CreateTexture("PickID"_id, idDesc);

                                                    // Reuse the same depth buffer as forward pass.
                                                    auto& depthImg = m_DepthImages[frameIndex];
                                                    if (!depthImg || depthImg->GetWidth() != extent.width ||
                                                        depthImg->GetHeight() != extent.height)
                                                    {
                                                        VkFormat depthFormat = RHI::VulkanImage::FindDepthFormat(*m_Device);
                                                        depthImg = std::make_unique<RHI::VulkanImage>(
                                                            *m_Device, extent.width, extent.height, 1,
                                                            depthFormat,
                                                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                            VK_IMAGE_USAGE_SAMPLED_BIT,
                                                            VK_IMAGE_ASPECT_DEPTH_BIT
                                                        );
                                                    }

                                                    auto depth = builder.ImportTexture(
                                                        "DepthBuffer"_id, depthImg->GetHandle(), depthImg->GetView(),
                                                        depthImg->GetFormat(), extent,
                                                        VK_IMAGE_LAYOUT_UNDEFINED);

                                                    RGAttachmentInfo idInfo{};
                                                    idInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                                    idInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                                                    idInfo.ClearValue.color.uint32[0] = 0u; // 0 means 'no entity'

                                                    RGAttachmentInfo depthInfo{};
                                                    depthInfo.ClearValue.depthStencil = {1.0f, 0};

                                                    data.IdBuffer = builder.WriteColor(idTex, idInfo);
                                                    data.Depth = builder.WriteDepth(depth, depthInfo);

                                                    pickIdHandle = data.IdBuffer;
                                                },
                                                [this,
                                                    pipeline = &m_PickPipeline,
                                                    geoStorage = &m_GeometryStorage,
                                                    scenePtr = &scene,
                                                    extent,
                                                    offset](const PickPassData&, const RGRegistry&, VkCommandBuffer cmd)
                                                {
                                                    // Bind pick pipeline
                                                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());

                                                    // Viewport
                                                    VkViewport viewport{};
                                                    viewport.x = 0.0f;
                                                    viewport.y = 0.0f;
                                                    viewport.width = static_cast<float>(extent.width);
                                                    viewport.height = static_cast<float>(extent.height);
                                                    viewport.minDepth = 0.0f;
                                                    viewport.maxDepth = 1.0f;
                                                    VkRect2D scissor{{0, 0}, extent};
                                                    vkCmdSetViewport(cmd, 0, 1, &viewport);
                                                    vkCmdSetScissor(cmd, 0, 1, &scissor);

                                                    // Bind camera set 0
                                                    uint32_t dynamicOffset = static_cast<uint32_t>(offset);
                                                    vkCmdBindDescriptorSets(
                                                        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                        pipeline->GetLayout(),
                                                        0, 1, &m_GlobalDescriptorSet,
                                                        1, &dynamicOffset);

                                                    // Draw every mesh with entity id push constant
                                                    struct PickPush
                                                    {
                                                        glm::mat4 Model;
                                                        uint32_t EntityID;
                                                    };

                                                    auto view = scenePtr->GetRegistry().view<
                                                        ECS::Components::Transform::Component,
                                                        ECS::MeshRenderer::Component>();

                                                    for (auto [entity, transform, renderable] : view.each())
                                                    {
                                                        if (!renderable.Geometry.IsValid()) continue;

                                                        // Resolve geometry
                                                        auto* geo = geoStorage->GetUnchecked(renderable.Geometry);
                                                        if (!geo) continue;

                                                        // World matrix
                                                        glm::mat4 worldMatrix;
                                                        if (auto* world = scenePtr->GetRegistry().try_get<
                                                            ECS::Components::Transform::WorldMatrix>(entity))
                                                            worldMatrix = world->Matrix;
                                                        else
                                                            worldMatrix = GetMatrix(transform);

                                                        // Bind VB/IB - must bind all 3 vertex buffers to match pipeline layout
                                                        auto* vBuf = geo->GetVertexBuffer()->GetHandle();
                                                        const auto& layout = geo->GetLayout();
                                                        VkBuffer vBuffers[] = {vBuf, vBuf, vBuf};
                                                        VkDeviceSize offsets[] = {
                                                            layout.PositionsOffset,
                                                            layout.NormalsOffset,
                                                            layout.AuxOffset
                                                        };
                                                        vkCmdBindVertexBuffers(cmd, 0, 3, vBuffers, offsets);

                                                        if (geo->GetIndexCount() > 0)
                                                        {
                                                            vkCmdBindIndexBuffer(cmd, geo->GetIndexBuffer()->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
                                                        }

                                                        // Set primitive topology (required for dynamic state)
                                                        VkPrimitiveTopology vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                                                        switch (geo->GetTopology())
                                                        {
                                                        case PrimitiveTopology::Points:
                                                            vkTopo = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                                                            break;
                                                        case PrimitiveTopology::Lines:
                                                            vkTopo = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                                                            break;
                                                        case PrimitiveTopology::Triangles:
                                                        default:
                                                            vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                                                            break;
                                                        }
                                                        vkCmdSetPrimitiveTopology(cmd, vkTopo);

                                                        // Push constants
                                                        const uint32_t id = static_cast<uint32_t>(static_cast<entt::id_type>(entity));
                                                        PickPush push{worldMatrix, id};
                                                        vkCmdPushConstants(cmd, pipeline->GetLayout(),
                                                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                                           0, sizeof(PickPush), &push);

                                                        if (geo->GetIndexCount() > 0)
                                                        {
                                                            vkCmdDrawIndexed(cmd, geo->GetIndexCount(), 1, 0, 0, 0);
                                                        }
                                                        else
                                                        {
                                                            auto vertCount = static_cast<uint32_t>(geo->GetLayout().PositionsSize / sizeof(glm::vec3));
                                                            vkCmdDraw(cmd, vertCount, 1, 0, 0);
                                                        }
                                                    }
                                                });

            // --- Pick copy pass (TRANSFER) ---
            m_RenderGraph.AddPass<PickCopyPassData>("PickCopy",
                                                    [&](PickCopyPassData& data, RGBuilder& builder)
                                                    {
                                                        // Only execute copy if a pick is pending.
                                                        if (!m_PendingPick.Pending)
                                                            return;

                                                        // Mark that we will read the ID buffer as a transfer source.
                                                        data.IdBuffer = builder.Read(pickIdHandle,
                                                                                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                                                    VK_ACCESS_2_TRANSFER_READ_BIT);
                                                    },
                                                    [this,
                                                        extent](const PickCopyPassData& data, const RGRegistry& reg,
                                                                VkCommandBuffer cmd)
                                                    {
                                                        if (!m_PendingPick.Pending)
                                                            return;

                                                        const uint32_t x = m_PendingPick.X;
                                                        const uint32_t y = m_PendingPick.Y;
                                                        const uint32_t frame = m_Renderer.GetCurrentFrameIndex();
                                                        VkBuffer dst = m_PickReadbackBuffers[frame]->GetHandle();

                                                        VkImage img = reg.GetImage(data.IdBuffer);

                                                        // At this point we're outside dynamic rendering, and RenderGraph
                                                        // has already transitioned IdBuffer into TRANSFER_SRC_OPTIMAL.
                                                        m_Renderer.CopyPixel_R32_UINT_ToBuffer(img, extent.width, extent.height, x, y, dst);
                                                    });

            // --- Forward pass (existing) ---
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
                                                               *m_Device, extent.width, extent.height, 1,
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
                                                   [this,
                                                       renderer = &m_Renderer,
                                                       pipeline = &m_Pipeline,
                                                       bindless = &m_BindlessSystem,
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
                                                           0, 1, &m_GlobalDescriptorSet,
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
                                                           if (!renderable.Geometry.IsValid() || !renderable.Material.IsValid())
                                                               continue;

                                                           // --- CACHE RESOLUTION START ---
                                                           // Only query AssetManager if the material handle changed or cache is uninitialized.
                                                           if (renderable.Material != renderable.CachedMaterialHandle ||
                                                               renderable.TextureID_Cache == ~0u)
                                                           {
                                                               auto* mat = assets->TryGet<Material>(renderable.Material);
                                                               if (mat)
                                                               {
                                                                   renderable.TextureID_Cache = mat->GetTextureIndex();
                                                                   renderable.CachedMaterialHandle = renderable.Material;
                                                               }
                                                               else
                                                               {
                                                                   // Material not ready yet (or wrong type). Skip for now.
                                                                   continue;
                                                               }
                                                           }
                                                           // --- CACHE RESOLUTION END ---

                                                           const uint32_t textureID = renderable.TextureID_Cache;

                                                           const bool isSelected = scenePtr->GetRegistry().all_of<ECS::Components::Selection::SelectedTag>(entity);

                                                           glm::mat4 worldMatrix;
                                                           if (auto* world = scenePtr->GetRegistry().try_get<
                                                               ECS::Components::Transform::WorldMatrix>(entity))
                                                           {
                                                               worldMatrix = world->Matrix;
                                                           }
                                                           else
                                                           {
                                                               // Fallback for non-hierarchical entities
                                                               worldMatrix = GetMatrix(transform);
                                                           }

                                                           packets.push_back({
                                                               renderable.Geometry,
                                                               textureID,
                                                               worldMatrix,
                                                               isSelected
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

                                                           // Selection highlight: tint selected entities toward orange.
                                                           // This is a cheap immediate-mode editor feature.
                                                           // (Implemented in the triangle.frag shader via TextureID high bit.)
                                                           if (packet.IsSelected)
                                                           {
                                                               push.TextureID = packet.TextureID | 0x80000000u;
                                                           }

                                                           vkCmdPushConstants(cmd, pipeline->GetLayout(),
                                                                              VK_SHADER_STAGE_VERTEX_BIT |
                                                                              VK_SHADER_STAGE_FRAGMENT_BIT,
                                                                              0, sizeof(push), &push);

                                                           // Issue Draw
                                                           if (currentGeo->GetIndexCount() > 0)
                                                           {
                                                               vkCmdDrawIndexed(
                                                                   cmd, currentGeo->GetIndexCount(), 1, 0, 0, 0);
                                                               // Record telemetry for draw call
                                                               Core::Telemetry::TelemetrySystem::Get().RecordDrawCall(
                                                                   currentGeo->GetIndexCount() / 3);
                                                           }
                                                           else
                                                           {
                                                               // Point Cloud
                                                               uint32_t vertCount = static_cast<uint32_t>(currentGeo->
                                                                   GetLayout().PositionsSize / sizeof(glm::vec3));
                                                               vkCmdDraw(cmd, vertCount, 1, 0, 0);
                                                               Core::Telemetry::TelemetrySystem::Get().
                                                                   RecordDrawCall(0);
                                                           }
                                                       }
                                                   }
            );

            // -----------------------------------------------------------------
            // Debug view resolve (optional): convert any chosen buffer into RGBA8.
            // When ShowInViewport is enabled, this pass runs BEFORE ImGui so the UI
            // can be rendered on top. Otherwise, it runs after and outputs to a
            // separate debug image for preview in the UI panel.
            // -----------------------------------------------------------------
            RGResourceHandle debugSrcHandle{}; // Will be set if we find a valid sampleable source

            // Lambda to add the debug view resolve pass
            auto addDebugViewResolvePass = [&, this]()
            {
                m_RenderGraph.AddPass<DebugViewResolvePassData>("DebugViewResolve",
                    [&, this](DebugViewResolvePassData& data, RGBuilder& builder)
                    {
                        if (!m_DebugView.Enabled)
                            return;

                        // On the first frame m_LastDebugImages is empty; skip resolve entirely.
                        // The lists are populated at the end of the frame, so on subsequent frames
                        // we can find valid sampleable sources by name.
                        if (m_LastDebugImages.empty())
                            return;

                        // Find the selected resource by NAME in the *previous* frame's debug list.
                        // We use the name to look up in the *current* frame's RenderGraph by recreating
                        // the texture with the same name (RenderGraph deduplicates by name).
                        const RenderGraphDebugImage* srcInfo = nullptr;
                        for (const auto& img : m_LastDebugImages)
                        {
                            if (img.Name == m_DebugView.SelectedResource &&
                                (img.Usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0)
                            {
                                srcInfo = &img;
                                break;
                            }
                        }

                        // Fallback to PickID if selected resource isn't sampleable
                        if (!srcInfo)
                        {
                            for (const auto& img : m_LastDebugImages)
                            {
                                if (img.Name == "PickID"_id && (img.Usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0)
                                {
                                    srcInfo = &img;
                                    m_DebugView.SelectedResource = img.Name;
                                    break;
                                }
                            }
                        }

                        // Still nothing? Skip this frame.
                        if (!srcInfo)
                            return;

                        // Now we need to find/create this resource in the CURRENT frame's graph.
                        // For transient resources like PickID, they're created by earlier passes
                        // with the same name, so we can use CreateTexture to get a handle
                        // (it will return the existing resource if already created this frame).
                        // For imported resources, we can't easily re-import them here without
                        // the original VkImage/VkImageView. So we only support transient resources
                        // for debug visualization for now.

                        // We'll use a simple approach: Read from the resource by looking it up
                        // via CreateTexture (which deduplicates by name). This works for transient
                        // textures created earlier in the frame (like PickID).
                        RGTextureDesc srcDesc{};
                        srcDesc.Width = srcInfo->Extent.width;
                        srcDesc.Height = srcInfo->Extent.height;
                        srcDesc.Format = srcInfo->Format;
                        srcDesc.Usage = srcInfo->Usage;
                        srcDesc.Aspect = srcInfo->Aspect;

                        auto srcHandle = builder.CreateTexture(srcInfo->Name, srcDesc);
                        if (!srcHandle.IsValid())
                            return;

                        // Destination: either backbuffer (viewport override) or per-frame debug RGBA image.
                        if (m_DebugView.ShowInViewport)
                        {
                            RGAttachmentInfo colorInfo{};
                            colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                            colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                            data.Dst = builder.WriteColor(backbufferHandle, colorInfo);
                        }
                        else
                        {
                            auto& dbgImg = m_DebugViewImages[frameIndex];
                            if (!dbgImg || dbgImg->GetWidth() != extent.width || dbgImg->GetHeight() != extent.height)
                            {
                                // Use the swapchain format to match the debug view pipeline
                                dbgImg = std::make_unique<RHI::VulkanImage>(
                                    *m_Device,
                                    extent.width,
                                    extent.height,
                                    1,
                                    m_Swapchain.GetImageFormat(),
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                    VK_IMAGE_ASPECT_COLOR_BIT);
                            }

                            auto dst = builder.ImportTexture(
                                "DebugViewRGBA"_id,
                                dbgImg->GetHandle(),
                                dbgImg->GetView(),
                                dbgImg->GetFormat(),
                                extent,
                                VK_IMAGE_LAYOUT_UNDEFINED);

                            RGAttachmentInfo info{};
                            info.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                            info.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                            data.Dst = builder.WriteColor(dst, info);
                        }

                        // Read the source for sampling
                        data.Src = builder.Read(srcHandle,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                        data.SrcFormat = srcInfo->Format;
                        data.IsDepth = (srcInfo->Aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;

                        // Store for descriptor update after compile
                        debugSrcHandle = data.Src;
                    },
                    [this, extent, frameIndex](const DebugViewResolvePassData& data, const RGRegistry& reg, VkCommandBuffer cmd)
                    {
                        (void)reg;
                        if (!m_DebugView.Enabled) return;
                        if (!m_DebugViewPipeline) return;
                        if (!data.Dst.IsValid()) return;
                        if (!data.Src.IsValid()) return;

                        // Bind pipeline
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_DebugViewPipeline->GetHandle());

                        VkViewport vp{};
                        vp.x = 0.0f;
                        vp.y = 0.0f;
                        vp.width = (float)extent.width;
                        vp.height = (float)extent.height;
                        vp.minDepth = 0.0f;
                        vp.maxDepth = 1.0f;
                        VkRect2D sc{{0, 0}, extent};
                        vkCmdSetViewport(cmd, 0, 1, &vp);
                        vkCmdSetScissor(cmd, 0, 1, &sc);

                        // Set primitive topology (required for dynamic state)
                        vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

                        // Use per-frame descriptor set to avoid in-use validation errors
                        // Bounds check frameIndex to avoid out-of-range access
                        if (frameIndex >= m_DebugViewSets.size())
                        {
                            Core::Log::Error("DebugView: frameIndex {} out of range (size={})", frameIndex, m_DebugViewSets.size());
                            return;
                        }
                        VkDescriptorSet currentSet = m_DebugViewSets[frameIndex];
                        if (currentSet == VK_NULL_HANDLE)
                        {
                            Core::Log::Warn("DebugView: descriptor set for frame {} is null, skipping draw", frameIndex);
                            return;
                        }
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_DebugViewPipeline->GetLayout(),
                            0, 1, &currentSet,
                            0, nullptr);

                        struct Push
                        {
                            int Mode;
                            float DepthNear;
                            float DepthFar;
                        } push{};

                        push.DepthNear = m_DebugView.DepthNear;
                        push.DepthFar = m_DebugView.DepthFar;

                        // Determine mode from pass data
                        if (data.IsDepth)
                            push.Mode = 2;
                        else if (data.SrcFormat == VK_FORMAT_R32_UINT)
                            push.Mode = 1;
                        else
                            push.Mode = 0;

                        vkCmdPushConstants(cmd, m_DebugViewPipeline->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &push);

                        vkCmdDraw(cmd, 3, 1, 0, 0);
                    });
            };

            // If ShowInViewport is enabled, add the debug view pass BEFORE ImGui
            // so that the UI is rendered on top of the debug view.
            if (m_DebugView.Enabled && m_DebugView.ShowInViewport)
            {
                addDebugViewResolvePass();
            }

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

            // If ShowInViewport is NOT enabled but debug IS enabled, add the pass
            // after ImGui to render to the separate debug image for preview.
            if (m_DebugView.Enabled && !m_DebugView.ShowInViewport)
            {
                addDebugViewResolvePass();
            }


            m_RenderGraph.Compile(frameIndex);

            // Refresh debug lists for UI *after* compile (physical views are resolved).
            m_LastDebugPasses = m_RenderGraph.BuildDebugPassList();
            m_LastDebugImages = m_RenderGraph.BuildDebugImageList();

            // Update debug descriptor set ONLY if we have a valid source handle from this frame
            // Use per-frame descriptor set to avoid in-use validation errors
            VkDescriptorSet currentDebugSet = (frameIndex < m_DebugViewSets.size()) ? m_DebugViewSets[frameIndex] : VK_NULL_HANDLE;

            if (debugSrcHandle.IsValid() && currentDebugSet != VK_NULL_HANDLE)
            {
                // Find the corresponding debug image to get the view
                for (const auto& img : m_LastDebugImages)
                {
                    if (img.Resource == debugSrcHandle.ID && img.View != VK_NULL_HANDLE)
                    {
                        VkDescriptorImageInfo imageInfo{m_DebugViewSampler, img.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

                        // Determine which binding to update based on the format
                        // Binding 0: uSrcFloat (sampler2D) - for float textures
                        // Binding 1: uSrcUint (usampler2D) - for uint textures (R32_UINT)
                        // Binding 2: uSrcDepth (sampler2D) - for depth textures
                        uint32_t targetBinding = 0; // default to float
                        if (img.Format == VK_FORMAT_R32_UINT)
                        {
                            targetBinding = 1; // uint
                        }
                        else if ((img.Aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0)
                        {
                            targetBinding = 2; // depth
                        }

                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = currentDebugSet;
                        write.dstBinding = targetBinding;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        write.descriptorCount = 1;
                        write.pImageInfo = &imageInfo;

                        vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 1, &write, 0, nullptr);
                        break;
                    }
                }
            }

            // Update ImGui texture binding for preview (points at the per-frame debug image view).
            if (m_DebugViewImGuiTexId)
            {
                Interface::GUI::RemoveTexture(m_DebugViewImGuiTexId);
                m_DebugViewImGuiTexId = nullptr;
            }
            if (!m_DebugView.ShowInViewport && m_DebugViewImages[frameIndex])
            {
                m_DebugViewImGuiTexId = Interface::GUI::AddTexture(m_DebugViewSampler,
                    m_DebugViewImages[frameIndex]->GetView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }

            m_RenderGraph.Execute(m_Renderer.GetCommandBuffer());
            m_Renderer.EndFrame();
        }
        else
        {
            Interface::GUI::EndFrame();
        }

        // NOTE: pick readback resolve happens above inside the IsFrameInProgress() block.
    }

    void RenderSystem::OnResize()
    {
        // Swapchain recreation already waited for idle in SimpleRenderer::OnResize().
        // Trim transient caches so old-extent resources don't accumulate.
        m_RenderGraph.Trim();

        // Depth images are sized to the swapchain extent; force them to be recreated.
        for (auto& img : m_DepthImages)
        {
            img.reset();
        }
        for (auto& img : m_DebugViewImages)
        {
            img.reset();
        }
    }
}
