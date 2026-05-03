module;

#include <cstdint>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

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
import Extrinsic.Graphics.VisualizationPackets;
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
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.FrameRecipe;
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

        constexpr float kMinLineWidth = 0.5f;
        constexpr float kMaxLineWidth = 32.0f;
        constexpr float kMinPointRadius = 0.0001f;
        constexpr float kMaxPointRadius = 1.0f;

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::vec4 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z) && std::isfinite(value.w);
        }

        [[nodiscard]] bool IsValidDebugLine(const DebugLinePacket& line) noexcept
        {
            return IsFinite(line.Start) && IsFinite(line.End) && IsFinite(line.Color) &&
                   std::isfinite(line.Width) && line.Width > 0.f;
        }

        [[nodiscard]] bool IsValidDebugPoint(const DebugPointPacket& point) noexcept
        {
            return IsFinite(point.Position) && IsFinite(point.Color) &&
                   std::isfinite(point.Radius) && point.Radius > 0.f;
        }

        [[nodiscard]] bool IsValidDebugTriangle(const DebugTrianglePacket& triangle) noexcept
        {
            return IsFinite(triangle.A) && IsFinite(triangle.B) &&
                   IsFinite(triangle.C) && IsFinite(triangle.Color);
        }

        [[nodiscard]] bool IsFinite(const glm::mat4& value) noexcept
        {
            return IsFinite(value[0]) && IsFinite(value[1]) &&
                   IsFinite(value[2]) && IsFinite(value[3]);
        }

        [[nodiscard]] bool IsValidTransformGizmo(const TransformGizmoRenderPacket& gizmo) noexcept
        {
            return IsFinite(gizmo.Transform) &&
                   IsFinite(gizmo.XAxisColor) && IsFinite(gizmo.YAxisColor) && IsFinite(gizmo.ZAxisColor) &&
                   std::isfinite(gizmo.AxisLength) && gizmo.AxisLength > 0.f;
        }
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
            m_VisualizationSyncRecords.clear();
            m_VisualizationAttributeBuffers.clear();
            m_VisualizationScalars.clear();
            m_VisualizationColors.clear();
            m_VisualizationVectorFields.clear();
            m_VisualizationIsolines.clear();
            m_VisualizationHtexAtlases.clear();
            m_VisualizationFragmentBakeAtlases.clear();
            m_VisualizationDiagnostics = {};
            m_VisualizationOverlaySummary = {};
            m_TransformSyncRecords.clear();
            m_LightSnapshots.clear();
            m_DebugLinePackets.clear();
            m_DebugPointPackets.clear();
            m_DebugTrianglePackets.clear();
            m_TransformGizmoPackets.clear();
            m_RenderableSnapshots.clear();
            m_InvalidSnapshotRecordCount = 0;
            m_HasExtractedRenderWorld = false;
            m_HasPreparedFrame = false;
            return true;
        }

        void SubmitRuntimeSnapshots(const RuntimeRenderSnapshotBatch& snapshots) override
        {
            m_TransformSyncRecords.assign(snapshots.Transforms.begin(), snapshots.Transforms.end());
            m_LightSnapshots.assign(snapshots.Lights.begin(), snapshots.Lights.end());
            m_VisualizationSyncRecords.assign(snapshots.Visualizations.begin(), snapshots.Visualizations.end());
            m_VisualizationAttributeBuffers.assign(snapshots.VisualizationAttributeBuffers.begin(), snapshots.VisualizationAttributeBuffers.end());
            m_VisualizationScalars.assign(snapshots.VisualizationScalars.begin(), snapshots.VisualizationScalars.end());
            m_VisualizationColors.assign(snapshots.VisualizationColors.begin(), snapshots.VisualizationColors.end());
            m_VisualizationVectorFields.assign(snapshots.VisualizationVectorFields.begin(), snapshots.VisualizationVectorFields.end());
            m_VisualizationIsolines.assign(snapshots.VisualizationIsolines.begin(), snapshots.VisualizationIsolines.end());
            m_VisualizationHtexAtlases.assign(snapshots.VisualizationHtexAtlases.begin(), snapshots.VisualizationHtexAtlases.end());
            m_VisualizationFragmentBakeAtlases.assign(snapshots.VisualizationFragmentBakeAtlases.begin(), snapshots.VisualizationFragmentBakeAtlases.end());
            const VisualizationPacketBatch visualizationBatch{
                .AttributeBuffers = m_VisualizationAttributeBuffers,
                .Scalars = m_VisualizationScalars,
                .Colors = m_VisualizationColors,
                .VectorFields = m_VisualizationVectorFields,
                .Isolines = m_VisualizationIsolines,
                .HtexAtlases = m_VisualizationHtexAtlases,
                .FragmentBakeAtlases = m_VisualizationFragmentBakeAtlases,
            };
            m_VisualizationDiagnostics = ValidateVisualizationPackets(visualizationBatch);
            m_VisualizationOverlaySummary = BuildVisualizationOverlaySummary(visualizationBatch);
            m_InvalidSnapshotRecordCount = 0;

            m_DebugLinePackets.clear();
            m_DebugPointPackets.clear();
            m_DebugTrianglePackets.clear();
            m_TransformGizmoPackets.clear();
            m_DebugLinePackets.reserve(snapshots.DebugLines.size());
            m_DebugPointPackets.reserve(snapshots.DebugPoints.size());
            m_DebugTrianglePackets.reserve(snapshots.DebugTriangles.size());
            m_TransformGizmoPackets.reserve(snapshots.TransformGizmos.size());

            for (DebugLinePacket line : snapshots.DebugLines)
            {
                if (!IsValidDebugLine(line))
                {
                    ++m_InvalidSnapshotRecordCount;
                    continue;
                }
                line.Width = std::clamp(line.Width, kMinLineWidth, kMaxLineWidth);
                m_DebugLinePackets.push_back(line);
            }

            for (DebugPointPacket point : snapshots.DebugPoints)
            {
                if (!IsValidDebugPoint(point))
                {
                    ++m_InvalidSnapshotRecordCount;
                    continue;
                }
                point.Radius = std::clamp(point.Radius, kMinPointRadius, kMaxPointRadius);
                m_DebugPointPackets.push_back(point);
            }

            for (const DebugTrianglePacket& triangle : snapshots.DebugTriangles)
            {
                if (!IsValidDebugTriangle(triangle))
                {
                    ++m_InvalidSnapshotRecordCount;
                    continue;
                }
                m_DebugTrianglePackets.push_back(triangle);
            }

            for (TransformGizmoRenderPacket gizmo : snapshots.TransformGizmos)
            {
                if (!IsValidTransformGizmo(gizmo))
                {
                    ++m_InvalidSnapshotRecordCount;
                    continue;
                }
                gizmo.AxisLength = std::clamp(gizmo.AxisLength, 0.001f, 10000.f);
                m_TransformGizmoPackets.push_back(gizmo);
            }

            m_RenderableSnapshots.clear();
            m_RenderableSnapshots.reserve(m_TransformSyncRecords.size());
            for (const TransformSyncRecord& record : m_TransformSyncRecords)
            {
                if (!record.Instance.IsValid())
                {
                    ++m_InvalidSnapshotRecordCount;
                    continue;
                }

                m_RenderableSnapshots.push_back(RenderableSnapshot{
                    .StableId = record.StableId,
                    .Instance = record.Instance,
                    .Model = record.Model,
                    .Bounds = record.Bounds,
                    .RenderFlags = record.RenderFlags,
                    .MaterialSlot = record.MaterialSlot,
                    .HasMaterialSlot = record.HasMaterialSlot,
                });
            }
        }

        RenderWorld ExtractRenderWorld(const RenderFrameInput& input) override
        {
            m_HasExtractedRenderWorld = true;
            m_HasPreparedFrame = false;
            const PickPixelRequest pick = input.Pick.Pending
                ? input.Pick
                : PickPixelRequest{.X = 0u, .Y = 0u, .Pending = input.HasPendingPick};
            const CameraViewSnapshot camera = BuildCameraViewSnapshot(input.Camera, input.Viewport, pick);
            return RenderWorld{
                .Viewport       = input.Viewport,
                .Alpha          = input.Alpha,
                .HasPendingPick = input.HasPendingPick,
                .DebugOverlayEnabled = input.DebugOverlayEnabled,
                .Camera = camera,
                .Renderables = m_RenderableSnapshots,
                .Lights = m_LightSnapshots,
                .PickRequest = PickRequestSnapshot{
                    .Pending = input.HasPendingPick || input.Pick.Pending,
                    .X = pick.X,
                    .Y = pick.Y,
                    .RayOrigin = camera.PickRayOrigin,
                    .RayDirection = camera.PickRayDirection,
                    .HasRay = camera.HasPickRay,
                },
                .DebugPrimitives = DebugPrimitiveSnapshot{
                    .Lines = m_DebugLinePackets,
                    .Points = m_DebugPointPackets,
                    .Triangles = m_DebugTrianglePackets,
                    .LineCount = static_cast<std::uint32_t>(m_DebugLinePackets.size()),
                    .PointCount = static_cast<std::uint32_t>(m_DebugPointPackets.size()),
                    .TriangleCount = static_cast<std::uint32_t>(m_DebugTrianglePackets.size()),
                    .HasTransientDebug = input.DebugOverlayEnabled ||
                        !m_DebugLinePackets.empty() ||
                        !m_DebugPointPackets.empty() ||
                        !m_DebugTrianglePackets.empty(),
                },
                .Gizmos = GizmoRenderSnapshot{
                    .TransformGizmos = m_TransformGizmoPackets,
                    .TransformGizmoCount = static_cast<std::uint32_t>(m_TransformGizmoPackets.size()),
                    .HasGizmos = !m_TransformGizmoPackets.empty(),
                },
                .Visualization = VisualizationSnapshot{
                    .AttributeBuffers = m_VisualizationAttributeBuffers,
                    .Scalars = m_VisualizationScalars,
                    .Colors = m_VisualizationColors,
                    .VectorFields = m_VisualizationVectorFields,
                    .Isolines = m_VisualizationIsolines,
                    .HtexAtlases = m_VisualizationHtexAtlases,
                    .FragmentBakeAtlases = m_VisualizationFragmentBakeAtlases,
                    .Diagnostics = m_VisualizationDiagnostics,
                    .OverlaySummary = m_VisualizationOverlaySummary,
                    .HasVisualizationPackets = m_VisualizationDiagnostics.InputPacketCount > 0u,
                },
                .PostProcess = PostProcessSnapshot{
                    .Enabled = input.DebugOverlayEnabled,
                },
                .InvalidSnapshotRecordCount = m_InvalidSnapshotRecordCount,
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
                            m_VisualizationSyncRecords,
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
                        m_TransformSyncSystem->SyncGpuBuffer(m_TransformSyncRecords, *m_GpuWorld);
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
                        m_LightSystem->SyncGpuBuffer(m_LightSnapshots, *m_GpuWorld);
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
                    m_VisualizationSyncRecords,
                    *m_MaterialSystem,
                    *m_ColormapSystem,
                    *m_GpuWorld);
                m_MaterialSystem->SyncGpuBuffer();
                m_TransformSyncSystem->SyncGpuBuffer(m_TransformSyncRecords, *m_GpuWorld);
                m_LightSystem->SyncGpuBuffer(m_LightSnapshots, *m_GpuWorld);
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
            const auto& surfaceOpaque = m_CullingSystem->GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque);
            const auto& lines = m_CullingSystem->GetBucket(RHI::GpuDrawBucketKind::Lines);
            const auto& points = m_CullingSystem->GetBucket(RHI::GpuDrawBucketKind::Points);
            const FrameRecipeBuildResult recipe = BuildDefaultFrameRecipe(
                m_RenderGraph,
                DeriveDefaultFrameRecipeFeatures(renderWorld),
                FrameRecipeImports{
                    .Backbuffer = RHI::TextureHandle{0u, 1u},
                    .SceneTable = m_GpuWorld->GetSceneTableBuffer(),
                    .InstanceStatic = m_GpuWorld->GetInstanceStaticBuffer(),
                    .InstanceDynamic = m_GpuWorld->GetInstanceDynamicBuffer(),
                    .EntityConfig = m_GpuWorld->GetEntityConfigBuffer(),
                    .GeometryRecords = m_GpuWorld->GetGeometryRecordBuffer(),
                    .Bounds = m_GpuWorld->GetBoundsBuffer(),
                    .Lights = m_GpuWorld->GetLightBuffer(),
                    .MaterialBuffer = m_MaterialSystem->GetBuffer(),
                    .SurfaceOpaqueIndexedArgs = surfaceOpaque.IndexedArgsBuffer,
                    .SurfaceOpaqueCount = surfaceOpaque.CountBuffer,
                    .LinesIndexedArgs = lines.IndexedArgsBuffer,
                    .LinesCount = lines.CountBuffer,
                    .PointsNonIndexedArgs = points.NonIndexedArgsBuffer,
                    .PointsCount = points.CountBuffer,
                },
                FrameRecipeSizing{
                    .Width = renderWorld.Viewport.Width > 0 ? static_cast<std::uint32_t>(renderWorld.Viewport.Width) : 1u,
                    .Height = renderWorld.Viewport.Height > 0 ? static_cast<std::uint32_t>(renderWorld.Viewport.Height) : 1u,
                    .BackbufferFormat = RHI::Format::RGBA8_UNORM,
                    .DepthFormat = RHI::Format::D32_FLOAT,
                });
            if (!recipe.Succeeded)
            {
                m_LastRenderGraphStats.Diagnostic = recipe.Diagnostic;
                Core::Log::Error("[Graphics] FrameRecipe build failed: diagnostic={}", recipe.Diagnostic);
                return;
            }

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
        RenderGraph                          m_RenderGraph;
        RenderGraphExecutor                  m_RenderGraphExecutor;
        Core::Dag::TaskGraph                 m_RenderPrepGraph{Core::Dag::QueueDomain::Cpu};
        std::vector<VisualizationSyncRecord> m_VisualizationSyncRecords;
        std::vector<VisualizationAttributeBufferPacket> m_VisualizationAttributeBuffers;
        std::vector<ScalarAttributePacket>              m_VisualizationScalars;
        std::vector<ColorAttributePacket>               m_VisualizationColors;
        std::vector<VectorFieldOverlayPacket>           m_VisualizationVectorFields;
        std::vector<IsolineOverlayPacket>               m_VisualizationIsolines;
        std::vector<HtexPatchPreviewAtlasPacket>        m_VisualizationHtexAtlases;
        std::vector<FragmentBakeAtlasPacket>            m_VisualizationFragmentBakeAtlases;
        VisualizationDiagnostics             m_VisualizationDiagnostics{};
        VisualizationOverlaySummary          m_VisualizationOverlaySummary{};
        std::vector<TransformSyncRecord>     m_TransformSyncRecords;
        std::vector<LightSnapshot>           m_LightSnapshots;
        std::vector<DebugLinePacket>         m_DebugLinePackets;
        std::vector<DebugPointPacket>        m_DebugPointPackets;
        std::vector<DebugTrianglePacket>     m_DebugTrianglePackets;
        std::vector<TransformGizmoRenderPacket> m_TransformGizmoPackets;
        std::vector<RenderableSnapshot>      m_RenderableSnapshots;
        std::uint32_t                        m_InvalidSnapshotRecordCount{0};
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
