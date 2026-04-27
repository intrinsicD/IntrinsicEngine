module;

#include <cstdint>
#include <chrono>
#include <memory>
#include <optional>
#include <vector>
#include <entt/entity/registry.hpp>

module Extrinsic.Graphics.Renderer;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.Types;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.VisualizationSyncSystem;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Graphics.ForwardSystem;
import Extrinsic.Graphics.DeferredSystem;
import Extrinsic.Graphics.PostProcessSystem;
import Extrinsic.Graphics.ShadowSystem;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Logging;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] RHI::TextureLayout ToTextureLayout(const TextureBarrierState state)
        {
            switch (state)
            {
            case TextureBarrierState::Undefined:           return RHI::TextureLayout::Undefined;
            case TextureBarrierState::ColorAttachmentWrite: return RHI::TextureLayout::ColorAttachment;
            case TextureBarrierState::DepthWrite:           return RHI::TextureLayout::DepthAttachment;
            case TextureBarrierState::DepthRead:            return RHI::TextureLayout::DepthReadOnly;
            case TextureBarrierState::ShaderRead:           return RHI::TextureLayout::ShaderReadOnly;
            case TextureBarrierState::ShaderWrite:          return RHI::TextureLayout::General;
            case TextureBarrierState::TransferSrc:          return RHI::TextureLayout::TransferSrc;
            case TextureBarrierState::TransferDst:          return RHI::TextureLayout::TransferDst;
            case TextureBarrierState::Present:              return RHI::TextureLayout::Present;
            }
            return RHI::TextureLayout::Undefined;
        }

        [[nodiscard]] RHI::MemoryAccess ToMemoryAccess(const BufferBarrierState state)
        {
            switch (state)
            {
            case BufferBarrierState::Undefined:     return RHI::MemoryAccess::None;
            case BufferBarrierState::IndirectRead:  return RHI::MemoryAccess::IndirectRead;
            case BufferBarrierState::IndexRead:     return RHI::MemoryAccess::IndexRead;
            case BufferBarrierState::VertexRead:    return RHI::MemoryAccess::ShaderRead;
            case BufferBarrierState::ShaderRead:    return RHI::MemoryAccess::ShaderRead;
            case BufferBarrierState::ShaderWrite:   return RHI::MemoryAccess::ShaderWrite;
            case BufferBarrierState::TransferSrc:    return RHI::MemoryAccess::TransferRead;
            case BufferBarrierState::TransferDst:    return RHI::MemoryAccess::TransferWrite;
            case BufferBarrierState::HostReadback:   return RHI::MemoryAccess::HostRead;
            }
            return RHI::MemoryAccess::None;
        }

        void SubmitBarrierPacket(RHI::ICommandContext& cmd,
                                 const CompiledRenderGraph& graph,
                                 const BarrierPacket& packet)
        {
            std::vector<RHI::TextureBarrierDesc> textureBarriers;
            textureBarriers.reserve(packet.TextureBarriers.size());
            for (const TextureBarrierPacket& barrier : packet.TextureBarriers)
            {
                textureBarriers.push_back(RHI::TextureBarrierDesc{
                    .Texture = graph.TextureHandles[barrier.TextureIndex],
                    .BeforeLayout = ToTextureLayout(barrier.Before),
                    .AfterLayout = ToTextureLayout(barrier.After),
                });
            }

            std::vector<RHI::BufferBarrierDesc> bufferBarriers;
            bufferBarriers.reserve(packet.BufferBarriers.size());
            for (const BufferBarrierPacket& barrier : packet.BufferBarriers)
            {
                bufferBarriers.push_back(RHI::BufferBarrierDesc{
                    .Buffer = graph.BufferHandles[barrier.BufferIndex],
                    .BeforeAccess = ToMemoryAccess(barrier.Before),
                    .AfterAccess = ToMemoryAccess(barrier.After),
                });
            }

            if (textureBarriers.empty() && bufferBarriers.empty())
            {
                return;
            }

            cmd.SubmitBarriers(RHI::BarrierBatchDesc{
                .TextureBarriers = textureBarriers,
                .BufferBarriers = bufferBarriers,
            });
        }

        struct PrepPipelineCommitTag {};
        struct PrepMaterialBaseSyncTag {};
        struct PrepVisualizationSyncTag {};
        struct PrepMaterialOverrideSyncTag {};
        struct PrepTransformSyncTag {};
        struct PrepLightSyncTag {};
        struct PrepGpuWorldSyncTag {};
    }

    class NullRenderer final : public IRenderer
    {
    public:
        void Initialize(RHI::IDevice& device) override
        {
            m_Device = &device;
            m_BufferManager  .emplace(device);
            m_SamplerManager .emplace(device);
            m_TextureManager .emplace(device, device.GetBindlessHeap());
            m_PipelineManager.emplace(device);
            m_GpuWorld.emplace();
            m_GpuWorld->Initialize(device, *m_BufferManager);
            m_MaterialSystem .emplace();
            m_MaterialSystem->Initialize(device, *m_BufferManager);
            m_ColormapSystem.emplace();
            m_ColormapSystem->Initialize(device, *m_TextureManager, *m_SamplerManager);
            m_VisualizationSyncSystem.emplace();
            m_VisualizationSyncSystem->Initialize(*m_MaterialSystem, device);
            m_TransformSyncSystem.emplace();
            m_TransformSyncSystem->Initialize();
            m_GpuWorld->SetMaterialBuffer(
                m_MaterialSystem->GetBuffer(),
                m_MaterialSystem->GetCapacity());
            m_CullingSystem  .emplace();
            m_LightSystem    .emplace();
            m_LightSystem->Initialize();
            m_SelectionSystem.emplace();
            m_SelectionSystem->Initialize();
            m_ForwardSystem.emplace();
            m_ForwardSystem->Initialize();
            m_DeferredSystem.emplace();
            m_DeferredSystem->Initialize();
            m_PostProcessSystem.emplace();
            m_PostProcessSystem->Initialize();
            m_ShadowSystem.emplace();
            m_ShadowSystem->Initialize();
            // CullingSystem::Initialize requires a shader path — concrete
            // renderers supply it.  NullRenderer skips the cull dispatch.
        }

        void Shutdown() override
        {
            m_Device = nullptr;
            if (m_SelectionSystem) m_SelectionSystem->Shutdown();
            if (m_LightSystem)     m_LightSystem->Shutdown();
            if (m_ForwardSystem)   m_ForwardSystem->Shutdown();
            if (m_DeferredSystem)  m_DeferredSystem->Shutdown();
            if (m_PostProcessSystem) m_PostProcessSystem->Shutdown();
            if (m_ShadowSystem)    m_ShadowSystem->Shutdown();
            if (m_CullingSystem)   m_CullingSystem->Shutdown();
            if (m_TransformSyncSystem) m_TransformSyncSystem->Shutdown();
            if (m_VisualizationSyncSystem) m_VisualizationSyncSystem->Shutdown();
            if (m_ColormapSystem)  m_ColormapSystem->Shutdown();
            if (m_GpuWorld)        m_GpuWorld->Shutdown();
            if (m_MaterialSystem)  m_MaterialSystem->Shutdown();

            m_SelectionSystem.reset();
            m_LightSystem    .reset();
            m_ForwardSystem  .reset();
            m_DeferredSystem .reset();
            m_PostProcessSystem.reset();
            m_ShadowSystem   .reset();
            m_CullingSystem  .reset();
            m_TransformSyncSystem.reset();
            m_VisualizationSyncSystem.reset();
            m_ColormapSystem .reset();
            m_GpuWorld       .reset();
            m_MaterialSystem .reset();
            m_PipelineManager.reset();
            m_TextureManager .reset();
            m_SamplerManager .reset();
            m_BufferManager  .reset();
        }

        void Resize(std::uint32_t, std::uint32_t) override
        {
            m_RenderGraph.Reset();
        }

        // ── Per-frame phases ──────────────────────────────────────────────

        bool BeginFrame(RHI::FrameHandle&) override
        {
            // NullRenderer has no swapchain; always reports "frame available"
            // so the rest of the loop exercises the extraction/prepare/execute
            // seams even without a real GPU backend.
            m_HasExtractedRenderWorld = false;
            m_HasPreparedFrame = false;
            return true;
        }

        RenderWorld ExtractRenderWorld(const RenderFrameInput& input) override
        {
            m_HasExtractedRenderWorld = true;
            m_HasPreparedFrame = false;
            return RenderWorld{
                .Viewport       = input.Viewport,
                .Alpha          = input.Alpha,
                .HasPendingPick = input.HasPendingPick,
            };
        }

        void PrepareFrame(RenderWorld&) override
        {
            if (!m_HasExtractedRenderWorld)
            {
                Core::Log::Warn("[Graphics] PrepareFrame called before ExtractRenderWorld");
                m_HasPreparedFrame = false;
                return;
            }

            // Phase 14.1 sync order contract:
            //  1) commit pipelines
            //  2) flush base materials
            //  3) resolve visualization overrides + write GpuEntityConfig
            //  4) flush override material deltas
            //  5) write instance transforms/material slots/flags/bounds
            //  6) write lights
            //  7) refresh scene table material binding
            //  8) upload GpuWorld dirty ranges
            if (m_EnableRenderPrepTaskGraph)
            {
                m_RenderPrepGraph.Reset();
                m_RenderPrepGraph.AddPass(
                    "RenderPrep.PipelineCommit",
                    [] (Core::Dag::TaskGraphBuilder& b)
                    {
                        b.Write<PrepPipelineCommitTag>();
                    },
                    [this]
                    {
                        m_PipelineManager->CommitPending();
                    });
                m_RenderPrepGraph.AddPass(
                    "RenderPrep.MaterialBaseSync",
                    [] (Core::Dag::TaskGraphBuilder& b)
                    {
                        b.Read<PrepPipelineCommitTag>();
                        b.Write<PrepMaterialBaseSyncTag>();
                    },
                    [this]
                    {
                        m_MaterialSystem->SyncGpuBuffer();
                    });
                m_RenderPrepGraph.AddPass(
                    "RenderPrep.VisualizationSync",
                    [] (Core::Dag::TaskGraphBuilder& b)
                    {
                        b.Read<PrepMaterialBaseSyncTag>();
                        b.Write<PrepVisualizationSyncTag>();
                    },
                    [this]
                    {
                        m_VisualizationSyncSystem->Sync(
                            m_SyncRegistry,
                            *m_MaterialSystem,
                            *m_ColormapSystem,
                            *m_GpuWorld);
                    });
                m_RenderPrepGraph.AddPass(
                    "RenderPrep.MaterialOverrideSync",
                    [] (Core::Dag::TaskGraphBuilder& b)
                    {
                        b.Read<PrepVisualizationSyncTag>();
                        b.Write<PrepMaterialOverrideSyncTag>();
                    },
                    [this]
                    {
                        m_MaterialSystem->SyncGpuBuffer();
                    });
                m_RenderPrepGraph.AddPass(
                    "RenderPrep.TransformSync",
                    [] (Core::Dag::TaskGraphBuilder& b)
                    {
                        b.Read<PrepMaterialOverrideSyncTag>();
                        b.Write<PrepTransformSyncTag>();
                    },
                    [this]
                    {
                        m_TransformSyncSystem->SyncGpuBuffer(m_SyncRegistry, *m_GpuWorld, *m_MaterialSystem);
                    });
                m_RenderPrepGraph.AddPass(
                    "RenderPrep.LightSync",
                    [] (Core::Dag::TaskGraphBuilder& b)
                    {
                        b.Read<PrepTransformSyncTag>();
                        b.Write<PrepLightSyncTag>();
                    },
                    [this]
                    {
                        m_LightSystem->SyncGpuBuffer(m_SyncRegistry, *m_GpuWorld);
                    });
                m_RenderPrepGraph.AddPass(
                    "RenderPrep.GpuWorldSync",
                    [] (Core::Dag::TaskGraphBuilder& b)
                    {
                        b.Read<PrepLightSyncTag>();
                        b.Write<PrepGpuWorldSyncTag>();
                    },
                    [this]
                    {
                        m_GpuWorld->SetMaterialBuffer(
                            m_MaterialSystem->GetBuffer(),
                            m_MaterialSystem->GetCapacity());
                        m_GpuWorld->SyncFrame();
                    });
                m_RenderPrepGraph.AddPass(
                    "RenderPrep.CullingSync",
                    [] (Core::Dag::TaskGraphBuilder& b)
                    {
                        b.Read<PrepGpuWorldSyncTag>();
                    },
                    [this]
                    {
                        m_CullingSystem->SyncGpuBuffer();
                    });
                const auto compile = m_RenderPrepGraph.Compile();
                if (!compile.has_value())
                {
                    Core::Log::Error("[Graphics] Render prep TaskGraph compile failed: error={}",
                                     static_cast<int>(compile.error()));
                    m_HasPreparedFrame = false;
                    return;
                }
                const auto execute = m_RenderPrepGraph.Execute();
                if (!execute.has_value())
                {
                    Core::Log::Error("[Graphics] Render prep TaskGraph execute failed: error={}",
                                     static_cast<int>(execute.error()));
                    m_HasPreparedFrame = false;
                    return;
                }
            }
            else
            {
                m_PipelineManager->CommitPending();
                m_MaterialSystem->SyncGpuBuffer();
                m_VisualizationSyncSystem->Sync(
                    m_SyncRegistry,
                    *m_MaterialSystem,
                    *m_ColormapSystem,
                    *m_GpuWorld);
                m_MaterialSystem->SyncGpuBuffer();
                m_TransformSyncSystem->SyncGpuBuffer(m_SyncRegistry, *m_GpuWorld, *m_MaterialSystem);
                m_LightSystem->SyncGpuBuffer(m_SyncRegistry, *m_GpuWorld);
                m_GpuWorld->SetMaterialBuffer(
                    m_MaterialSystem->GetBuffer(),
                    m_MaterialSystem->GetCapacity());
                m_GpuWorld->SyncFrame();
                m_CullingSystem->SyncGpuBuffer();
            }
            m_HasPreparedFrame = true;
        }

        void ExecuteFrame(const RHI::FrameHandle& frame,
                          const RenderWorld& renderWorld) override
        {
            m_LastRenderGraphStats = {};
            if (!m_HasPreparedFrame)
            {
                m_LastRenderGraphStats.Diagnostic = "ExecuteFrame requires successful PrepareFrame.";
                Core::Log::Warn("[Graphics] ExecuteFrame called before successful PrepareFrame");
                return;
            }
            m_RenderGraph.Reset();
            const auto backbuffer = m_RenderGraph.ImportBackbuffer("Null.Backbuffer", RHI::TextureHandle{0u, 1u});
            const auto sceneTable = m_RenderGraph.ImportBuffer(
                "GpuWorld.SceneTable",
                m_GpuWorld->GetSceneTableBuffer(),
                BufferState::ShaderRead,
                BufferState::ShaderRead);
            const auto instanceStatic = m_RenderGraph.ImportBuffer(
                "GpuWorld.InstanceStatic",
                m_GpuWorld->GetInstanceStaticBuffer(),
                BufferState::ShaderRead,
                BufferState::ShaderRead);
            const auto instanceDynamic = m_RenderGraph.ImportBuffer(
                "GpuWorld.InstanceDynamic",
                m_GpuWorld->GetInstanceDynamicBuffer(),
                BufferState::ShaderRead,
                BufferState::ShaderRead);
            const auto entityConfig = m_RenderGraph.ImportBuffer(
                "GpuWorld.EntityConfig",
                m_GpuWorld->GetEntityConfigBuffer(),
                BufferState::ShaderRead,
                BufferState::ShaderRead);
            const auto geometryRecords = m_RenderGraph.ImportBuffer(
                "GpuWorld.GeometryRecords",
                m_GpuWorld->GetGeometryRecordBuffer(),
                BufferState::ShaderRead,
                BufferState::ShaderRead);
            const auto bounds = m_RenderGraph.ImportBuffer(
                "GpuWorld.Bounds",
                m_GpuWorld->GetBoundsBuffer(),
                BufferState::ShaderRead,
                BufferState::ShaderRead);
            const auto lights = m_RenderGraph.ImportBuffer(
                "GpuWorld.Lights",
                m_GpuWorld->GetLightBuffer(),
                BufferState::ShaderRead,
                BufferState::ShaderRead);
            const auto materialBuffer = m_RenderGraph.ImportBuffer(
                "Material.Buffer",
                m_MaterialSystem->GetBuffer(),
                BufferState::ShaderRead,
                BufferState::ShaderRead);
            const auto drawIndirect = m_RenderGraph.ImportBuffer(
                "Cull.SurfaceOpaque.IndexedArgs",
                m_CullingSystem->GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque).IndexedArgsBuffer,
                BufferState::ShaderWrite,
                BufferState::IndirectRead);
            const auto drawCount = m_RenderGraph.ImportBuffer(
                "Cull.SurfaceOpaque.Count",
                m_CullingSystem->GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque).CountBuffer,
                BufferState::ShaderWrite,
                BufferState::IndirectRead);
            const auto depth = m_RenderGraph.CreateTexture("Null.Depth",
                                                           RHI::TextureDesc{
                                                               .Width = 1u,
                                                               .Height = 1u,
                                                               .Fmt = RHI::Format::D32_FLOAT,
                                                               .Usage = RHI::TextureUsage::DepthTarget,
                                                           });
            const auto gbuffer = m_RenderGraph.CreateTexture("Null.GBufferA",
                                                             RHI::TextureDesc{
                                                                 .Width = 1u,
                                                                 .Height = 1u,
                                                                 .Fmt = RHI::Format::RGBA16_FLOAT,
                                                                 .Usage = RHI::TextureUsage::ColorTarget |
                                                                          RHI::TextureUsage::Sampled,
                                                             });
            const auto entityId = m_RenderGraph.CreateTexture("Null.EntityId",
                                                              RHI::TextureDesc{
                                                                  .Width = 1u,
                                                                  .Height = 1u,
                                                                  .Fmt = RHI::Format::R32_UINT,
                                                                  .Usage = RHI::TextureUsage::ColorTarget |
                                                                           RHI::TextureUsage::Sampled,
                                                              });
            const auto lit = m_RenderGraph.CreateTexture("Null.Lit",
                                                         RHI::TextureDesc{
                                                             .Width = 1u,
                                                             .Height = 1u,
                                                             .Fmt = RHI::Format::RGBA16_FLOAT,
                                                             .Usage = RHI::TextureUsage::ColorTarget |
                                                                      RHI::TextureUsage::Sampled,
                                                         });
            const auto post = m_RenderGraph.CreateTexture("Null.Post",
                                                          RHI::TextureDesc{
                                                              .Width = 1u,
                                                              .Height = 1u,
                                                              .Fmt = RHI::Format::RGBA16_FLOAT,
                                                              .Usage = RHI::TextureUsage::ColorTarget |
                                                                       RHI::TextureUsage::Sampled,
                                                          });
            const auto optionalDebug = m_RenderGraph.CreateTexture("Null.OptionalDebug",
                                                                   RHI::TextureDesc{
                                                                       .Width = 1u,
                                                                       .Height = 1u,
                                                                       .Fmt = RHI::Format::RGBA8_UNORM,
                                                                       .Usage = RHI::TextureUsage::ColorTarget |
                                                                                RHI::TextureUsage::Sampled,
                                                                   });
            const auto picking = m_RenderGraph.CreateBuffer(
                "Null.Picking",
                RHI::BufferDesc{
                    .SizeBytes = sizeof(std::uint32_t),
                    .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc,
                    .DebugName = "Null.Picking",
                });

            [[maybe_unused]] const auto passCompute = m_RenderGraph.AddPass("Null.Compute.Prologue", [sceneTable, instanceStatic, instanceDynamic, entityConfig, geometryRecords, bounds, materialBuffer, lights, drawIndirect, drawCount, picking](RenderGraphBuilder& builder) {
                builder.Read(sceneTable, BufferUsage::ShaderRead);
                builder.Read(instanceStatic, BufferUsage::ShaderRead);
                builder.Read(instanceDynamic, BufferUsage::ShaderRead);
                builder.Read(entityConfig, BufferUsage::ShaderRead);
                builder.Read(geometryRecords, BufferUsage::ShaderRead);
                builder.Read(bounds, BufferUsage::ShaderRead);
                builder.Read(materialBuffer, BufferUsage::ShaderRead);
                builder.Read(lights, BufferUsage::ShaderRead);
                builder.Write(drawIndirect, BufferUsage::ShaderWrite);
                builder.Write(drawCount, BufferUsage::ShaderWrite);
                builder.Write(picking, BufferUsage::TransferDst);
            });
            [[maybe_unused]] const auto passCulling = m_RenderGraph.AddPass("Null.Culling", [drawIndirect, drawCount, picking](RenderGraphBuilder& builder) {
                builder.Read(drawIndirect, BufferUsage::IndirectRead);
                builder.Read(drawCount, BufferUsage::IndirectRead);
                builder.Read(picking, BufferUsage::ShaderRead);
                builder.SideEffect();
            });
            if (renderWorld.HasPendingPick)
            {
                [[maybe_unused]] const auto passPicking = m_RenderGraph.AddPass("Null.Picking", [picking](RenderGraphBuilder& builder) {
                    builder.Write(picking, BufferUsage::ShaderWrite);
                    builder.SideEffect();
                });
            }
            [[maybe_unused]] const auto passDepth = m_RenderGraph.AddPass("Null.DepthPrepass", [depth](RenderGraphBuilder& builder) {
                builder.Write(depth, TextureUsage::DepthWrite);
            });
            [[maybe_unused]] const auto passGBuffer = m_RenderGraph.AddPass("Null.GBuffer", [gbuffer, entityId, depth](RenderGraphBuilder& builder) {
                builder.Write(gbuffer, TextureUsage::ColorAttachmentWrite);
                builder.Write(entityId, TextureUsage::ColorAttachmentWrite);
                builder.Read(depth, TextureUsage::DepthRead);
            });
            [[maybe_unused]] const auto passDeferred = m_RenderGraph.AddPass("Null.DeferredLighting", [gbuffer, lit, lights](RenderGraphBuilder& builder) {
                builder.Read(gbuffer, TextureUsage::ShaderRead);
                builder.Read(lights, BufferUsage::ShaderRead);
                builder.Write(lit, TextureUsage::ColorAttachmentWrite);
            });
            [[maybe_unused]] const auto passForwardSurface = m_RenderGraph.AddPass("Null.ForwardSurface", [lit, depth](RenderGraphBuilder& builder) {
                builder.Read(depth, TextureUsage::DepthRead);
                builder.Write(lit, TextureUsage::ColorAttachmentWrite);
            });
            [[maybe_unused]] const auto passForwardLine = m_RenderGraph.AddPass("Null.ForwardLine", [lit](RenderGraphBuilder& builder) {
                builder.Write(lit, TextureUsage::ColorAttachmentWrite);
            });
            [[maybe_unused]] const auto passForwardPoint = m_RenderGraph.AddPass("Null.ForwardPoint", [lit](RenderGraphBuilder& builder) {
                builder.Write(lit, TextureUsage::ColorAttachmentWrite);
            });
            [[maybe_unused]] const auto passBloom = m_RenderGraph.AddPass("Null.Bloom", [lit, post](RenderGraphBuilder& builder) {
                builder.Read(lit, TextureUsage::ShaderRead);
                builder.Write(post, TextureUsage::ColorAttachmentWrite);
            });
            [[maybe_unused]] const auto passToneMap = m_RenderGraph.AddPass("Null.ToneMap", [post](RenderGraphBuilder& builder) {
                builder.Write(post, TextureUsage::ColorAttachmentWrite);
            });
            [[maybe_unused]] const auto passFxaa = m_RenderGraph.AddPass("Null.FXAA", [post](RenderGraphBuilder& builder) {
                builder.Read(post, TextureUsage::ShaderRead);
            });
            [[maybe_unused]] const auto passSelection = m_RenderGraph.AddPass("Null.SelectionOutline", [post, entityId, depth](RenderGraphBuilder& builder) {
                builder.Read(entityId, TextureUsage::ShaderRead);
                builder.Read(depth, TextureUsage::DepthRead);
                builder.Write(post, TextureUsage::ColorAttachmentWrite);
            });
            [[maybe_unused]] const auto passOptionalDebug = m_RenderGraph.AddPass("Null.OptionalDebugView", [optionalDebug](RenderGraphBuilder& builder) {
                builder.Write(optionalDebug, TextureUsage::ColorAttachmentWrite);
            });
            [[maybe_unused]] const auto passImGui = m_RenderGraph.AddPass("Null.ImGui", [post](RenderGraphBuilder& builder) {
                builder.Read(post, TextureUsage::ShaderRead);
                builder.SideEffect();
            });
            [[maybe_unused]] const auto passPresent = m_RenderGraph.AddPass("Null.Present", [backbuffer, post](RenderGraphBuilder& builder) {
                builder.Read(post, TextureUsage::ShaderRead);
                builder.Read(backbuffer, TextureUsage::Present);
                builder.SideEffect();
            });

            const auto compileBegin = std::chrono::steady_clock::now();
            auto compiled = m_RenderGraph.Compile();
            const auto compileEnd = std::chrono::steady_clock::now();
            m_LastRenderGraphStats.CompileTimeMicros = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(compileEnd - compileBegin).count());
            if (!compiled.has_value())
            {
                m_LastRenderGraphStats.Diagnostic = m_RenderGraph.GetLastCompileDiagnostic();
                Core::Log::Error("[Graphics] RenderGraph Compile() failed: error={} diagnostic={}",
                                 static_cast<int>(compiled.error()),
                                 m_LastRenderGraphStats.Diagnostic);
                return;
            }
            m_LastRenderGraphStats.CompileSucceeded = true;
            m_LastRenderGraphStats.PassCount = compiled->PassCount;
            m_LastRenderGraphStats.CulledPassCount = compiled->CulledPassCount;
            m_LastRenderGraphStats.ResourceCount = compiled->ResourceCount;
            m_LastRenderGraphStats.BarrierCount = static_cast<std::uint32_t>(compiled->BarrierPackets.size());
            m_LastRenderGraphStats.TransientMemoryEstimateBytes = compiled->TransientMemoryEstimateBytes;
            m_LastRenderGraphStats.DebugDump = BuildRenderGraphDebugDump(*compiled);

            const auto executeBegin = std::chrono::steady_clock::now();
            if (m_Device == nullptr)
            {
                m_LastRenderGraphStats.Diagnostic = "RenderGraph execute requires a live device.";
                Core::Log::Error("[Graphics] RenderGraph Execute() failed: device missing");
                return;
            }

            auto& graphicsContext = m_Device->GetGraphicsContext(frame.FrameIndex);
            const auto executeResult = m_RenderGraphExecutor.Execute(
                *compiled,
                {},
                {},
                [&graphicsContext, &compiled](const BarrierPacket& packet)
                {
                    SubmitBarrierPacket(graphicsContext, *compiled, packet);
                });
            const auto executeEnd = std::chrono::steady_clock::now();
            m_LastRenderGraphStats.ExecuteTimeMicros = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(executeEnd - executeBegin).count());
            if (!executeResult.has_value())
            {
                m_LastRenderGraphStats.Diagnostic = "RenderGraph execute failed.";
                Core::Log::Error("[Graphics] RenderGraph Execute() failed: error={}",
                                 static_cast<int>(executeResult.error()));
                return;
            }
            m_LastRenderGraphStats.ExecuteSucceeded = true;

            // Phase 14.2 GPU order is intentionally fixed for concrete
            // backends:
            //   1) culling counter reset
            //   2) culling dispatch
            //   3) depth prepass (optional)
            //   4) gbuffer
            //   5) shadows
            //   6) deferred lighting
            //   7) forward lines
            //   8) forward points
            //   9) selection/outline
            //  10) postprocess/present
            //
            // Null backend records no commands.
        }

        std::uint64_t EndFrame(const RHI::FrameHandle&) override
        {
            // No GPU timeline — report zero so maintenance callers
            // never block waiting for a value that will never arrive.
            return 0;
        }

        // ── Resource managers ─────────────────────────────────────────────

        RHI::BufferManager&   GetBufferManager()   override { return *m_BufferManager;   }
        RHI::TextureManager&  GetTextureManager()  override { return *m_TextureManager;  }
        RHI::SamplerManager&  GetSamplerManager()  override { return *m_SamplerManager;  }
        RHI::PipelineManager& GetPipelineManager() override { return *m_PipelineManager; }
        GpuWorld&             GetGpuWorld()        override { return *m_GpuWorld;        }
        MaterialSystem&        GetMaterialSystem()  override { return *m_MaterialSystem;  }
        ColormapSystem&        GetColormapSystem()  override { return *m_ColormapSystem;  }
        VisualizationSyncSystem& GetVisualizationSyncSystem() override { return *m_VisualizationSyncSystem; }
        CullingSystem&         GetCullingSystem()   override { return *m_CullingSystem;   }
        TransformSyncSystem&   GetTransformSyncSystem() override { return *m_TransformSyncSystem; }
        LightSystem&           GetLightSystem()     override { return *m_LightSystem;     }
        SelectionSystem&       GetSelectionSystem() override { return *m_SelectionSystem; }
        ForwardSystem&         GetForwardSystem()   override { return *m_ForwardSystem;   }
        DeferredSystem&        GetDeferredSystem()  override { return *m_DeferredSystem;  }
        PostProcessSystem&     GetPostProcessSystem() override { return *m_PostProcessSystem; }
        ShadowSystem&          GetShadowSystem()    override { return *m_ShadowSystem;    }
        const RenderGraphFrameStats& GetLastRenderGraphStats() const override { return m_LastRenderGraphStats; }

    private:
        std::optional<RHI::BufferManager>   m_BufferManager;
        std::optional<RHI::SamplerManager>  m_SamplerManager;
        std::optional<RHI::TextureManager>  m_TextureManager;
        std::optional<RHI::PipelineManager> m_PipelineManager;
        std::optional<GpuWorld>              m_GpuWorld;
        std::optional<MaterialSystem>        m_MaterialSystem;
        std::optional<ColormapSystem>        m_ColormapSystem;
        std::optional<VisualizationSyncSystem> m_VisualizationSyncSystem;
        std::optional<CullingSystem>         m_CullingSystem;
        std::optional<TransformSyncSystem>   m_TransformSyncSystem;
        std::optional<LightSystem>           m_LightSystem;
        std::optional<SelectionSystem>       m_SelectionSystem;
        std::optional<ForwardSystem>         m_ForwardSystem;
        std::optional<DeferredSystem>        m_DeferredSystem;
        std::optional<PostProcessSystem>     m_PostProcessSystem;
        std::optional<ShadowSystem>          m_ShadowSystem;
        RHI::IDevice*                        m_Device{nullptr};
        entt::registry                       m_SyncRegistry;
        RenderGraph                          m_RenderGraph;
        RenderGraphExecutor                  m_RenderGraphExecutor;
        Core::Dag::TaskGraph                 m_RenderPrepGraph{Core::Dag::QueueDomain::Cpu};
        bool                                 m_EnableRenderPrepTaskGraph{true};
        bool                                 m_HasExtractedRenderWorld{false};
        bool                                 m_HasPreparedFrame{false};
        RenderGraphFrameStats                m_LastRenderGraphStats;
    };

    std::unique_ptr<IRenderer> CreateRenderer()
    {
        return std::make_unique<NullRenderer>();
    }
}
