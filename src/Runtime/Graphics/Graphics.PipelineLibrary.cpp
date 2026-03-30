module;

#include <cstdlib>
#include <string>
#include <optional>
#include <memory>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

module Graphics.PipelineLibrary;

import Graphics.ShaderRegistry;

import Core.Hash;
import Core.Filesystem;
import Core.Logging;
import RHI.Bindless;
import RHI.ComputePipeline;
import RHI.Descriptors;
import RHI.Device;
import RHI.Pipeline;
import RHI.Shader;
import RHI.Types;

using namespace Core::Hash;

namespace Graphics
{
    PipelineLibrary::PipelineLibrary(std::shared_ptr<RHI::VulkanDevice> device,
                                     RHI::BindlessDescriptorSystem& bindless,
                                     RHI::DescriptorLayout& globalSetLayout)
        : m_DeviceOwner(std::move(device)),
          m_Device(m_DeviceOwner.get()),
          m_Bindless(bindless),
          m_GlobalSetLayout(globalSetLayout)
    {
    }

    PipelineLibrary::~PipelineLibrary()
    {
        if (!m_Device) return;

        VkDevice logicalDevice = m_Device->GetLogicalDevice();
        if (logicalDevice == VK_NULL_HANDLE) return;

        // Clear pipelines first (they reference the layouts).
        m_Pipelines.clear();
        m_CullPipeline.reset();
        m_SceneUpdatePipeline.reset();

        // Destroy descriptor set layouts created in BuildDefaults().
        if (m_Stage1InstanceSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(logicalDevice, m_Stage1InstanceSetLayout, nullptr);
            m_Stage1InstanceSetLayout = VK_NULL_HANDLE;
        }

        if (m_CullSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(logicalDevice, m_CullSetLayout, nullptr);
            m_CullSetLayout = VK_NULL_HANDLE;
        }

        if (m_SceneUpdateSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(logicalDevice, m_SceneUpdateSetLayout, nullptr);
            m_SceneUpdateSetLayout = VK_NULL_HANDLE;
        }
    }

    void PipelineLibrary::BuildDefaults(const ShaderRegistry& shaderRegistry,
                                       VkFormat swapchainFormat,
                                       VkFormat depthFormat,
                                       VkFormat sceneColorFormat)
    {
        (void)swapchainFormat;

        // Local resolver helpers — avoid repeating the ShaderRegistry lambda at every call site.
        auto resolver    = [&](Core::Hash::StringID id) { return shaderRegistry.Get(id); };
        auto resolveVF   = [&](Core::Hash::StringID vertId, Core::Hash::StringID fragId)
        {
            return std::pair<std::string, std::string>{
                Core::Filesystem::ResolveShaderPathOrExit(resolver, vertId),
                Core::Filesystem::ResolveShaderPathOrExit(resolver, fragId)
            };
        };
        auto resolveComp = [&](Core::Hash::StringID id)
        {
            return Core::Filesystem::ResolveShaderPathOrExit(resolver, id);
        };

        // ---------------------------------------------------------------------
        // Surface pipeline (Textured + BDA)
        // Renders to HDR SceneColor target (sceneColorFormat), NOT the swapchain.
        // ---------------------------------------------------------------------
        {
            auto [vertPath, fragPath] = resolveVF("Surface.Vert"_id, "Surface.Frag"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

            RHI::VertexInputDescription inputLayout = {}; // Empty input layout due to BDA.
            RHI::PipelineBuilder builder(m_DeviceOwner);
            builder.SetShaders(&vert, &frag);
            builder.SetInputLayout(inputLayout);
            builder.SetColorFormats({sceneColorFormat});
            builder.SetDepthFormat(depthFormat);

            // Depth prepass shares the draw stream: prepass records LESS, raster
            // records EQUAL. Dynamic compare-op avoids duplicate pipeline objects.
            builder.EnableDynamicDepthCompareOp();

            // Current asset winding is CCW in clip space; use CCW front-face.
            // If this ever flips again, revisit the projection Y-flip convention.
            builder.SetCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);

            builder.AddDescriptorSetLayout(m_GlobalSetLayout.GetHandle());
            builder.AddDescriptorSetLayout(m_Bindless.GetLayout());

            // Stage 1: per-frame instance/visibility SSBOs (set = 2).
            // Create this layout once and reuse it (must match the pipeline layout exactly).
            if (m_Stage1InstanceSetLayout == VK_NULL_HANDLE)
            {
                VkDescriptorSetLayoutBinding bindings[2]{};
                bindings[0].binding = 0;
                bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[0].descriptorCount = 1;
                bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

                bindings[1].binding = 1;
                bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[1].descriptorCount = 1;
                bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

                VkDescriptorSetLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                layoutInfo.bindingCount = 2;
                layoutInfo.pBindings = bindings;

                VK_CHECK(vkCreateDescriptorSetLayout(m_Device->GetLogicalDevice(), &layoutInfo, nullptr,
                                                    &m_Stage1InstanceSetLayout));

                // NOTE: Do NOT SafeDestroy() this layout. SafeDestroy schedules deletion a frame later,
                // which will invalidate descriptor allocations while the pipeline (and SurfacePass) are still live.
            }

            builder.AddDescriptorSetLayout(m_Stage1InstanceSetLayout);

            VkPushConstantRange pushConstant{};
            pushConstant.offset = 0;
            pushConstant.size = sizeof(RHI::MeshPushConstants);
            pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            builder.AddPushConstantRange(pushConstant);

            auto pipelineResult = builder.Build();
            if (!pipelineResult)
            {
                Core::Log::Error("Failed to build Surface pipeline: {}", (int)pipelineResult.error());
                std::exit(1);
            }

            m_Pipelines[kPipeline_Surface] = std::move(*pipelineResult);
        }

        // ---------------------------------------------------------------------
        // Surface pipeline variants for Lines and Points.
        //
        // Rationale: VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY is enabled globally in PipelineBuilder,
        // but most drivers run with dynamicPrimitiveTopologyUnrestricted = VK_FALSE.
        // That means vkCmdSetPrimitiveTopology() must stay within the same topology class as
        // the pipeline’s VkPipelineInputAssemblyStateCreateInfo::topology.
        //
        // Our base surface pipeline is created with TRIANGLE_LIST, so using it to draw LINE_LIST
        // or POINT_LIST triggers validation errors and undefined behavior (flicker).
        //
        // Solution: create dedicated surface pipelines with matching base topology.
        // ---------------------------------------------------------------------
        {
            // Lines
            {
                auto [vertPath, fragPath] = resolveVF("Surface.Vert"_id, "Surface.Frag"_id);

                RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
                RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

                RHI::VertexInputDescription inputLayout = {};
                RHI::PipelineBuilder builder(m_DeviceOwner);
                builder.SetShaders(&vert, &frag);
                builder.SetInputLayout(inputLayout);
                builder.SetColorFormats({sceneColorFormat});
                builder.SetDepthFormat(depthFormat);
                builder.SetTopology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
                builder.EnableDynamicDepthCompareOp();

                builder.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
                builder.AddDescriptorSetLayout(m_GlobalSetLayout.GetHandle());
                builder.AddDescriptorSetLayout(m_Bindless.GetLayout());
                builder.AddDescriptorSetLayout(m_Stage1InstanceSetLayout);

                VkPushConstantRange pushConstant{};
                pushConstant.offset = 0;
                pushConstant.size = sizeof(RHI::MeshPushConstants);
                pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                builder.AddPushConstantRange(pushConstant);

                auto pipelineResult = builder.Build();
                if (!pipelineResult)
                {
                    Core::Log::Error("Failed to build SurfaceLines pipeline: {}", (int)pipelineResult.error());
                    std::exit(1);
                }

                m_Pipelines[kPipeline_SurfaceLines] = std::move(*pipelineResult);
            }

            // Points
            {
                auto [vertPath, fragPath] = resolveVF("Surface.Vert"_id, "Surface.Frag"_id);

                RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
                RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

                RHI::VertexInputDescription inputLayout = {};
                RHI::PipelineBuilder builder(m_DeviceOwner);
                builder.SetShaders(&vert, &frag);
                builder.SetInputLayout(inputLayout);
                builder.SetColorFormats({sceneColorFormat});
                builder.SetDepthFormat(depthFormat);
                builder.SetTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
                builder.EnableDynamicDepthCompareOp();

                builder.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
                builder.AddDescriptorSetLayout(m_GlobalSetLayout.GetHandle());
                builder.AddDescriptorSetLayout(m_Bindless.GetLayout());
                builder.AddDescriptorSetLayout(m_Stage1InstanceSetLayout);

                VkPushConstantRange pushConstant{};
                pushConstant.offset = 0;
                pushConstant.size = sizeof(RHI::MeshPushConstants);
                pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                builder.AddPushConstantRange(pushConstant);

                auto pipelineResult = builder.Build();
                if (!pipelineResult)
                {
                    Core::Log::Error("Failed to build SurfacePoints pipeline: {}", (int)pipelineResult.error());
                    std::exit(1);
                }

                m_Pipelines[Graphics::kPipeline_SurfacePoints] = std::move(*pipelineResult);
            }
        }

        // ---------------------------------------------------------------------
        // G-Buffer surface pipeline (deferred path MRT output)
        //
        // Same vertex shader as forward. Fragment shader writes to 3 MRT targets:
        //   location 0 = SceneNormal  (RGBA16F)
        //   location 1 = Albedo       (RGBA8)
        //   location 2 = Material0    (RGBA16F)
        // ---------------------------------------------------------------------
        {
            auto [vertPath, fragPath] = resolveVF("Surface.Vert"_id, "Surface.GBuffer.Frag"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

            RHI::VertexInputDescription inputLayout = {};
            RHI::PipelineBuilder builder(m_DeviceOwner);
            builder.SetShaders(&vert, &frag);
            builder.SetInputLayout(inputLayout);
            builder.SetColorFormats({
                VK_FORMAT_R16G16B16A16_SFLOAT,  // SceneNormal
                VK_FORMAT_R8G8B8A8_UNORM,       // Albedo
                VK_FORMAT_R16G16B16A16_SFLOAT    // Material0
            });
            builder.SetDepthFormat(depthFormat);
            builder.SetCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            builder.EnableDynamicDepthCompareOp();
            builder.AddDescriptorSetLayout(m_GlobalSetLayout.GetHandle());
            builder.AddDescriptorSetLayout(m_Bindless.GetLayout());
            builder.AddDescriptorSetLayout(m_Stage1InstanceSetLayout);

            VkPushConstantRange pushConstant{};
            pushConstant.offset = 0;
            pushConstant.size = sizeof(RHI::MeshPushConstants);
            pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            builder.AddPushConstantRange(pushConstant);

            auto pipelineResult = builder.Build();
            if (!pipelineResult)
            {
                Core::Log::Error("Failed to build SurfaceGBuffer pipeline: {}", (int)pipelineResult.error());
                std::exit(1);
            }

            m_Pipelines[kPipeline_SurfaceGBuffer] = std::move(*pipelineResult);
        }

        // ---------------------------------------------------------------------
        // Debug surface pipeline (transient debug triangles)
        //
        // Lightweight: no instance SSBOs, no bindless textures. Only set=0
        // camera UBO + 24-byte push constants (3 BDA pointers). Alpha blending
        // enabled for semi-transparent fills. No backface culling (double-sided).
        // Depth test on, depth write off (fills don't occlude scene geometry).
        // ---------------------------------------------------------------------
        {
            auto [vertPath, fragPath] = resolveVF("DebugSurface.Vert"_id, "DebugSurface.Frag"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

            RHI::VertexInputDescription inputLayout = {};
            RHI::PipelineBuilder builder(m_DeviceOwner);
            builder.SetShaders(&vert, &frag);
            builder.SetInputLayout(inputLayout);
            builder.SetColorFormats({sceneColorFormat});
            builder.SetDepthFormat(depthFormat);
            builder.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            builder.EnableAlphaBlending();
            builder.EnableDepthTest(false); // depth test on, depth write off
            builder.AddDescriptorSetLayout(m_GlobalSetLayout.GetHandle());

            VkPushConstantRange pushConstant{};
            pushConstant.offset = 0;
            pushConstant.size = 24; // 3 x uint64_t BDA pointers
            pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            builder.AddPushConstantRange(pushConstant);

            auto pipelineResult = builder.Build();
            if (!pipelineResult)
            {
                Core::Log::Error("Failed to build DebugSurface pipeline: {}", (int)pipelineResult.error());
                std::exit(1);
            }

            m_Pipelines[kPipeline_DebugSurface] = std::move(*pipelineResult);
        }

        // ---------------------------------------------------------------------
        // Depth prepass pipeline (depth-only, no fragment shader, no color)
        //
        // Reuses the same vertex shader as the forward surface pass (BDA
        // pull-model). Zero color attachments, depth write enabled with
        // VK_COMPARE_OP_LESS. No fragment invocations — fastest early-Z fill.
        // Same descriptor set layout (global + bindless + instance SSBOs) so
        // the draw stream from SurfacePass is directly consumable.
        // ---------------------------------------------------------------------
        {
            const std::string vertPath = Core::Filesystem::ResolveShaderPathOrExit(resolver, "Surface.Vert"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);

            RHI::VertexInputDescription inputLayout = {};
            RHI::PipelineBuilder builder(m_DeviceOwner);
            builder.SetShaders(&vert, nullptr);
            builder.SetInputLayout(inputLayout);
            builder.SetColorFormats({});
            builder.SetDepthFormat(depthFormat);
            builder.SetCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            builder.EnableDepthTest(true, VK_COMPARE_OP_LESS);

            builder.AddDescriptorSetLayout(m_GlobalSetLayout.GetHandle());
            builder.AddDescriptorSetLayout(m_Bindless.GetLayout());
            builder.AddDescriptorSetLayout(m_Stage1InstanceSetLayout);

            VkPushConstantRange pushConstant{};
            pushConstant.offset = 0;
            pushConstant.size = sizeof(RHI::MeshPushConstants);
            pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            builder.AddPushConstantRange(pushConstant);

            auto pipelineResult = builder.Build();
            if (!pipelineResult)
            {
                Core::Log::Error("Failed to build DepthPrepass pipeline: {}", (int)pipelineResult.error());
                std::exit(1);
            }

            m_Pipelines[kPipeline_DepthPrepass] = std::move(*pipelineResult);
        }

        // ---------------------------------------------------------------------
        // Picking pipeline (ID buffer + BDA)
        // ---------------------------------------------------------------------
        {
            auto [vertPath, fragPath] = resolveVF("Picking.Vert"_id, "Picking.Frag"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

            RHI::VertexInputDescription inputLayout = {};
            RHI::PipelineBuilder builder(m_DeviceOwner);
            builder.SetShaders(&vert, &frag);
            builder.SetInputLayout(inputLayout);
            builder.SetColorFormats({VK_FORMAT_R32_UINT});
            builder.SetDepthFormat(depthFormat);

            // Keep winding convention consistent with the main Surface pass.
            builder.SetCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);

            builder.AddDescriptorSetLayout(m_GlobalSetLayout.GetHandle());

            VkPushConstantRange pushConstant{};
            pushConstant.offset = 0;
            pushConstant.size = sizeof(RHI::MeshPushConstants);
            pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            builder.AddPushConstantRange(pushConstant);

            auto pipelineResult = builder.Build();
            if (!pipelineResult)
            {
                Core::Log::Error("Failed to build Picking pipeline: {}", (int)pipelineResult.error());
                std::exit(1);
            }

            m_Pipelines[kPipeline_Picking] = std::move(*pipelineResult);
        }

        // ---------------------------------------------------------------------
        // MRT Mesh Pick pipeline (EntityID + PrimitiveID, 2× R32_UINT)
        // ---------------------------------------------------------------------
        {
            auto [vertPath, fragPath] = resolveVF("PickMesh.Vert"_id, "PickMesh.Frag"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

            RHI::VertexInputDescription inputLayout = {};
            RHI::PipelineBuilder builder(m_DeviceOwner);
            builder.SetShaders(&vert, &frag);
            builder.SetInputLayout(inputLayout);
            builder.SetColorFormats({VK_FORMAT_R32_UINT, VK_FORMAT_R32_UINT});
            builder.SetDepthFormat(depthFormat);
            builder.SetCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            builder.AddDescriptorSetLayout(m_GlobalSetLayout.GetHandle());

            VkPushConstantRange pushConstant{};
            pushConstant.offset = 0;
            // PickMesh uses the canonical mesh push-constant contract.
            // Keep this aligned with `RHI::MeshPushConstants` to satisfy the
            // shader's declared push-constant block size.
            pushConstant.size = sizeof(RHI::MeshPushConstants);
            pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            builder.AddPushConstantRange(pushConstant);

            auto pipelineResult = builder.Build();
            if (!pipelineResult)
            {
                Core::Log::Error("Failed to build PickMesh pipeline: {}", (int)pipelineResult.error());
                std::exit(1);
            }

            m_Pipelines[kPipeline_PickMesh] = std::move(*pipelineResult);
        }

        // ---------------------------------------------------------------------
        // MRT Line Pick pipeline (vertex-amplified quads, 2× R32_UINT)
        // ---------------------------------------------------------------------
        {
            auto [vertPath, fragPath] = resolveVF("PickLine.Vert"_id, "PickLine.Frag"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

            RHI::VertexInputDescription inputLayout = {};
            RHI::PipelineBuilder builder(m_DeviceOwner);
            builder.SetShaders(&vert, &frag);
            builder.SetInputLayout(inputLayout);
            builder.SetColorFormats({VK_FORMAT_R32_UINT, VK_FORMAT_R32_UINT});
            builder.SetDepthFormat(depthFormat);
            builder.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            builder.EnableDepthBias(-1.0f, -1.0f);
            builder.AddDescriptorSetLayout(m_GlobalSetLayout.GetHandle());

            VkPushConstantRange pushConstant{};
            pushConstant.offset = 0;
            pushConstant.size = 112; // PickMRTPushConsts
            pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            builder.AddPushConstantRange(pushConstant);

            auto pipelineResult = builder.Build();
            if (!pipelineResult)
            {
                Core::Log::Error("Failed to build PickLine pipeline: {}", (int)pipelineResult.error());
                std::exit(1);
            }

            m_Pipelines[kPipeline_PickLine] = std::move(*pipelineResult);
        }

        // ---------------------------------------------------------------------
        // MRT Point Pick pipeline (billboard quads, 2× R32_UINT)
        // ---------------------------------------------------------------------
        {
            auto [vertPath, fragPath] = resolveVF("PickPoint.Vert"_id, "PickPoint.Frag"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

            RHI::VertexInputDescription inputLayout = {};
            RHI::PipelineBuilder builder(m_DeviceOwner);
            builder.SetShaders(&vert, &frag);
            builder.SetInputLayout(inputLayout);
            builder.SetColorFormats({VK_FORMAT_R32_UINT, VK_FORMAT_R32_UINT});
            builder.SetDepthFormat(depthFormat);
            builder.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            builder.EnableDepthBias(-2.0f, -2.0f);
            builder.AddDescriptorSetLayout(m_GlobalSetLayout.GetHandle());

            VkPushConstantRange pushConstant{};
            pushConstant.offset = 0;
            pushConstant.size = 112; // PickMRTPushConsts
            pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            builder.AddPushConstantRange(pushConstant);

            auto pipelineResult = builder.Build();
            if (!pipelineResult)
            {
                Core::Log::Error("Failed to build PickPoint pipeline: {}", (int)pipelineResult.error());
                std::exit(1);
            }

            m_Pipelines[kPipeline_PickPoint] = std::move(*pipelineResult);
        }

        // ---------------------------------------------------------------------
        // GPUScene: Scatter update pipeline
        // ---------------------------------------------------------------------
        {
            if (m_SceneUpdateSetLayout == VK_NULL_HANDLE)
            {
                VkDescriptorSetLayoutBinding bindings[3]{};

                // binding 0: Updates
                bindings[0].binding = 0;
                bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[0].descriptorCount = 1;
                bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 1: Scene instances (SSBO)
                bindings[1].binding = 1;
                bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[1].descriptorCount = 1;
                bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 2: Bounds (SSBO)
                bindings[2].binding = 2;
                bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[2].descriptorCount = 1;
                bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                VkDescriptorSetLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                layoutInfo.bindingCount = 3;
                layoutInfo.pBindings = bindings;

                VK_CHECK(vkCreateDescriptorSetLayout(m_Device->GetLogicalDevice(), &layoutInfo, nullptr, &m_SceneUpdateSetLayout));
            }

            if (!m_SceneUpdatePipeline)
            {
                const std::string compPath = resolveComp("SceneUpdate.Comp"_id);

                RHI::ShaderModule comp(*m_Device, compPath, RHI::ShaderStage::Compute);

                RHI::ComputePipelineBuilder cb(m_DeviceOwner);
                cb.SetShader(&comp);
                cb.AddDescriptorSetLayout(m_SceneUpdateSetLayout);

                VkPushConstantRange pcr{};
                pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                pcr.offset = 0;
                pcr.size = sizeof(uint32_t) * 4; // UpdateCount + padding
                cb.AddPushConstantRange(pcr);

                auto built = cb.Build();
                if (!built)
                {
                    Core::Log::Error("Failed to build SceneUpdate compute pipeline: {}", (int)built.error());
                    std::exit(1);
                }
                m_SceneUpdatePipeline = std::move(*built);
            }
        }

        // ---------------------------------------------------------------------
        // Stage 3: Compute culling pipeline
        // ---------------------------------------------------------------------
        {
            if (m_CullSetLayout == VK_NULL_HANDLE)
            {
                VkDescriptorSetLayoutBinding bindings[7]{};
                // binding 1: Instances
                bindings[0].binding = 1;
                bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[0].descriptorCount = 1;
                bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 2: Bounds
                bindings[1].binding = 2;
                bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[1].descriptorCount = 1;
                bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 3: GeometryIndexCount table
                bindings[2].binding = 3;
                bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[2].descriptorCount = 1;
                bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 4: HandleToDense routing table
                bindings[3].binding = 4;
                bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[3].descriptorCount = 1;
                bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 5: IndirectOut
                bindings[4].binding = 5;
                bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[4].descriptorCount = 1;
                bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 6: VisibilityOut
                bindings[5].binding = 6;
                bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[5].descriptorCount = 1;
                bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 7: DrawCounts
                bindings[6].binding = 7;
                bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[6].descriptorCount = 1;
                bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                VkDescriptorSetLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                layoutInfo.bindingCount = 7;
                layoutInfo.pBindings = bindings;

                VK_CHECK(vkCreateDescriptorSetLayout(m_Device->GetLogicalDevice(), &layoutInfo, nullptr, &m_CullSetLayout));
            }

            if (!m_CullPipeline)
            {
                const std::string compPath = resolveComp("Cull.Comp"_id);

                RHI::ShaderModule comp(*m_Device, compPath, RHI::ShaderStage::Compute);

                RHI::ComputePipelineBuilder cb(m_DeviceOwner);
                cb.SetShader(&comp);
                cb.AddDescriptorSetLayout(m_CullSetLayout);

                VkPushConstantRange pcr{};
                pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                pcr.offset = 0;
                pcr.size = sizeof(glm::vec4) * 6 + sizeof(uint32_t) * 4;
                cb.AddPushConstantRange(pcr);

                auto built = cb.Build();
                if (!built)
                {
                    Core::Log::Error("Failed to build Cull compute pipeline: {}", (int)built.error());
                    std::exit(1);
                }
                m_CullPipeline = std::move(*built);
            }
        }
    }

    std::optional<std::reference_wrapper<RHI::GraphicsPipeline>>
    PipelineLibrary::TryGet(Core::Hash::StringID name)
    {
        auto it = m_Pipelines.find(name);
        if (it == m_Pipelines.end() || !it->second)
            return std::nullopt;
        return *it->second;
    }

    std::optional<std::reference_wrapper<const RHI::GraphicsPipeline>>
    PipelineLibrary::TryGet(Core::Hash::StringID name) const
    {
        auto it = m_Pipelines.find(name);
        if (it == m_Pipelines.end() || !it->second)
            return std::nullopt;
        return *it->second;
    }

    RHI::GraphicsPipeline& PipelineLibrary::GetOrDie(Core::Hash::StringID name)
    {
        auto p = TryGet(name);
        if (!p)
        {
            Core::Log::Error("CRITICAL: Missing pipeline configuration for ID: 0x{:08X}", name.Value);
            std::exit(-1);
        }
        return p->get();
    }

    const RHI::GraphicsPipeline& PipelineLibrary::GetOrDie(Core::Hash::StringID name) const
    {
        auto p = TryGet(name);
        if (!p)
        {
            Core::Log::Error("CRITICAL: Missing pipeline configuration for ID: 0x{:08X}", name.Value);
            std::exit(-1);
        }
        return p->get();
    }
}
