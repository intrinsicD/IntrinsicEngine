module;

#include <cstdint>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

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
import Extrinsic.Graphics.Material;
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
import Extrinsic.Graphics.Pass.DepthPrepass;
import Extrinsic.Graphics.Pass.Surface.MinimalDebug;
import Extrinsic.Graphics.Pass.Present.MinimalDebug;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Filesystem.PathResolver;
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

        constexpr float kCameraInverseDeterminantEpsilon = 0.000001f;
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

        [[nodiscard]] bool IsInvertibleFiniteMatrix(const glm::mat4& value) noexcept
        {
            const float determinant = glm::determinant(value);
            return IsFinite(value) && std::isfinite(determinant) &&
                   std::abs(determinant) > kCameraInverseDeterminantEpsilon;
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
            m_BackbufferFormat = device.GetBackbufferFormat();
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
            if (device.IsOperational())
            {
                [[maybe_unused]] const bool passResourcesReady = InitializeOperationalPassResources(device);
            }
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

        bool RebuildOperationalResources(RHI::IDevice& device) override
        {
            m_Device = &device;
            if (!device.IsOperational())
            {
                m_CullingOutputAvailable = false;
                m_LastRenderGraphStats.LifecycleDiagnostic =
                    "Renderer operational-resource rebuild requires an operational device.";
                return false;
            }
            m_BackbufferFormat = device.GetBackbufferFormat();
            if (!m_BufferManager || !m_PipelineManager || !m_MaterialSystem ||
                !m_GpuWorld || !m_CullingSystem)
            {
                m_LastRenderGraphStats.LifecycleDiagnostic =
                    "Renderer operational-resource rebuild requires initialized renderer systems.";
                return false;
            }

            if (!m_MaterialSystem->RebuildGpuResources(device, *m_BufferManager))
            {
                m_LastRenderGraphStats.LifecycleDiagnostic =
                    "Renderer operational-resource rebuild failed while recreating material buffers.";
                return false;
            }
            if (!m_GpuWorld->RebuildGpuResources(device, *m_BufferManager))
            {
                m_LastRenderGraphStats.LifecycleDiagnostic =
                    "Renderer operational-resource rebuild failed while recreating GpuWorld buffers.";
                return false;
            }
            m_GpuWorld->SetMaterialBuffer(
                m_MaterialSystem->GetBuffer(),
                m_MaterialSystem->GetCapacity());
            m_MaterialSystem->SyncGpuBuffer();
            m_GpuWorld->SyncFrame();

            const bool passResourcesReady = InitializeOperationalPassResources(device);
            m_RenderGraph.Reset();
            m_LastRenderGraphStats.LifecycleDiagnostic = m_CullingOutputAvailable
                ? std::string{}
                : std::string{"Renderer operational-resource rebuild completed with culling unavailable."};
            return passResourcesReady;
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
            m_DepthPrepassPipelineLease.reset();
            m_DefaultDebugSurfacePipelineLease.reset();
            m_MinimalDebugPresentPipelineLease.reset();
            m_MinimalDebugSurfacePass.SetPipeline(RHI::PipelineHandle{});
            m_MinimalDebugPresentPass.SetPipeline(RHI::PipelineHandle{});
            m_PipelineManager.reset();
            m_TextureManager .reset();
            m_SamplerManager .reset();
            m_BufferManager  .reset();
            m_CullingOutputAvailable = false;
            // GRAPHICS-033D — drop the smoke's readback handle so a later
            // Initialize starts with the wiring disabled (the smoke fixture
            // re-arms it after Initialize before Run).
            m_MinimalDebugReadbackBuffer = RHI::BufferHandle{};
        }

        void Resize(std::uint32_t, std::uint32_t) override
        {
            m_RenderGraph.Reset();
        }

        // ── Per-frame phases ──────────────────────────────────────────────

        bool BeginFrame(RHI::FrameHandle& outFrame) override
        {
            ResetFrameState();
            if (m_Device == nullptr)
            {
                m_LastRenderGraphStats.LifecycleDiagnostic = "BeginFrame requires a live device.";
                Core::Log::Error("[Graphics] BeginFrame failed: device missing");
                return false;
            }
            return m_Device->BeginFrame(outFrame);
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
            if (m_MaterialSystem)
            {
                m_MaterialSystem->ResetPerFrameSubstitutionCounters();
            }

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

            // GRAPHICS-031B Decision 7 path-(b): substitute missing / invalid
            // material slots with `kDefaultMaterialSlotIndex` (slot 0 =
            // `Material.DefaultDebugSurface`) and record the substitution
            // category through `MaterialSystem`. Mutate `m_TransformSyncRecords`
            // in place so the downstream `TransformSyncSystem::SyncGpuBuffer`
            // path observes the substituted slot, then mirror the records into
            // the immutable `m_RenderableSnapshots` span exposed via
            // `ExtractRenderWorld`.
            m_RenderableSnapshots.clear();
            m_RenderableSnapshots.reserve(m_TransformSyncRecords.size());
            const std::uint32_t materialCapacity =
                m_MaterialSystem ? m_MaterialSystem->GetCapacity() : 0u;
            for (TransformSyncRecord& record : m_TransformSyncRecords)
            {
                if (!record.Instance.IsValid())
                {
                    ++m_InvalidSnapshotRecordCount;
                    continue;
                }

                if (!record.HasMaterialSlot)
                {
                    record.MaterialSlot = kDefaultMaterialSlotIndex;
                    record.HasMaterialSlot = true;
                    if (m_MaterialSystem)
                    {
                        m_MaterialSystem->RecordMissingMaterialFallback();
                    }
                }
                else if (materialCapacity > 0u && record.MaterialSlot >= materialCapacity)
                {
                    record.MaterialSlot = kDefaultMaterialSlotIndex;
                    if (m_MaterialSystem)
                    {
                        m_MaterialSystem->RecordInvalidMaterialSlot();
                    }
                }

                if (record.MaterialSlot == kDefaultMaterialSlotIndex && m_MaterialSystem)
                {
                    m_MaterialSystem->RecordDefaultDebugSurfaceUse();
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
            if (m_Device == nullptr)
            {
                m_LastRenderGraphStats.Diagnostic = "RenderGraph execute requires a live device.";
                Core::Log::Error("[Graphics] RenderGraph Execute() failed: device missing");
                return;
            }
            m_RenderGraph.Reset();
            const auto& surfaceOpaque = m_CullingSystem->GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque);
            const auto& lines = m_CullingSystem->GetBucket(RHI::GpuDrawBucketKind::Lines);
            const auto& points = m_CullingSystem->GetBucket(RHI::GpuDrawBucketKind::Points);
            const FrameRecipeImports imports{
                .Backbuffer = m_Device->GetBackbufferHandle(frame),
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
            };
            const FrameRecipeSizing sizing{
                .Width = renderWorld.Viewport.Width > 0 ? static_cast<std::uint32_t>(renderWorld.Viewport.Width) : 1u,
                .Height = renderWorld.Viewport.Height > 0 ? static_cast<std::uint32_t>(renderWorld.Viewport.Height) : 1u,
                .BackbufferFormat = m_BackbufferFormat,
                .DepthFormat = RHI::Format::D32_FLOAT,
            };
            const FrameRecipeBuildResult recipe = (m_FrameRecipe == Core::Config::FrameRecipeKind::MinimalDebug)
                ? BuildMinimalDebugSurfaceRecipe(m_RenderGraph, imports, sizing)
                : BuildDefaultFrameRecipe(m_RenderGraph,
                                          DeriveDefaultFrameRecipeFeatures(renderWorld),
                                          imports,
                                          sizing);
            if (!recipe.Succeeded)
            {
                m_LastRenderGraphStats.Diagnostic = recipe.Diagnostic;
                Core::Log::Error("[Graphics] FrameRecipe build failed: diagnostic={}", recipe.Diagnostic);
                // GRAPHICS-033E: a failed recipe build cannot satisfy gate 7;
                // publish fail-closed so the operational gate sees the latest
                // outcome before the next attempt.
                m_Device->NoteRecipeGraphValidation(false);
                return;
            }
            if (m_FrameRecipe == Core::Config::FrameRecipeKind::MinimalDebug)
            {
                // GRAPHICS-032A seeds the counter from recipe-build-time
                // prerequisite gaps; GRAPHICS-032B then accumulates record-time
                // gaps (missing slot-0 lease, SurfaceOpaque bucket, GpuWorld) on
                // top via RecordMinimalDebugSurfacePass.
                m_LastRenderGraphStats.MinimalRecipeMissingPrerequisiteCount += recipe.MissingPrerequisiteCount;
            }

            const auto compileBegin = std::chrono::steady_clock::now();
            auto compiled = m_RenderGraph.Compile();
            const auto compileEnd = std::chrono::steady_clock::now();
            m_LastRenderGraphStats.Compile.TimeMicros = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(compileEnd - compileBegin).count());
            if (!compiled.has_value())
            {
                const auto& findings = m_RenderGraph.GetLastCompileValidationResult().Findings;
                m_LastRenderGraphStats.Diagnostic = findings.empty() ? std::string{} : findings.front().Message;
                Core::Log::Error("[Graphics] RenderGraph Compile() failed: error={} diagnostic={}",
                                 static_cast<int>(compiled.error()),
                                 m_LastRenderGraphStats.Diagnostic);
                // GRAPHICS-033E: a failed compile cannot satisfy gate 7. Publish
                // fail-closed exactly once per compile attempt so the operational
                // gate cannot oscillate stale-clean while the next attempt rebuilds.
                m_Device->NoteRecipeGraphValidation(false);
                return;
            }
            // GRAPHICS-033E: run the recipe-aware validation against the freshly
            // compiled graph and publish a single boolean to the device exactly
            // once per recipe compile. The recipe-aware validator supplies the
            // `ImportedResourceAuthorization` entries that the bare
            // compile-time validator lacks (the compile-time pass has no
            // recipe context, so imported writes from non-side-effect passes
            // such as `CullingPass` always trip
            // `UnauthorizedImportedBufferWrite`). Gate 7
            // (`BarrierValidationClean`) therefore flips to `true` when the
            // recipe-aware validation reports zero `Error`-severity findings.
            const FrameRecipeIntrospection recipeIntrospection =
                (m_FrameRecipe == Core::Config::FrameRecipeKind::MinimalDebug)
                    ? DescribeMinimalDebugSurfaceRecipe()
                    : DescribeDefaultFrameRecipe(DeriveDefaultFrameRecipeFeatures(renderWorld));
            const RenderGraphValidationResult recipeValidation =
                ValidateRecipeCompiledGraph(recipeIntrospection, *compiled);
            const bool recipeValidationClean =
                recipeValidation.CountBySeverity(RenderGraphValidationSeverity::Error) == 0u;
            m_Device->NoteRecipeGraphValidation(recipeValidationClean);

            m_LastRenderGraphStats.Compile.Succeeded = true;
            m_LastRenderGraphStats.Compile.PassCount = compiled->PassCount;
            m_LastRenderGraphStats.Compile.CulledPassCount = compiled->CulledPassCount;
            m_LastRenderGraphStats.Compile.ResourceCount = compiled->ResourceCount;
            m_LastRenderGraphStats.Compile.BarrierCount = static_cast<std::uint32_t>(compiled->BarrierPackets.size());
            m_LastRenderGraphStats.Compile.TransientMemoryEstimateBytes = compiled->TransientMemoryEstimateBytes;
            m_LastRenderGraphStats.DebugDump = BuildRenderGraphDebugDump(*compiled);
            m_LastRenderGraphStats.Execute.DeviceOperational = m_Device->IsOperational();

            const auto executeBegin = std::chrono::steady_clock::now();
            auto& graphicsContext = m_Device->GetGraphicsContext(frame.FrameIndex);
            std::vector<std::string_view> passNameByIndex(compiled->PassDeclarations.size());
            for (std::size_t orderIndex = 0; orderIndex < compiled->TopologicalOrder.size(); ++orderIndex)
            {
                const std::uint32_t passIndex = compiled->TopologicalOrder[orderIndex];
                if (passIndex < passNameByIndex.size() && orderIndex < compiled->PassNames.size())
                {
                    passNameByIndex[passIndex] = compiled->PassNames[orderIndex];
                }
            }

            const RHI::CameraUBO camera = BuildCameraUbo(renderWorld, frame.FrameIndex);
            graphicsContext.Begin();
            const auto executeResult = m_RenderGraphExecutor.Execute(
                *compiled,
                {},
                [this, &graphicsContext, &passNameByIndex, &camera, &frame, &compiled](const std::uint32_t passIndex)
                {
                    if (passIndex >= passNameByIndex.size())
                    {
                        Core::Log::Warn("[Graphics] Routed pass-name resolution failed during execute: passIndex={}",
                                        passIndex);
                        return;
                    }

                    const std::string_view passName = passNameByIndex[passIndex];
                    if (passName.empty())
                    {
                        Core::Log::Warn("[Graphics] Routed pass-name resolution failed during execute: passIndex={}",
                                        passIndex);
                        return;
                    }
                    const ActiveRenderPassDesc activeRenderPass = BuildActiveRenderPassDesc(*compiled, passIndex);
                    if (activeRenderPass.HasAttachments)
                    {
                        graphicsContext.BeginRenderPass(RHI::RenderPassDesc{
                            .ColorTargets = activeRenderPass.ColorAttachments,
                            .Depth = activeRenderPass.DepthAttachment,
                        });
                        const Core::Extent2D extent = m_Device != nullptr
                            ? m_Device->GetBackbufferExtent()
                            : Core::Extent2D{.Width = 1, .Height = 1};
                        const std::uint32_t width = extent.Width > 0 ? static_cast<std::uint32_t>(extent.Width) : 1u;
                        const std::uint32_t height = extent.Height > 0 ? static_cast<std::uint32_t>(extent.Height) : 1u;
                        graphicsContext.SetViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
                        graphicsContext.SetScissor(0, 0, width, height);
                    }

                    const auto endActiveRenderPass = [&graphicsContext, &activeRenderPass]
                    {
                        if (activeRenderPass.HasAttachments)
                        {
                            graphicsContext.EndRenderPass();
                        }
                    };
                    if (passName == std::string_view{"CullingPass"})
                    {
                        const RenderCommandPassStatus status = RecordCullingPass(graphicsContext, camera);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else if (passName == std::string_view{"DepthPrepass"})
                    {
                        const RenderCommandPassStatus status = RecordDepthPrepass(graphicsContext, camera, frame.FrameIndex);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else if (passName == kMinimalDebugSurfacePassName)
                    {
                        const RenderCommandPassStatus status =
                            RecordMinimalDebugSurfacePass(graphicsContext, camera, frame.FrameIndex);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else if (passName == kMinimalDebugPresentPassName)
                    {
                        const RenderCommandPassStatus status =
                            RecordMinimalDebugPresentPass(graphicsContext, camera, frame.FrameIndex);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else
                    {
                        // GRAPHICS-018 §4: surface/deferred/debug pass command bodies
                        // are not yet wired to operational Vulkan resources. They
                        // soft-skip with structured diagnostics so the executor
                        // emits the same render-graph barriers and the renderer
                        // reports complete per-pass status, while preserving
                        // backend-neutral RHI traffic. The status splits cleanly
                        // by device readiness: a non-operational device reports
                        // SkippedNonOperational so CPU CI surfaces accidental
                        // operational claims, and an operational device reports
                        // SkippedUnavailable so future per-pass routing changes
                        // don't silently regress to a no-op.
                        const RenderCommandPassStatus status =
                            (m_Device == nullptr || !m_Device->IsOperational())
                                ? RenderCommandPassStatus::SkippedNonOperational
                                : RenderCommandPassStatus::SkippedUnavailable;
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    endActiveRenderPass();
                },
                [&graphicsContext, &compiled](const BarrierPacket& packet)
                {
                    SubmitBarrierPacket(graphicsContext, *compiled, packet);
                });
            // GRAPHICS-033D — opt-in MinimalDebug backbuffer-to-host readback.
            // Inserted after the executor finalises the
            // `ColorAttachment → Present` transition and before End() closes
            // the command buffer so the copy executes on the same submit that
            // produced the visible-triangle pixels. The triplet leaves the
            // backbuffer back in Present layout so EndFrame's submit + the
            // device's Present() call still produce a well-formed
            // vkQueuePresentKHR. The path is gated on (a) operational device,
            // (b) MinimalDebug recipe, (c) the smoke wired a valid readback
            // buffer through SetMinimalDebugBackbufferReadbackBuffer(), and
            // (d) the executor reports a clean Execute so we never copy from
            // an undefined backbuffer.
            if (executeResult.has_value() &&
                m_FrameRecipe == Core::Config::FrameRecipeKind::MinimalDebug &&
                m_MinimalDebugReadbackBuffer.IsValid() &&
                m_Device != nullptr && m_Device->IsOperational())
            {
                const RHI::TextureHandle backbuffer = m_Device->GetBackbufferHandle(frame);
                if (backbuffer.IsValid())
                {
                    graphicsContext.TextureBarrier(backbuffer,
                                                    RHI::TextureLayout::Present,
                                                    RHI::TextureLayout::TransferSrc);
                    graphicsContext.CopyTextureToBuffer(backbuffer,
                                                        RHI::TextureLayout::TransferSrc,
                                                        0u, 0u,
                                                        m_MinimalDebugReadbackBuffer,
                                                        0u);
                    graphicsContext.TextureBarrier(backbuffer,
                                                    RHI::TextureLayout::TransferSrc,
                                                    RHI::TextureLayout::Present);
                    ++m_LastRenderGraphStats.MinimalDebugBackbufferReadbackCopyCount;
                }
            }
            graphicsContext.End();
            const auto executeEnd = std::chrono::steady_clock::now();
            m_LastRenderGraphStats.Execute.TimeMicros = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(executeEnd - executeBegin).count());
            if (!executeResult.has_value())
            {
                m_LastRenderGraphStats.Diagnostic = "RenderGraph execute failed.";
                Core::Log::Error("[Graphics] RenderGraph Execute() failed: error={}",
                                 static_cast<int>(executeResult.error()));
                return;
            }
            m_LastRenderGraphStats.Execute.Succeeded = true;

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

        std::uint64_t EndFrame(const RHI::FrameHandle& frame) override
        {
            if (m_Device == nullptr)
            {
                m_LastRenderGraphStats.LifecycleDiagnostic = "EndFrame requires a live device.";
                Core::Log::Error("[Graphics] EndFrame failed: device missing");
                return 0;
            }

            m_Device->EndFrame(frame);
            return m_Device->GetGlobalFrameNumber();
        }

        // ── Resource managers ─────────────────────────────────────────────

        [[nodiscard]] RHI::PipelineHandle GetDefaultDebugSurfacePipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_DefaultDebugSurfacePipelineLease.has_value() ||
                !m_DefaultDebugSurfacePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_DefaultDebugSurfacePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetDefaultDebugSurfacePipelineDesc() const noexcept override
        {
            return BuildDefaultDebugSurfacePipelineDesc(m_BackbufferFormat);
        }

        void SetFrameRecipe(Core::Config::FrameRecipeKind kind) noexcept override
        {
            m_FrameRecipe = kind;
        }

        [[nodiscard]] Core::Config::FrameRecipeKind GetFrameRecipe() const noexcept override
        {
            return m_FrameRecipe;
        }

        void SetMinimalDebugBackbufferReadbackBuffer(RHI::BufferHandle handle) noexcept override
        {
            m_MinimalDebugReadbackBuffer = handle;
        }

        [[nodiscard]] RHI::BufferHandle GetMinimalDebugBackbufferReadbackBuffer() const noexcept override
        {
            return m_MinimalDebugReadbackBuffer;
        }

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
        // GRAPHICS-031A — canonical default-debug-surface PipelineDesc.
        //
        // VertexShaderPath / FragmentShaderPath point at the compiled SPIR-V
        // artifacts produced by `intrinsic_add_glsl_shaders()` under the
        // runtime shader output directory (`<bin>/shaders/<relative>.spv`).
        // The Vulkan backend's `ReadSpirvFile()` opens these paths verbatim,
        // so the renderer pre-resolves them via `Core::Filesystem::GetShaderPath`
        // (the same resolver used by the legacy `RenderOrchestrator`). When
        // the SPV files are absent (e.g. CI builds without
        // `INTRINSIC_BUILD_SANDBOX=ON`), `GetShaderPath` returns the raw
        // relative path so the resolved value remains deterministic. Initial
        // `Initialize()` and `RebuildOperationalResources()` therefore
        // republish a byte-identical descriptor against a stable filesystem
        // state.
        [[nodiscard]] static RHI::PipelineDesc BuildDefaultDebugSurfacePipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/forward/default_debug_surface.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/forward/default_debug_surface.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::Back;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = true;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Less;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = colorFormat;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.DefaultDebugSurface";
            return desc;
        }

        [[nodiscard]] static RHI::PipelineDesc BuildMinimalVisibleTrianglePipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/minimal_debug_visible_triangle.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/minimal_debug_visible_triangle.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = colorFormat;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = 0u;
            desc.DebugName = "Renderer.MinimalVisibleTriangle";
            return desc;
        }

        [[nodiscard]] bool InitializeOperationalPassResources(RHI::IDevice& device)
        {
            if (!device.IsOperational() || !m_CullingSystem || !m_BufferManager || !m_PipelineManager)
            {
                m_CullingOutputAvailable = false;
                return false;
            }

            m_CullingSystem->Shutdown();
            m_CullingOutputAvailable = m_CullingSystem->Initialize(
                device,
                *m_BufferManager,
                *m_PipelineManager,
                Core::Filesystem::GetShaderPath("shaders/instance_cull.comp.spv"));

            m_DepthPrepassPipelineLease.reset();
            RHI::PipelineDesc depthPrepassDesc{};
            depthPrepassDesc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/depth_prepass.vert.spv");
            depthPrepassDesc.ColorTargetCount = 0u;
            depthPrepassDesc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            depthPrepassDesc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            depthPrepassDesc.DebugName = "Renderer.DepthPrepass";
            auto depthPipeline = m_PipelineManager->Create(depthPrepassDesc);
            if (depthPipeline.has_value())
            {
                m_DepthPrepassPipelineLease.emplace(std::move(*depthPipeline));
                m_DepthPrepassPass.SetPipeline(
                    m_PipelineManager->GetDeviceHandle(m_DepthPrepassPipelineLease->GetHandle()));
            }
            else
            {
                Core::Log::Warn("[Graphics] DepthPrepass pipeline unavailable; pass commands will be skipped: error={}",
                                static_cast<int>(depthPipeline.error()));
            }

            // GRAPHICS-031A: canonical missing-material fallback pipeline.
            // Republished byte-identical from BuildDefaultDebugSurfacePipelineDesc()
            // so the descriptor matches across initial init and rebuilds.
            // GRAPHICS-032B: the MinimalDebugSurface pass leases the same slot-0
            // pipeline so its recorded command stream matches the
            // default-debug-surface pipeline byte-for-byte.
            m_DefaultDebugSurfacePipelineLease.reset();
            m_MinimalDebugPresentPipelineLease.reset();
            m_MinimalDebugSurfacePass.SetPipeline(RHI::PipelineHandle{});
            m_MinimalDebugPresentPass.SetPipeline(RHI::PipelineHandle{});
            const RHI::PipelineDesc defaultDebugSurfaceDesc = BuildDefaultDebugSurfacePipelineDesc(m_BackbufferFormat);
            auto defaultDebugSurfacePipeline = m_PipelineManager->Create(defaultDebugSurfaceDesc);
            if (defaultDebugSurfacePipeline.has_value())
            {
                m_DefaultDebugSurfacePipelineLease.emplace(std::move(*defaultDebugSurfacePipeline));
                const RHI::PipelineHandle slotZeroPipeline =
                    m_PipelineManager->GetDeviceHandle(m_DefaultDebugSurfacePipelineLease->GetHandle());
                m_MinimalDebugSurfacePass.SetPipeline(slotZeroPipeline);
            }
            else
            {
                Core::Log::Warn("[Graphics] DefaultDebugSurface pipeline unavailable; fallback recording will be skipped: error={}",
                                static_cast<int>(defaultDebugSurfacePipeline.error()));
            }

            const RHI::PipelineDesc minimalVisibleTriangleDesc =
                BuildMinimalVisibleTrianglePipelineDesc(m_BackbufferFormat);
            auto minimalVisibleTrianglePipeline = m_PipelineManager->Create(minimalVisibleTriangleDesc);
            if (minimalVisibleTrianglePipeline.has_value())
            {
                m_MinimalDebugPresentPipelineLease.emplace(std::move(*minimalVisibleTrianglePipeline));
                m_MinimalDebugPresentPass.SetPipeline(
                    m_PipelineManager->GetDeviceHandle(m_MinimalDebugPresentPipelineLease->GetHandle()));
            }
            else
            {
                Core::Log::Warn("[Graphics] MinimalVisibleTriangle pipeline unavailable; present recording will be skipped: error={}",
                                static_cast<int>(minimalVisibleTrianglePipeline.error()));
            }

            return m_CullingOutputAvailable && m_DepthPrepassPipelineLease.has_value() &&
                m_DepthPrepassPipelineLease->IsValid();
        }

        struct ActiveRenderPassDesc
        {
            std::vector<RHI::ColorAttachment> ColorAttachments{};
            RHI::DepthAttachment DepthAttachment{};
            bool HasAttachments = false;
        };

        [[nodiscard]] static ActiveRenderPassDesc BuildActiveRenderPassDesc(
            const CompiledRenderGraph& compiled,
            const std::uint32_t passIndex)
        {
            ActiveRenderPassDesc out{};
            for (const CompiledRenderPassAttachment& attachment : compiled.RenderPassAttachments)
            {
                if (attachment.PassIndex != passIndex || attachment.ResourceIndex >= compiled.TextureHandles.size())
                {
                    continue;
                }

                const RHI::TextureHandle texture = compiled.TextureHandles[attachment.ResourceIndex];
                if (!texture.IsValid())
                {
                    continue;
                }

                if (attachment.IsDepthAttachment)
                {
                    out.DepthAttachment = RHI::DepthAttachment{
                        .Target = texture,
                        .Load = attachment.Load,
                        .Store = attachment.Store,
                        .ClearDepth = 1.0f,
                    };
                    out.HasAttachments = true;
                    continue;
                }

                if (out.ColorAttachments.size() <= attachment.AttachmentIndex)
                {
                    out.ColorAttachments.resize(attachment.AttachmentIndex + 1u);
                }
                out.ColorAttachments[attachment.AttachmentIndex] = RHI::ColorAttachment{
                    .Target = texture,
                    .Load = attachment.Load,
                    .Store = attachment.Store,
                    .ClearR = 0.0f,
                    .ClearG = 0.0f,
                    .ClearB = 0.0f,
                    .ClearA = 1.0f,
                };
                out.HasAttachments = true;
            }
            return out;
        }

        void ResetFrameState()
        {
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
            if (m_MaterialSystem)
            {
                m_MaterialSystem->ResetPerFrameSubstitutionCounters();
            }
            m_HasExtractedRenderWorld = false;
            m_HasPreparedFrame = false;
            m_LastRenderGraphStats = {};
        }

        [[nodiscard]] RHI::CameraUBO BuildCameraUbo(const RenderWorld& world,
                                                    const std::uint32_t frameIndex) const
        {
            RHI::CameraUBO camera{};
            camera.View = world.Camera.View;
            camera.Proj = world.Camera.Projection;
            camera.ViewProj = world.Camera.ViewProjection;
            camera.CameraPosition = glm::vec4{world.Camera.Position, 0.f};
            camera.CameraDirection = glm::vec4{world.Camera.Forward, 0.f};
            camera.ViewportWidth = world.Viewport.Width > 0 ? static_cast<float>(world.Viewport.Width) : 0.f;
            camera.ViewportHeight = world.Viewport.Height > 0 ? static_cast<float>(world.Viewport.Height) : 0.f;
            camera.NearPlane = world.Camera.NearPlane;
            camera.FarPlane = world.Camera.FarPlane;
            camera.FrameIndex = frameIndex;

            if (world.Camera.Valid)
            {
                if (IsInvertibleFiniteMatrix(world.Camera.View))
                {
                    camera.InvView = glm::inverse(world.Camera.View);
                }
                if (IsInvertibleFiniteMatrix(world.Camera.Projection))
                {
                    camera.InvProj = glm::inverse(world.Camera.Projection);
                }
            }

            if (m_LightSystem)
            {
                m_LightSystem->ApplyTo(camera);
            }
            if (m_ShadowSystem)
            {
                m_ShadowSystem->ApplyTo(camera);
            }
            return camera;
        }

        void AccumulateCommandRecordStatus(const std::string_view passName,
                                           const RenderCommandPassStatus status)
        {
            m_LastRenderGraphStats.CommandRecords.Passes.push_back(RenderGraphCommandPassStats{
                .Name = std::string{passName},
                .Status = status,
            });

            switch (status)
            {
            case RenderCommandPassStatus::Recorded:
                ++m_LastRenderGraphStats.CommandRecords.Recorded;
                break;
            case RenderCommandPassStatus::SkippedNonOperational:
                ++m_LastRenderGraphStats.CommandRecords.Skipped;
                ++m_LastRenderGraphStats.CommandRecords.SkippedNonOperational;
                break;
            case RenderCommandPassStatus::SkippedUnavailable:
                ++m_LastRenderGraphStats.CommandRecords.Skipped;
                ++m_LastRenderGraphStats.CommandRecords.SkippedUnavailable;
                break;
            }
        }

        [[nodiscard]] RenderCommandPassStatus RecordCullingPass(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable)
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_CullingSystem->ResetCounters(cmd);
            m_CullingSystem->DispatchCull(cmd, camera, *m_GpuWorld);
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordDepthPrepass(RHI::ICommandContext& cmd,
                                                                 const RHI::CameraUBO& camera,
                                                                 const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            // DepthPrepass consumes the indirect draw output written by the
            // culling pass, so the cached culling output must exist before it
            // records commands.
            if (!m_CullingOutputAvailable || !m_DepthPrepassPipelineLease.has_value() ||
                !m_DepthPrepassPipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_DepthPrepassPass.Execute(cmd, camera, *m_GpuWorld, *m_CullingSystem, frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-032B — minimal-debug-surface CPU-mock command body. The pass
        // shares the GRAPHICS-031A slot-0 default-debug-surface pipeline lease
        // and draws against the SurfaceOpaque cull bucket. Missing prerequisites
        // (slot-0 lease, SurfaceOpaque bucket residency, or GpuWorld scene table)
        // soft-skip to SkippedUnavailable and additionally bump
        // MinimalRecipeMissingPrerequisiteCount so the diagnostic surfaces
        // record-site gaps in addition to the recipe-build-time count.
        [[nodiscard]] RenderCommandPassStatus RecordMinimalDebugSurfacePass(RHI::ICommandContext& cmd,
                                                                            const RHI::CameraUBO& camera,
                                                                            const std::uint32_t frameIndex)
        {
            (void)cmd;
            (void)camera;
            (void)frameIndex;
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }

            const bool pipelineReady = m_DefaultDebugSurfacePipelineLease.has_value() &&
                                       m_DefaultDebugSurfacePipelineLease->IsValid() &&
                                       m_MinimalDebugSurfacePass.GetPipeline().IsValid();
            const bool gpuWorldReady = m_GpuWorld.has_value();
            // BUG-009: the surface pass records `DrawIndexedIndirectCount`
            // against the SurfaceOpaque bucket buffers, which are populated by
            // `RecordCullingPass`. When the culling pipeline failed to build,
            // `m_CullingOutputAvailable` is false and the culling dispatch is
            // skipped, so the indirect arg/count buffers — even though their
            // handles remain allocated by `CullingSystem::AllocateGpuBuffers`
            // — are never written this frame. Gate the minimal-recipe surface
            // pass on the same `m_CullingOutputAvailable` flag used by
            // `RecordCullingPass`/`RecordDepthPrepass` so the prerequisite
            // check matches the live culling-output contract.
            bool bucketReady = false;
            if (m_CullingOutputAvailable && m_CullingSystem.has_value())
            {
                const auto& bucket = m_CullingSystem->GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque);
                bucketReady = bucket.Indexed && bucket.IndexedArgsBuffer.IsValid() &&
                              bucket.CountBuffer.IsValid() && bucket.Capacity > 0u;
            }

            if (!pipelineReady || !gpuWorldReady || !bucketReady)
            {
                ++m_LastRenderGraphStats.MinimalRecipeMissingPrerequisiteCount;
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            ++m_LastRenderGraphStats.MinimalSurfacePassExecutions;
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-033D prerequisite: once MinimalDebug records against real
        // Vulkan dynamic rendering, the finalizer must not issue the older
        // CPU-mock `Draw(3)` with the BDA default-debug-surface pipeline. That
        // pipeline requires the same scene-table push constants and indexed
        // reference-triangle inputs as `Pass.Surface.MinimalDebug`; replay that
        // parameterized draw body into the backbuffer render pass so the smoke
        // has a legal visible-triangle producer until GRAPHICS-081 removes the
        // scaffold.
        [[nodiscard]] RenderCommandPassStatus RecordMinimalDebugPresentPass(RHI::ICommandContext& cmd,
                                                                            const RHI::CameraUBO& camera,
                                                                            const std::uint32_t frameIndex)
        {
            (void)camera;
            (void)frameIndex;
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }

            const bool pipelineReady = m_MinimalDebugPresentPipelineLease.has_value() &&
                                       m_MinimalDebugPresentPipelineLease->IsValid() &&
                                       m_MinimalDebugPresentPass.GetPipeline().IsValid();
            if (!pipelineReady)
            {
                ++m_LastRenderGraphStats.MinimalRecipeMissingPrerequisiteCount;
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_MinimalDebugPresentPass.Execute(cmd);
            ++m_LastRenderGraphStats.MinimalPresentPassExecutions;
            return RenderCommandPassStatus::Recorded;
        }

        std::optional<RHI::BufferManager>   m_BufferManager;
        std::optional<RHI::SamplerManager>  m_SamplerManager;
        std::optional<RHI::TextureManager>  m_TextureManager;
        std::optional<RHI::PipelineManager> m_PipelineManager;
        RHI::Format                          m_BackbufferFormat{RHI::Format::RGBA8_UNORM};
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
        DepthPrepassPass                     m_DepthPrepassPass;
        MinimalDebugSurfacePass              m_MinimalDebugSurfacePass;
        MinimalDebugPresentPass              m_MinimalDebugPresentPass;
        std::optional<RHI::PipelineManager::PipelineLease> m_DepthPrepassPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_DefaultDebugSurfacePipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_MinimalDebugPresentPipelineLease;
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
        bool                                 m_CullingOutputAvailable{false};
        bool                                 m_HasExtractedRenderWorld{false};
        bool                                 m_HasPreparedFrame{false};
        Core::Config::FrameRecipeKind        m_FrameRecipe{Core::Config::FrameRecipeKind::Default};
        // GRAPHICS-033D — opt-in readback target wired by the smoke fixture
        // through SetMinimalDebugBackbufferReadbackBuffer(). Invalid handle =
        // readback disabled (default), so the executor's standard
        // ColorAttachment → Present transition is unchanged for every other
        // caller. Retired together with the MinimalDebug recipe by
        // GRAPHICS-081.
        RHI::BufferHandle                    m_MinimalDebugReadbackBuffer{};
        RenderGraphFrameStats                m_LastRenderGraphStats;
    };

    std::unique_ptr<IRenderer> CreateRenderer()
    {
        return std::make_unique<NullRenderer>();
    }
}
