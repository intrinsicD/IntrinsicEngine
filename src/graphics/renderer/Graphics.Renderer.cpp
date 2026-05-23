module;

#include <array>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
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
import Extrinsic.Graphics.Pass.Deferred.GBuffers;
import Extrinsic.Graphics.Pass.Deferred.Lighting;
import Extrinsic.Graphics.Pass.Forward.Surface;
import Extrinsic.Graphics.Pass.Forward.Line;
import Extrinsic.Graphics.Pass.Forward.Point;
import Extrinsic.Graphics.Pass.Shadows;
import Extrinsic.Graphics.Pass.Selection.EntityId;
import Extrinsic.Graphics.Pass.Selection.FaceId;
import Extrinsic.Graphics.Pass.Selection.EdgeId;
import Extrinsic.Graphics.Pass.Selection.PointId;
import Extrinsic.Graphics.Pass.Selection.Outline;
import Extrinsic.Graphics.Pass.PostProcess.Bloom;
import Extrinsic.Graphics.Pass.PostProcess.FXAA;
import Extrinsic.Graphics.Pass.PostProcess.Histogram;
import Extrinsic.Graphics.Pass.PostProcess.SMAA;
import Extrinsic.Graphics.Pass.PostProcess.ToneMap;
import Extrinsic.Graphics.Pass.Surface.MinimalDebug;
import Extrinsic.Graphics.Pass.Present.MinimalDebug;
import Extrinsic.Graphics.Pass.Present;
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
        // GRAPHICS-075 Slice E.2 — `Histogram.Readback` per-slot size in
        // bytes. Matches the recipe-side `PostProcess.Histogram` buffer
        // declaration (`256 * sizeof(std::uint32_t)`) and the GPU shader's
        // `bins[256]` storage block (`assets/shaders/post_histogram.comp`).
        // The renderer-owned host-visible readback buffer holds
        // `kHistogramReadbackSlotBytes * frames-in-flight` bytes total so
        // each in-flight frame copies into its own slot without aliasing.
        constexpr std::uint64_t kHistogramReadbackSlotBytes =
            256ull * sizeof(std::uint32_t);

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
            m_LightSystem    .emplace();
            m_LightSystem->Initialize();
            m_SelectionSystem.emplace();
            m_SelectionSystem->Initialize();
            // GRAPHICS-074 Slice A — `EntityIdPass` is selection-system-bound
            // and consumes the `SurfaceOpaque` cull bucket via
            // `EntityIdPass::Execute(...)`. The pass must be emplaced before
            // the operational publisher creates the EntityId selection
            // pipeline and calls `SetPipeline(...)` — same publisher-before-
            // first-frame invariant as the forward / shadow / deferred passes
            // below — otherwise the first frame would observe a `has_value()`
            // lease but a default-constructed pipeline handle on the pass
            // itself, and `Execute()` would early-return on
            // `!m_Pipeline.IsValid()` while the executor still reported
            // `Recorded`.
            m_SelectionEntityIdPass.emplace(*m_SelectionSystem);
            // GRAPHICS-074 Slice B — Face/Edge/Point ID selection passes
            // share the same publisher-before-first-frame invariant as the
            // EntityId pass above: each is emplaced here so the operational
            // publisher can `SetPipeline(...)` on the pass instance before
            // any executor branch reaches `Execute(...)`. Same fail-closed
            // semantics: `has_value()` lease + default pipeline handle on
            // the pass would silently early-return inside `Execute()` while
            // the executor still reported `Recorded`.
            m_SelectionFaceIdPass.emplace(*m_SelectionSystem);
            m_SelectionEdgeIdPass.emplace(*m_SelectionSystem);
            m_SelectionPointIdPass.emplace(*m_SelectionSystem);
            // GRAPHICS-074 Slice C — `SelectionOutlinePass` is selection-system-
            // bound and renders a fullscreen quad into `SelectionOutline`. Same
            // publisher-before-first-frame invariant as the other selection
            // passes above: emplaced here so the operational publisher can
            // `SetPipeline(...)` on the pass instance before any executor
            // branch reaches `Execute(...)`. `Execute()` early-returns when
            // the pipeline handle or `SelectionSystem` is not initialised, so
            // a missing pipeline yields `SkippedUnavailable` on the executor
            // taxonomy rather than a silently-recorded no-op.
            m_SelectionOutlinePass.emplace(*m_SelectionSystem);
            m_ForwardSystem.emplace();
            m_ForwardSystem->Initialize();
            // GRAPHICS-070/071/073 — default-recipe forward surface/line/point/
            // shadow passes own their system-bound instances plus pipeline
            // leases created from `InitializeOperationalPassResources()`. The
            // passes must be emplaced before that publisher runs so the
            // initial operational `Initialize()` path can call
            // `SetPipeline(...)` on each pass — otherwise the first frame
            // would see a `has_value()` lease but a default-constructed
            // pipeline handle on the pass itself, and `Execute()` would
            // early-return on `!m_Pipeline.IsValid()` while the executor still
            // reported `Recorded`. Pipeline leases + `SetPipeline()` are
            // routed through the same code path on
            // `RebuildOperationalResources()` so the post-operational-
            // transition reset (GRAPHICS-018R) republishes them byte-identical.
            m_ForwardSurfacePass.emplace(*m_ForwardSystem);
            m_ForwardLinePass.emplace(*m_ForwardSystem);
            m_ForwardPointPass.emplace(*m_ForwardSystem);
            // GRAPHICS-073 Slice A — ShadowSystem must be live before the
            // operational publisher creates the depth-only shadow pipeline
            // and calls `SetPipeline(...)` on `m_ShadowPass`.
            // GRAPHICS-073 Slice B — ShadowSystem now owns the depth atlas +
            // `sampler2DShadow`-bindable sampler. The managers are emplaced
            // earlier in this function (line 207), so the system can hold
            // long-lived references and lazily allocate the atlas when
            // `SetParams(...)` enables shadows. The atlas is *not* reallocated
            // by `RebuildOperationalResources()` so the imported handle stays
            // byte-identical across rebuilds.
            m_ShadowSystem.emplace();
            m_ShadowSystem->Initialize(device, *m_TextureManager, *m_SamplerManager);
            m_ShadowPass.emplace(*m_ShadowSystem);
            // GRAPHICS-072 Slice A — DeferredSystem and its `DeferredGBufferPass`
            // must be live before the operational publisher runs so the
            // initial `Initialize()` path can call `SetPipeline(...)` on the
            // GBuffer pass. The same invariant the forward / shadow passes
            // follow above: `has_value()` lease but an unset pipeline handle
            // on the pass would silently early-return inside `Execute()` while
            // the executor still reported `Recorded`.
            m_DeferredSystem.emplace();
            m_DeferredSystem->Initialize();
            m_DeferredGBufferPass.emplace(*m_DeferredSystem);
            // GRAPHICS-072 Slice B — DeferredLightingPass must be live before
            // the operational publisher runs so the initial `Initialize()`
            // path can call `SetPipeline(...)` on the lighting pass. Same
            // invariant as the GBuffer pass above: `has_value()` lease but an
            // unset pipeline handle on the pass would silently early-return
            // inside `Execute()` while the executor still reported `Recorded`.
            // Slice C: the pass also takes the `ShadowSystem&` so
            // `Execute(...)` can publish the atlas bindless index through the
            // pushed `DeferredLightingPushConstants::ShadowAtlasBindlessIndex`
            // field. The system has already been emplaced + Initialize'd
            // above (m_ShadowSystem), so the reference is live before the
            // operational publisher runs.
            m_DeferredLightingPass.emplace(*m_DeferredSystem, *m_ShadowSystem);
            // GRAPHICS-075 Slice A — `PostProcessSystem` must be live before
            // the operational publisher runs so the initial `Initialize()`
            // path can call `SetPipeline(...)` on `m_PostProcessToneMapPass`.
            // Same invariant the forward / shadow / deferred / selection
            // passes follow above: a `has_value()` lease but a default-
            // constructed pipeline handle on the pass would silently early-
            // return inside `PostProcessToneMapPass::Execute()` while the
            // executor still reported `Recorded`.
            //
            // GRAPHICS-075 Slice D.2b — uses the device-aware Initialize
            // overload so PostProcessSystem allocates + uploads its retained
            // SMAA `AreaTex` / `SearchTex` LUT textures and the exposure-
            // adaptation history buffer up-front when the device is
            // operational. The overload is idempotent and no-ops when the
            // device is non-operational; the RebuildOperationalResources()
            // path below re-invokes it so a device that becomes operational
            // later picks up the allocation without a Shutdown()+Initialize()
            // round-trip.
            m_PostProcessSystem.emplace();
            m_PostProcessSystem->Initialize(device, *m_TextureManager, *m_BufferManager);
            m_PostProcessToneMapPass.emplace(*m_PostProcessSystem);
            // GRAPHICS-075 Slice B.1 — same lifetime contract as the
            // tonemap pass above: emplace after `m_PostProcessSystem` is
            // initialised and before the operational publisher runs, so
            // the initial `Initialize()` path can call
            // `SetDownsamplePipeline(...)` / `SetUpsamplePipeline(...)` on
            // `m_PostProcessBloomPass`.
            m_PostProcessBloomPass.emplace(*m_PostProcessSystem);
            // GRAPHICS-075 Slice C — same lifetime contract as the bloom
            // pass above: emplace after `m_PostProcessSystem` is
            // initialised and before the operational publisher runs, so
            // the initial `Initialize()` path can call `SetPipeline(...)`
            // on `m_PostProcessFXAAPass`. The FXAA leg is gated by
            // `PostProcessSettings::AntiAliasing == FXAA` inside the pass
            // body (which `IsStageEnabled` already enforces); the helper
            // still reports `Recorded` under the umbrella's accumulator
            // when the stage is disabled, mirroring the bloom helper's
            // "structurally-recorded no-op" taxonomy.
            m_PostProcessFXAAPass.emplace(*m_PostProcessSystem);
            // GRAPHICS-075 Slice D.2a — SMAA pass shares the same lifetime
            // contract as the bloom + FXAA passes above. Mutually
            // exclusive with FXAA per `PostProcessSettings::AntiAliasing`;
            // `IsStageEnabled(SMAA)` short-circuits the per-stage Execute
            // calls to no-op when AA is `None` or `FXAA`, while the
            // per-stage umbrella helpers still report `Recorded` under
            // their `"PostProcessAA{Edge,Blend,Resolve}Pass"`
            // accumulators. Per-stage pipeline leases (edge / blend /
            // resolve) are bound in `InitializeOperationalPassResources`.
            m_PostProcessSMAAPass.emplace(*m_PostProcessSystem);
            // GRAPHICS-075 Slice E.1 — same lifetime contract as the
            // tonemap + bloom + FXAA + SMAA passes above; emplaced after
            // `m_PostProcessSystem` is initialised and before the
            // operational publisher runs so the initial `Initialize()`
            // path can call `SetPipeline(...)` on
            // `m_PostProcessHistogramPass`.
            m_PostProcessHistogramPass.emplace(*m_PostProcessSystem);
            if (device.IsOperational())
            {
                [[maybe_unused]] const bool passResourcesReady = InitializeOperationalPassResources(device);
            }
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

            // GRAPHICS-075 Slice D.2b — re-invoke the device-aware
            // PostProcessSystem initializer to cover the case where the
            // device was non-operational at first Initialize() and has
            // since become operational. The overload is idempotent — the
            // retained SMAA LUT + exposure-history leases survive
            // RebuildOperationalResources() byte-identical when they were
            // already allocated.
            if (m_PostProcessSystem.has_value())
            {
                m_PostProcessSystem->Initialize(device, *m_TextureManager, *m_BufferManager);
            }

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

            // GRAPHICS-070/071/072/073/074 — drop forward, deferred GBuffer,
            // shadow, and EntityId selection passes before resetting their
            // system dependencies below so optional destructors do not observe
            // a dangling reference.
            m_ForwardSurfacePass.reset();
            m_ForwardLinePass.reset();
            m_ForwardPointPass.reset();
            m_ShadowPass.reset();
            m_DeferredGBufferPass.reset();
            m_DeferredLightingPass.reset();
            m_SelectionEntityIdPass.reset();
            m_SelectionFaceIdPass.reset();
            m_SelectionEdgeIdPass.reset();
            m_SelectionPointIdPass.reset();
            m_SelectionOutlinePass.reset();
            // GRAPHICS-075 Slice A — reset the tonemap pass before its system
            // dependency below so the optional destructor does not observe a
            // dangling `PostProcessSystem&`. Same lifetime contract as the
            // selection / forward / deferred / shadow passes above.
            m_PostProcessToneMapPass.reset();
            // GRAPHICS-075 Slice B.1 — bloom pass shares the same lifetime
            // contract as the tonemap pass above.
            m_PostProcessBloomPass.reset();
            // GRAPHICS-075 Slice C — FXAA pass shares the same lifetime
            // contract as the bloom + tonemap passes above.
            m_PostProcessFXAAPass.reset();
            // GRAPHICS-075 Slice D.1 — SMAA pass shares the same lifetime
            // contract as the bloom + tonemap + FXAA passes above; drop
            // before `m_PostProcessSystem` is reset below so the optional
            // destructor does not observe a dangling reference.
            m_PostProcessSMAAPass.reset();
            // GRAPHICS-075 Slice E.1 — histogram pass shares the same
            // lifetime contract as the SMAA / FXAA / bloom / tonemap
            // passes above; drop before `m_PostProcessSystem` is reset
            // below so the optional destructor does not observe a
            // dangling reference.
            m_PostProcessHistogramPass.reset();
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
            // GRAPHICS-076 Slice A — drop the canonical default-recipe
            // present pipeline lease alongside the MinimalDebug present
            // lease above; same teardown ordering contract (lease reset
            // before `m_PipelineManager` is destroyed below).
            m_PresentPipelineLease.reset();
            m_ForwardSurfacePipelineLease.reset();
            m_ForwardLinePipelineLease.reset();
            m_ForwardPointPipelineLease.reset();
            m_ShadowPipelineLease.reset();
            m_DeferredGBufferPipelineLease.reset();
            m_DeferredLightingPipelineLease.reset();
            m_SelectionEntityIdPipelineLease.reset();
            m_SelectionFaceIdPipelineLease.reset();
            m_SelectionEdgeIdPipelineLease.reset();
            m_SelectionPointIdPipelineLease.reset();
            m_SelectionOutlinePipelineLease.reset();
            m_PostProcessToneMapPipelineLease.reset();
            // GRAPHICS-075 Slice B.1 — drop the bloom pipeline leases
            // alongside the tonemap lease before the BufferManager /
            // PipelineManager are torn down below.
            m_PostProcessBloomDownsamplePipelineLease.reset();
            m_PostProcessBloomUpsamplePipelineLease.reset();
            // GRAPHICS-075 Slice C — drop the FXAA pipeline lease alongside
            // the tonemap + bloom leases above; same teardown ordering
            // contract.
            m_PostProcessFXAAPipelineLease.reset();
            // GRAPHICS-075 Slice D.1 — drop the three SMAA pipeline leases
            // alongside the FXAA lease above; same teardown ordering
            // contract. The leases must reset before
            // `m_PipelineManager` is torn down below since the lease
            // destructor calls back through the manager.
            m_PostProcessSMAAEdgePipelineLease.reset();
            m_PostProcessSMAABlendPipelineLease.reset();
            m_PostProcessSMAAResolvePipelineLease.reset();
            // GRAPHICS-075 Slice E.1 — drop the histogram pipeline lease
            // alongside the SMAA leases above; same teardown ordering
            // contract. The lease must reset before `m_PipelineManager`
            // is torn down below since the lease destructor calls back
            // through the manager.
            m_PostProcessHistogramPipelineLease.reset();
            // GRAPHICS-074 Slice D.1 — drop the renderer-owned
            // `Picking.Readback` lease before the BufferManager is torn
            // down so the lease's destructor still observes a live manager
            // (the manager's `Release` path calls `IDevice::DestroyBuffer`).
            m_PickingReadbackBuffer.reset();
            m_PickingReadbackBufferSize = 0u;
            // GRAPHICS-074 Slice D.3 — drop the per-slot picking metadata
            // alongside the buffer so a later `Initialize(device)` allocates
            // fresh bookkeeping against the new BufferManager. Pending
            // readbacks are simply discarded (the `SelectionSystem` is also
            // about to be torn down on the line above).
            m_PickingSlotPending.clear();
            m_PickingSlotIssuedFrame.clear();
            m_PickingSlotRequest.clear();
            m_PickingSlotInvalidated.clear();
            // GRAPHICS-075 Slice E.2 — drop the renderer-owned
            // `Histogram.Readback` lease + per-slot metadata before the
            // BufferManager is torn down, mirroring the picking pattern.
            m_HistogramReadbackBuffer.reset();
            m_HistogramReadbackBufferSize = 0u;
            m_HistogramSlotPending.clear();
            m_HistogramSlotIssuedFrame.clear();
            m_HistogramSlotInvalidated.clear();
            m_MinimalDebugSurfacePass.SetPipeline(RHI::PipelineHandle{});
            m_MinimalDebugPresentPass.SetPipeline(RHI::PipelineHandle{});
            // GRAPHICS-076 Slice A — zero the canonical present pass's
            // cached pipeline handle alongside the MinimalDebug present
            // pass above so a later `Initialize(device)` starts from a
            // clean fail-closed state instead of inheriting a stale
            // device handle from a previous operational lifecycle.
            m_PresentPass.SetPipeline(RHI::PipelineHandle{});
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
            // GRAPHICS-074 Slice D.3 — drain completed picking-readback slots
            // before acquiring the next frame. We use
            // `IDevice::GetGlobalFrameNumber()` (the post-EndFrame counter)
            // as the "completed-frame number" proxy: stub/null backends
            // complete GPU work synchronously inside `EndFrame(...)` so any
            // slot whose `IssuedFrame < GlobalFrameNumber` has flushed; real
            // async backends (Vulkan) signal post-submit fences out of band
            // and a follow-up task may specialise this check via a
            // dedicated `GetCompletedFrameNumber()` / `HasFrameCompleted()`
            // IDevice seam once that lifecycle is plumbed. Each drained
            // slot decodes one 4-byte `EntityId` + one 4-byte
            // `EncodedSelectionId` word at `slot * 8` per `GRAPHICS-012Q`
            // and is routed to the `SelectionSystem` via
            // `PublishPickResult` (live hit) or `PublishNoHit` (zero
            // EntityId, invalidated request, or read failure).
            DrainCompletedPickingSlots();
            // GRAPHICS-075 Slice E.2 — drain completed histogram-readback
            // slots before acquiring the next frame. Mirrors the picking
            // drain pattern: uses `IDevice::GetGlobalFrameNumber()` as the
            // completed-frame proxy (the post-EndFrame counter), decodes
            // the 256 uint32 bins each slot copied, and forwards them to
            // `PostProcessSystem::PublishHistogramReadback(...)`. Slots
            // flagged `Invalidated` (e.g. by a `RebuildOperationalResources()`
            // device-lost recovery) are released without publishing so the
            // exposure-history mirror is never anchored to stale
            // pre-rebuild bytes.
            DrainCompletedHistogramSlots();
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
                // GRAPHICS-073 Slice B — when `ShadowSystem` has lazily
                // allocated its atlas (after `SetParams` enabled shadows),
                // hand the handle to the recipe so the imported atlas
                // replaces the Slice A transient `graph.CreateTexture(...)`
                // path. Stays invalid until the runtime publishes shadows
                // enabled, which keeps default-CPU/null fixtures on the
                // transient fallback.
                .ShadowAtlas = m_ShadowSystem ? m_ShadowSystem->GetAtlasTexture() : RHI::TextureHandle{},
                // GRAPHICS-074 Slice D.2 — hand the renderer-owned host-
                // visible `Picking.Readback` lease to the recipe so it is
                // imported (with `TransferDst → HostReadback`) rather than
                // allocated as a transient. The handle is invalid until
                // Slice D.1's operational publisher has run, which lines up
                // with `pickingActive` requiring an operational device.
                .PickingReadback = (m_PickingReadbackBuffer.has_value() && m_PickingReadbackBuffer->IsValid())
                                       ? m_PickingReadbackBuffer->GetHandle()
                                       : RHI::BufferHandle{},
                // GRAPHICS-075 Slice E.2 — hand the renderer-owned host-
                // visible `Histogram.Readback` lease to the recipe so the
                // executor can record `CopyBuffer(PostProcess.Histogram →
                // Histogram.Readback @ slot * 1024)` after the histogram
                // dispatch. The handle is invalid until the operational
                // publisher has run; `BuildDefaultFrameRecipe` falls back to
                // skipping the import + readback write when the handle is
                // not valid yet.
                .HistogramReadback = (m_HistogramReadbackBuffer.has_value() && m_HistogramReadbackBuffer->IsValid())
                                         ? m_HistogramReadbackBuffer->GetHandle()
                                         : RHI::BufferHandle{},
            };
            const FrameRecipeSizing sizing{
                .Width = renderWorld.Viewport.Width > 0 ? static_cast<std::uint32_t>(renderWorld.Viewport.Width) : 1u,
                .Height = renderWorld.Viewport.Height > 0 ? static_cast<std::uint32_t>(renderWorld.Viewport.Height) : 1u,
                .BackbufferFormat = m_BackbufferFormat,
                .DepthFormat = RHI::Format::D32_FLOAT,
            };
            // GRAPHICS-073 Slice B — derive the typed shadow sizing from the
            // current `ShadowSystem` params so transient fallbacks (no atlas
            // imported) still size the recipe-owned atlas per
            // `ShadowParams::AtlasResolution * CascadeCount`. When the atlas
            // is imported, `BuildDefaultFrameRecipe` ignores this sizing and
            // honors the imported handle's dimensions.
            FrameRecipeShadowSizing shadowSizing{};
            if (m_ShadowSystem)
            {
                const ShadowParams shadowParams = m_ShadowSystem->GetParams();
                shadowSizing.AtlasResolution = shadowParams.AtlasResolution;
                shadowSizing.CascadeCount = shadowParams.CascadeCount;
            }
            // GRAPHICS-070 — derive default-recipe features once per frame so
            // the executor lambda below can route `"SurfacePass"` through the
            // forward or deferred surface body without re-deriving features
            // for every pass dispatch. The `MinimalDebug` recipe path keeps
            // its existing zero-feature fixed structure.
            // GRAPHICS-072 Slice A — apply the renderer-stored lighting-path
            // override after the per-world derivation so contract tests can
            // drive the deferred surface/composition branches without
            // re-deriving features at the call site. `DeriveDefaultFrameRecipeFeatures`
            // returns `Forward` by default; the override flips it to
            // `Deferred` / `Hybrid` when set via `SetLightingPath(...)`.
            FrameRecipeFeatures defaultRecipeFeatures = DeriveDefaultFrameRecipeFeatures(renderWorld);
            defaultRecipeFeatures.LightingPath = m_LightingPath;
            // GRAPHICS-075 Slice D.2a — flip `presentSource` to
            // `PostProcess.AATemp.Resolved` only when the postprocess
            // system reports a non-`None` AA selector *and* the selected
            // mode's pipeline(s) are actually available. Plumbing only on
            // the selector (`AntiAliasing != None`) would route present
            // to `AATemp.Resolved` even when the matching pipeline
            // failed to build — both AA pass bodies would short-circuit
            // (FXAA needs its pipeline; SMAA needs its three) and
            // present would consume the cleared / undefined resolved
            // attachment instead of the freshly-written `SceneColorLDR`.
            // For SMAA we require all three pipelines because the
            // resolve shader reads `AATemp.Weights`: if blend (or edge)
            // is missing the resolve still draws but reads cleared
            // inputs, so we treat AA as unavailable for present-routing
            // purposes and fall back to `SceneColorLDR`. The resolve
            // helper itself mirrors this gate so its
            // `RenderCommandPassStatus` faithfully reports
            // `SkippedUnavailable` when the mode's pipeline is missing.
            // `DeriveDefaultFrameRecipeFeatures` itself does not see
            // `PostProcessSettings` (it is renderer-internal state, not
            // a `RenderWorld` field), so plumb the flag here.
            defaultRecipeFeatures.EnableAntiAliasing =
                m_PostProcessSystem.has_value() &&
                SelectedAntiAliasingPipelinesAvailable();
            const FrameRecipeBuildResult recipe = (m_FrameRecipe == Core::Config::FrameRecipeKind::MinimalDebug)
                ? BuildMinimalDebugSurfaceRecipe(m_RenderGraph, imports, sizing)
                : BuildDefaultFrameRecipe(m_RenderGraph,
                                          defaultRecipeFeatures,
                                          imports,
                                          sizing,
                                          shadowSizing);
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
                    : DescribeDefaultFrameRecipe(defaultRecipeFeatures);
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
            // GRAPHICS-070 — when the active recipe is the default recipe the
            // executor consults `usesDeferred` to choose between the forward
            // surface body (this task) and the deferred GBuffer body
            // (GRAPHICS-072 future scope). Mirrors the anonymous-namespace
            // `UsesDeferredResources()` predicate inside
            // `Graphics.FrameRecipe.cpp`: any non-forward lighting path uses
            // deferred resources. The `MinimalDebug` recipe never declares
            // `"SurfacePass"`, so this flag is unused there.
            const bool defaultRecipeUsesDeferred =
                (m_FrameRecipe != Core::Config::FrameRecipeKind::MinimalDebug) &&
                (defaultRecipeFeatures.LightingPath != FrameRecipeLightingPath::Forward);
            graphicsContext.Begin();
            const auto executeResult = m_RenderGraphExecutor.Execute(
                *compiled,
                {},
                [this, &graphicsContext, &passNameByIndex, &camera, &frame, &compiled,
                 defaultRecipeUsesDeferred, &renderWorld](const std::uint32_t passIndex)
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

                    // GRAPHICS-074 Slice D.2 — the picking executor branch
                    // needs to end the render pass mid-branch so it can
                    // record the texture-to-buffer copies (which must run
                    // outside any render pass). The outer `endActiveRenderPass()`
                    // at the bottom of the lambda would then double-end, so
                    // we track whether the inner branch already ended it.
                    bool renderPassEnded = false;
                    const auto endActiveRenderPass = [&graphicsContext, &activeRenderPass, &renderPassEnded]
                    {
                        if (!renderPassEnded && activeRenderPass.HasAttachments)
                        {
                            graphicsContext.EndRenderPass();
                            renderPassEnded = true;
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
                    else if (passName == std::string_view{"SurfacePass"} && !defaultRecipeUsesDeferred)
                    {
                        // GRAPHICS-070 — default-recipe forward surface pass.
                        // The deferred mode branch is owned by the next
                        // `else if` below.
                        const RenderCommandPassStatus status =
                            RecordForwardSurfacePass(graphicsContext, camera, frame.FrameIndex);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else if (passName == std::string_view{"SurfacePass"} && defaultRecipeUsesDeferred)
                    {
                        // GRAPHICS-072 Slice A — default-recipe deferred
                        // GBuffer pass.
                        const RenderCommandPassStatus status =
                            RecordDeferredGBufferPass(graphicsContext, camera, frame.FrameIndex);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else if (passName == std::string_view{"CompositionPass"})
                    {
                        // GRAPHICS-072 Slice B — default-recipe deferred
                        // lighting composition. Only declared by
                        // `BuildDefaultFrameRecipe` when `usesDeferred` is
                        // true, so this branch is reached only under the
                        // deferred lighting path. The shadow-atlas binding
                        // (`set 1, binding 1`) is Slice C scope and the
                        // current `DeferredLightingPass::Execute` body only
                        // pushes the 16-byte `SceneTableBDA` block + draws
                        // the fullscreen triangle.
                        const RenderCommandPassStatus status =
                            RecordDeferredLightingPass(graphicsContext, camera, frame.FrameIndex);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else if (passName == std::string_view{"LinePass"})
                    {
                        const RenderCommandPassStatus status =
                            RecordForwardLinePass(graphicsContext, camera, frame.FrameIndex);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else if (passName == std::string_view{"PointPass"})
                    {
                        const RenderCommandPassStatus status =
                            RecordForwardPointPass(graphicsContext, camera, frame.FrameIndex);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else if (passName == std::string_view{"ShadowPass"})
                    {
                        const RenderCommandPassStatus status =
                            RecordShadowPass(graphicsContext, camera, frame.FrameIndex);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else if (passName == std::string_view{"SelectionOutlinePass"})
                    {
                        // GRAPHICS-074 Slice C/D.4 — default-recipe selection
                        // outline route. The recipe declares
                        // `SelectionOutlinePass` only when
                        // `features.EnableSelectionOutline` is true (set from
                        // `world.Selection.HasHovered ||
                        // !world.Selection.SelectedStableIds.empty()` in
                        // `DeriveDefaultFrameRecipeFeatures`), so this branch
                        // is reached only when at least one selectable entity
                        // is present this frame. `RecordSelectionOutlinePass`
                        // mirrors the selection-ID helpers: non-operational
                        // device → `SkippedNonOperational`; missing pass /
                        // lease → `SkippedUnavailable`; otherwise the
                        // fullscreen `Bind/PushConstants/Draw(3,1,0,0)` shape
                        // records and we return `Recorded`. Slice D.4 sources
                        // the `selection_outline.frag` push block from
                        // `renderWorld.Selection` so the shader sees the
                        // seeded hovered/selected ids + outline visual style
                        // instead of the Slice C all-zero placeholder.
                        const RenderCommandPassStatus status =
                            RecordSelectionOutlinePass(graphicsContext, camera, frame.FrameIndex,
                                                       renderWorld.Selection);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else if (passName == std::string_view{"PickingPass"})
                    {
                        // GRAPHICS-074 Slice A + B — default-recipe selection
                        // ID sub-passes. The recipe declares `PickingPass`
                        // only when `features.EnablePicking &&
                        // features.EnableDepthPrepass` is true, so the branch
                        // is reached only when picking is active and a
                        // populated `SceneDepth` exists. The four sub-passes
                        // share the recipe's `PickingPass` render pass and
                        // dispatch back-to-back: EntityId first (so its
                        // `(Entity, 0)` `PrimitiveId` is the fallback when no
                        // sub-element pass covers a pixel), then
                        // Face/Edge/Point — each writes the matching
                        // `EncodeSelectionId(domain, gl_PrimitiveID)` into
                        // `PrimitiveId` over its own cull bucket
                        // (`SurfaceOpaque` / `Lines` / `Points`). With
                        // depth-equal / depth-write-off, only the
                        // nearest-surface fragment per pixel can write, so
                        // the most refined domain code that survives the
                        // prepass depth test wins per pixel. The status is
                        // accumulated under the single `PickingPass` name to
                        // keep the executor's per-pass status taxonomy the
                        // same shape the rest of the recipe uses: any
                        // sub-pass that records bumps the aggregate to
                        // `Recorded`; a sub-pass with a not-yet-ready
                        // pipeline downgrades to `SkippedUnavailable` per
                        // `AccumulateCommandRecordStatus`'s usual rules; a
                        // non-operational device produces
                        // `SkippedNonOperational` uniformly. The
                        // `Picking.Readback` drain +
                        // `PublishPickResult`/`PublishNoHit` wiring remain
                        // Slice D scope.
                        const RenderCommandPassStatus entityStatus =
                            RecordSelectionEntityIdPass(graphicsContext, camera, frame.FrameIndex);
                        AccumulateCommandRecordStatus(passName, entityStatus);
                        const RenderCommandPassStatus faceStatus =
                            RecordSelectionFaceIdPass(graphicsContext, camera, frame.FrameIndex);
                        AccumulateCommandRecordStatus(passName, faceStatus);
                        const RenderCommandPassStatus edgeStatus =
                            RecordSelectionEdgeIdPass(graphicsContext, camera, frame.FrameIndex);
                        AccumulateCommandRecordStatus(passName, edgeStatus);
                        const RenderCommandPassStatus pointStatus =
                            RecordSelectionPointIdPass(graphicsContext, camera, frame.FrameIndex);
                        AccumulateCommandRecordStatus(passName, pointStatus);

                        // GRAPHICS-074 Slice D.2 — picking-readback copy
                        // pair. After the four selection-ID sub-passes have
                        // recorded against the shared `PickingPass` render
                        // pass, copy one pixel each from `EntityId` and
                        // `PrimitiveId` (at the requested pick coordinates)
                        // into the renderer-owned host-visible
                        // `Picking.Readback` buffer at the per-frame slot
                        // (`slot * 8` for `EntityId`, `slot * 8 + 4` for the
                        // `EncodedSelectionId`). The two copies are wrapped
                        // by `ColorAttachment → TransferSrc → ColorAttachment`
                        // transitions per the GRAPHICS-033D
                        // `MinimalDebugReadbackBuffer` pattern, but executed
                        // *outside* the render pass — so we end it first and
                        // let the outer `endActiveRenderPass()` skip via the
                        // `renderPassEnded` latch. The copy is gated on:
                        //   (a) operational device,
                        //   (b) renderer's `Picking.Readback` buffer wired,
                        //   (c) `renderWorld.PickRequest.Pending` (the same
                        //       signal that enabled `EnablePicking` upstream).
                        // When any gate fails, no copy or barriers record so
                        // the per-slot `PickingReadbackCopyCount` counter
                        // accurately distinguishes pending-pick frames from
                        // skipped frames. Slice D.3 drains the buffer on
                        // `BeginFrame()` once the issuing frame completes.
                        if (m_Device != nullptr && m_Device->IsOperational() &&
                            m_PickingReadbackBuffer.has_value() &&
                            m_PickingReadbackBuffer->IsValid() &&
                            renderWorld.PickRequest.Pending)
                        {
                            const RHI::BufferHandle pickingBuffer =
                                m_PickingReadbackBuffer->GetHandle();
                            RHI::TextureHandle entityIdHandle{};
                            RHI::TextureHandle primitiveIdHandle{};
                            for (std::size_t i = 0; i < compiled->TextureNames.size(); ++i)
                            {
                                if (i >= compiled->TextureHandles.size())
                                {
                                    break;
                                }
                                if (compiled->TextureNames[i] == std::string_view{"EntityId"})
                                {
                                    entityIdHandle = compiled->TextureHandles[i];
                                }
                                else if (compiled->TextureNames[i] == std::string_view{"PrimitiveId"})
                                {
                                    primitiveIdHandle = compiled->TextureHandles[i];
                                }
                            }
                            if (entityIdHandle.IsValid() && primitiveIdHandle.IsValid())
                            {
                                endActiveRenderPass();
                                const std::uint32_t framesInFlight =
                                    std::max(1u, m_Device->GetFramesInFlight());
                                const std::uint32_t slot =
                                    frame.FrameIndex % framesInFlight;
                                const std::uint64_t slotOffset =
                                    static_cast<std::uint64_t>(slot) * 8ull;
                                const std::uint32_t pickX = renderWorld.PickRequest.X;
                                const std::uint32_t pickY = renderWorld.PickRequest.Y;

                                graphicsContext.TextureBarrier(entityIdHandle,
                                                                RHI::TextureLayout::ColorAttachment,
                                                                RHI::TextureLayout::TransferSrc);
                                graphicsContext.TextureBarrier(primitiveIdHandle,
                                                                RHI::TextureLayout::ColorAttachment,
                                                                RHI::TextureLayout::TransferSrc);
                                graphicsContext.CopyTextureToBuffer(entityIdHandle,
                                                                     RHI::TextureLayout::TransferSrc,
                                                                     0u, 0u,
                                                                     pickingBuffer,
                                                                     slotOffset,
                                                                     pickX, pickY,
                                                                     1u, 1u);
                                graphicsContext.CopyTextureToBuffer(primitiveIdHandle,
                                                                     RHI::TextureLayout::TransferSrc,
                                                                     0u, 0u,
                                                                     pickingBuffer,
                                                                     slotOffset + 4ull,
                                                                     pickX, pickY,
                                                                     1u, 1u);
                                graphicsContext.TextureBarrier(entityIdHandle,
                                                                RHI::TextureLayout::TransferSrc,
                                                                RHI::TextureLayout::ColorAttachment);
                                graphicsContext.TextureBarrier(primitiveIdHandle,
                                                                RHI::TextureLayout::TransferSrc,
                                                                RHI::TextureLayout::ColorAttachment);
                                ++m_LastRenderGraphStats.PickingReadbackCopyCount;

                                // GRAPHICS-074 Slice D.3 — record the per-
                                // slot metadata the next BeginFrame() drain
                                // keys off. The slot arrays were sized to
                                // `frames-in-flight` in
                                // InitializeOperationalPassResources(), so
                                // `slot` is always within range when we
                                // reach this site (operational device =>
                                // arrays sized). `Invalidated` is reset to
                                // false on issue because the copy we just
                                // recorded supersedes any prior slot
                                // contents; a subsequent
                                // RebuildOperationalResources() may flip it
                                // back to true before the drain runs.
                                if (slot < m_PickingSlotPending.size())
                                {
                                    m_PickingSlotPending[slot] = true;
                                    m_PickingSlotIssuedFrame[slot] = frame.FrameIndex;
                                    m_PickingSlotRequest[slot] = PickPixelRequest{
                                        .X = pickX,
                                        .Y = pickY,
                                        .Pending = true,
                                    };
                                    m_PickingSlotInvalidated[slot] = false;
                                }
                            }
                        }
                    }
                    else if (passName == std::string_view{"PostProcessPass"})
                    {
                        // GRAPHICS-075 Slice A — default-recipe postprocess
                        // umbrella branch. The recipe declares
                        // `"PostProcessPass"` whenever
                        // `features.EnablePostProcess` is true (its current
                        // unconditional default in
                        // `DeriveDefaultFrameRecipeFeatures`). Slice B.1
                        // fans out to Bloom (downsample + upsample) before
                        // ToneMap so the bloom write naturally precedes the
                        // tonemap read of `PostProcess.BloomScratch` in
                        // recorded order. Slice C splits FXAA into its own
                        // ordered graph pass, and Slice D.2a further
                        // splits the AA umbrella into three ordered
                        // passes (`"PostProcessAA{Edge,Blend,Resolve}Pass"`,
                        // see the dedicated executor branches below) so
                        // edge / blend / resolve pipelines can target
                        // format-incompatible color attachments. Slice E
                        // will add the Histogram sub-pass behind this
                        // same `"PostProcessPass"` branch. The status is
                        // accumulated under the single `"PostProcessPass"`
                        // name to keep the executor's per-pass status
                        // taxonomy the same shape the rest of the recipe
                        // uses: any sub-pass that records bumps the
                        // aggregate to `Recorded`; a sub-pass with a
                        // not-yet-ready pipeline downgrades to
                        // `SkippedUnavailable` per
                        // `AccumulateCommandRecordStatus`'s usual rules;
                        // a non-operational device produces
                        // `SkippedNonOperational` uniformly.
                        // GRAPHICS-075 Slice B.2 — resolve the per-frame
                        // `PostProcess.BloomScratch` transient handle from
                        // the compiled graph and republish it to the bloom
                        // pass alongside the *effective* mip-chain depth
                        // (clamped via `ComputeBloomMipChainLevels` against
                        // the current viewport extent — Vulkan rejects
                        // `mipLevels > floor(log2(max(W, H))) + 1`). The
                        // recipe-side `BuildDefaultFrameRecipe` declares
                        // `BloomScratch` with the same helper, so the pass-
                        // side iteration count matches the allocated
                        // texture's actual mip range. The lookup walks the
                        // compiled `TextureNames`/`TextureHandles` parallel
                        // arrays the same way the picking executor route
                        // resolves `EntityId`/`PrimitiveId` upstream. When
                        // the transient resource is absent (e.g. when
                        // `EnablePostProcess = false`) we publish
                        // `TextureHandle{}` + a degenerate single-mip count;
                        // the pass body early-skips the iteration in that
                        // case rather than recording over a missing
                        // attachment.
                        if (m_PostProcessBloomPass.has_value())
                        {
                            RHI::TextureHandle bloomScratchHandle{};
                            for (std::size_t i = 0; i < compiled->TextureNames.size(); ++i)
                            {
                                if (i >= compiled->TextureHandles.size())
                                {
                                    break;
                                }
                                if (compiled->TextureNames[i] == std::string_view{"PostProcess.BloomScratch"})
                                {
                                    bloomScratchHandle = compiled->TextureHandles[i];
                                    break;
                                }
                            }
                            const Core::Extent2D bloomExtent = m_Device != nullptr
                                ? m_Device->GetBackbufferExtent()
                                : Core::Extent2D{.Width = 1, .Height = 1};
                            const std::uint32_t bloomWidth = bloomExtent.Width > 0
                                ? static_cast<std::uint32_t>(bloomExtent.Width)
                                : 1u;
                            const std::uint32_t bloomHeight = bloomExtent.Height > 0
                                ? static_cast<std::uint32_t>(bloomExtent.Height)
                                : 1u;
                            const std::uint32_t bloomMipLevels =
                                ComputeBloomMipChainLevels(bloomWidth, bloomHeight);
                            m_PostProcessBloomPass->SetBloomScratch(bloomScratchHandle, bloomMipLevels);
                        }
                        const RenderCommandPassStatus bloomStatus =
                            RecordPostProcessBloomPass(graphicsContext, camera);
                        AccumulateCommandRecordStatus(passName, bloomStatus);
                        const RenderCommandPassStatus toneMapStatus =
                            RecordPostProcessToneMapPass(graphicsContext, camera);
                        AccumulateCommandRecordStatus(passName, toneMapStatus);
                    }
                    else if (passName == std::string_view{"PostProcessHistogramPass"})
                    {
                        // GRAPHICS-075 Slice E.1 — the histogram compute
                        // dispatch lives in its own ordered graph pass
                        // before `"PostProcessPass"` because Vulkan
                        // forbids `vkCmdDispatch` inside an active
                        // render-pass scope. Publish the backbuffer
                        // extent so the dispatch shape (`ceil(W/16) x
                        // ceil(H/16) x 1`, matching the shader's
                        // `local_size_x = local_size_y = 16` tile) tracks
                        // the runtime viewport rather than the stale
                        // `(1, 1, 1)` the Slice A stub recorded, and
                        // publish the per-frame `PostProcess.Histogram`
                        // transient buffer handle so the pass body can
                        // zero-fill the 256 bins before dispatching
                        // (the shader accumulates via `atomicAdd`, so
                        // without a per-frame clear the transient
                        // allocator's reused contents from prior frames
                        // would contaminate the next frame's luminance
                        // distribution and corrupt Slice E.2's
                        // exposure-adaptation readback). With
                        // `EnableHistogram == false` the pass body
                        // short-circuits and the helper still reports
                        // `Recorded` per the structurally-recorded-no-op
                        // taxonomy bloom / FXAA / SMAA already follow.
                        RHI::BufferHandle histogramHandle{};
                        for (std::size_t i = 0; i < compiled->BufferNames.size(); ++i)
                        {
                            if (i >= compiled->BufferHandles.size())
                            {
                                break;
                            }
                            if (compiled->BufferNames[i] == std::string_view{"PostProcess.Histogram"})
                            {
                                histogramHandle = compiled->BufferHandles[i];
                                break;
                            }
                        }
                        if (m_PostProcessHistogramPass.has_value())
                        {
                            const Core::Extent2D histogramExtent = m_Device != nullptr
                                ? m_Device->GetBackbufferExtent()
                                : Core::Extent2D{.Width = 1, .Height = 1};
                            const std::uint32_t histogramWidth = histogramExtent.Width > 0
                                ? static_cast<std::uint32_t>(histogramExtent.Width)
                                : 1u;
                            const std::uint32_t histogramHeight = histogramExtent.Height > 0
                                ? static_cast<std::uint32_t>(histogramExtent.Height)
                                : 1u;
                            m_PostProcessHistogramPass->SetViewport(histogramWidth, histogramHeight);
                            m_PostProcessHistogramPass->SetHistogramBuffer(histogramHandle);
                        }
                        const RenderCommandPassStatus status =
                            RecordPostProcessHistogramPass(graphicsContext, camera);
                        AccumulateCommandRecordStatus(passName, status);

                        // GRAPHICS-075 Slice E.2 — record the per-frame
                        // `CopyBuffer(PostProcess.Histogram → Histogram.Readback
                        // @ slot * 1024)` after the histogram compute dispatch
                        // so the next frame's `BeginFrame()`-side drain can
                        // decode the 256-bin payload and publish it to
                        // `PostProcessSystem::PublishHistogramReadback(...)`.
                        // The copy is gated on:
                        //   (a) the histogram helper actually recording
                        //       (`status == Recorded` — operational device +
                        //       valid pipeline + populated `PostProcessSystem`),
                        //   (b) the histogram *stage* being live
                        //       (`IsStageEnabled(Histogram)` — the helper
                        //       returns `Recorded` even when the stage is off,
                        //       under the standing "structurally-recorded
                        //       no-op" taxonomy bloom / FXAA / SMAA also
                        //       follow; the pass body early-returns without
                        //       dispatching, so the transient
                        //       `PostProcess.Histogram` buffer is never
                        //       zero-filled or atomically populated this
                        //       frame, and a copy here would publish
                        //       undefined transient-allocator bytes into the
                        //       exposure-history mirror through the next
                        //       drain — corrupting adaptation state even
                        //       though the histogram is disabled),
                        //   (c) the renderer's `Histogram.Readback` lease
                        //       being valid,
                        //   (d) the recipe having compiled a transient
                        //       `PostProcess.Histogram` handle into the graph.
                        // The bracketing `ShaderWrite → TransferRead →
                        // ShaderWrite` buffer barrier pair makes the atomic
                        // accumulations visible to the copy and restores the
                        // shader-write state so downstream consumers of the
                        // histogram buffer (none today; landed under the
                        // exposure-history Slice E.2 plan but consumed by
                        // future GPU-side tonemap iterations) observe valid
                        // state.
                        const bool histogramStageLive =
                            m_PostProcessSystem.has_value() &&
                            m_PostProcessSystem->IsStageEnabled(PostProcessStageKind::Histogram);
                        if (status == RenderCommandPassStatus::Recorded &&
                            histogramStageLive &&
                            m_HistogramReadbackBuffer.has_value() &&
                            m_HistogramReadbackBuffer->IsValid() &&
                            histogramHandle.IsValid())
                        {
                            const RHI::BufferHandle readbackBuffer =
                                m_HistogramReadbackBuffer->GetHandle();
                            const std::uint32_t framesInFlight =
                                std::max(1u, m_Device->GetFramesInFlight());
                            const std::uint32_t slot =
                                frame.FrameIndex % framesInFlight;
                            const std::uint64_t slotOffset =
                                static_cast<std::uint64_t>(slot) *
                                kHistogramReadbackSlotBytes;

                            graphicsContext.BufferBarrier(histogramHandle,
                                                          RHI::MemoryAccess::ShaderWrite,
                                                          RHI::MemoryAccess::TransferRead);
                            graphicsContext.CopyBuffer(histogramHandle,
                                                       readbackBuffer,
                                                       /*srcOffset=*/0u,
                                                       /*dstOffset=*/slotOffset,
                                                       /*sizeBytes=*/kHistogramReadbackSlotBytes);
                            graphicsContext.BufferBarrier(histogramHandle,
                                                          RHI::MemoryAccess::TransferRead,
                                                          RHI::MemoryAccess::ShaderWrite);
                            ++m_LastRenderGraphStats.HistogramReadbackCopyCount;

                            if (slot < m_HistogramSlotPending.size())
                            {
                                m_HistogramSlotPending[slot] = true;
                                m_HistogramSlotIssuedFrame[slot] = frame.FrameIndex;
                                m_HistogramSlotInvalidated[slot] = false;
                            }
                        }
                    }
                    else if (passName == std::string_view{"PostProcessAAEdgePass"})
                    {
                        // GRAPHICS-075 Slice D.2a — the AA umbrella splits
                        // into three ordered graph passes so edge / blend
                        // / resolve pipelines can target format-
                        // incompatible color attachments (`RG8_UNORM` /
                        // `RGBA8_UNORM` / backbuffer). SMAA edge records
                        // here when `AntiAliasing == SMAA`; otherwise the
                        // pass body short-circuits to a no-op and the
                        // helper still returns `Recorded` under the same
                        // "structurally-recorded no-op" taxonomy bloom
                        // and FXAA already follow.
                        const RenderCommandPassStatus status =
                            RecordPostProcessAAEdgePass(graphicsContext, camera);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else if (passName == std::string_view{"PostProcessAABlendPass"})
                    {
                        // GRAPHICS-075 Slice D.2a — SMAA blend records
                        // here when `AntiAliasing == SMAA`; mutually
                        // exclusive with FXAA, which records under the
                        // resolve pass only.
                        const RenderCommandPassStatus status =
                            RecordPostProcessAABlendPass(graphicsContext, camera);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else if (passName == std::string_view{"PostProcessAAResolvePass"})
                    {
                        // GRAPHICS-075 Slice D.2a — both FXAA and SMAA
                        // resolve write `PostProcess.AATemp.Resolved` in
                        // the resolve graph pass. FXAA samples
                        // `SceneColorLDR` directly; SMAA resolve samples
                        // `SceneColorLDR` + `AATemp.Weights`. Both bodies
                        // are gated on `IsStageEnabled` per
                        // `PostProcessSettings::AntiAliasing`, so only
                        // the active mode emits bind/push/draw; the
                        // helper still returns `Recorded` when both
                        // bodies short-circuit (e.g. `AntiAliasing ==
                        // None`).
                        const RenderCommandPassStatus status =
                            RecordPostProcessAAResolvePass(graphicsContext, camera);
                        AccumulateCommandRecordStatus(passName, status);
                    }
                    else if (passName == std::string_view{"Present"})
                    {
                        // GRAPHICS-076 Slice A — default-recipe canonical
                        // present route. The recipe always declares
                        // `"Present"` as the end-of-graph finalization
                        // pass (it reads `FrameRecipe.PresentSource` and
                        // the imported `Backbuffer`), so this branch is
                        // reached every frame in the default recipe.
                        // `RecordPresentPass` mirrors the other default-
                        // recipe helpers' taxonomy: non-operational device
                        // → `SkippedNonOperational`; missing pipeline lease
                        // → `SkippedUnavailable`; otherwise the canonical
                        // fullscreen `BindPipeline + Draw(3, 1, 0, 0)`
                        // shape records and we return `Recorded`. The
                        // MinimalDebug scaffold still routes through the
                        // adjacent branch below so the two present paths
                        // remain textually side-by-side until
                        // `GRAPHICS-081` retires the scaffold.
                        const RenderCommandPassStatus status =
                            RecordPresentPass(graphicsContext);
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

        [[nodiscard]] RHI::PipelineHandle GetForwardSurfacePipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_ForwardSurfacePipelineLease.has_value() ||
                !m_ForwardSurfacePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_ForwardSurfacePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetForwardSurfacePipelineDesc() const noexcept override
        {
            return BuildForwardSurfacePipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetForwardLinePipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_ForwardLinePipelineLease.has_value() ||
                !m_ForwardLinePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_ForwardLinePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetForwardLinePipelineDesc() const noexcept override
        {
            return BuildForwardLinePipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetForwardPointPipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_ForwardPointPipelineLease.has_value() ||
                !m_ForwardPointPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_ForwardPointPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetForwardPointPipelineDesc() const noexcept override
        {
            return BuildForwardPointPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetShadowPipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_ShadowPipelineLease.has_value() ||
                !m_ShadowPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_ShadowPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetShadowPipelineDesc() const noexcept override
        {
            return BuildShadowPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetDeferredGBufferPipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_DeferredGBufferPipelineLease.has_value() ||
                !m_DeferredGBufferPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_DeferredGBufferPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetDeferredGBufferPipelineDesc() const noexcept override
        {
            return BuildDeferredGBufferPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetDeferredLightingPipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_DeferredLightingPipelineLease.has_value() ||
                !m_DeferredLightingPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_DeferredLightingPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetDeferredLightingPipelineDesc() const noexcept override
        {
            return BuildDeferredLightingPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetSelectionEntityIdPipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_SelectionEntityIdPipelineLease.has_value() ||
                !m_SelectionEntityIdPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_SelectionEntityIdPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetSelectionEntityIdPipelineDesc() const noexcept override
        {
            return BuildSelectionEntityIdPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetSelectionFaceIdPipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_SelectionFaceIdPipelineLease.has_value() ||
                !m_SelectionFaceIdPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_SelectionFaceIdPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetSelectionFaceIdPipelineDesc() const noexcept override
        {
            return BuildSelectionFaceIdPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetSelectionEdgeIdPipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_SelectionEdgeIdPipelineLease.has_value() ||
                !m_SelectionEdgeIdPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_SelectionEdgeIdPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetSelectionEdgeIdPipelineDesc() const noexcept override
        {
            return BuildSelectionEdgeIdPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetSelectionPointIdPipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_SelectionPointIdPipelineLease.has_value() ||
                !m_SelectionPointIdPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_SelectionPointIdPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetSelectionPointIdPipelineDesc() const noexcept override
        {
            return BuildSelectionPointIdPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetSelectionOutlinePipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_SelectionOutlinePipelineLease.has_value() ||
                !m_SelectionOutlinePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_SelectionOutlinePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetSelectionOutlinePipelineDesc() const noexcept override
        {
            return BuildSelectionOutlinePipelineDesc(m_BackbufferFormat);
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessToneMapPipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_PostProcessToneMapPipelineLease.has_value() ||
                !m_PostProcessToneMapPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_PostProcessToneMapPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessToneMapPipelineDesc() const noexcept override
        {
            return BuildPostProcessToneMapPipelineDesc(m_BackbufferFormat);
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessBloomDownsamplePipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_PostProcessBloomDownsamplePipelineLease.has_value() ||
                !m_PostProcessBloomDownsamplePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_PostProcessBloomDownsamplePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessBloomDownsamplePipelineDesc() const noexcept override
        {
            return BuildPostProcessBloomDownsamplePipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessBloomUpsamplePipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_PostProcessBloomUpsamplePipelineLease.has_value() ||
                !m_PostProcessBloomUpsamplePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_PostProcessBloomUpsamplePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessBloomUpsamplePipelineDesc() const noexcept override
        {
            return BuildPostProcessBloomUpsamplePipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessFXAAPipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_PostProcessFXAAPipelineLease.has_value() ||
                !m_PostProcessFXAAPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_PostProcessFXAAPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessFXAAPipelineDesc() const noexcept override
        {
            return BuildPostProcessFXAAPipelineDesc(m_BackbufferFormat);
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessSMAAEdgePipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_PostProcessSMAAEdgePipelineLease.has_value() ||
                !m_PostProcessSMAAEdgePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_PostProcessSMAAEdgePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessSMAAEdgePipelineDesc() const noexcept override
        {
            return BuildPostProcessSMAAEdgePipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessSMAABlendPipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_PostProcessSMAABlendPipelineLease.has_value() ||
                !m_PostProcessSMAABlendPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_PostProcessSMAABlendPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessSMAABlendPipelineDesc() const noexcept override
        {
            return BuildPostProcessSMAABlendPipelineDesc();
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessSMAAResolvePipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_PostProcessSMAAResolvePipelineLease.has_value() ||
                !m_PostProcessSMAAResolvePipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_PostProcessSMAAResolvePipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessSMAAResolvePipelineDesc() const noexcept override
        {
            return BuildPostProcessSMAAResolvePipelineDesc(m_BackbufferFormat);
        }

        [[nodiscard]] RHI::PipelineHandle GetPostProcessHistogramPipeline() const noexcept override
        {
            if (!m_PipelineManager.has_value() || !m_PostProcessHistogramPipelineLease.has_value() ||
                !m_PostProcessHistogramPipelineLease->IsValid())
            {
                return RHI::PipelineHandle{};
            }
            return m_PipelineManager->GetDeviceHandle(m_PostProcessHistogramPipelineLease->GetHandle());
        }

        [[nodiscard]] RHI::PipelineDesc GetPostProcessHistogramPipelineDesc() const noexcept override
        {
            return BuildPostProcessHistogramPipelineDesc();
        }

        [[nodiscard]] RHI::BufferHandle GetPickingReadbackBuffer() const noexcept override
        {
            if (!m_PickingReadbackBuffer.has_value() || !m_PickingReadbackBuffer->IsValid())
            {
                return RHI::BufferHandle{};
            }
            return m_PickingReadbackBuffer->GetHandle();
        }

        [[nodiscard]] std::uint64_t GetPickingReadbackBufferSize() const noexcept override
        {
            return m_PickingReadbackBufferSize;
        }

        // GRAPHICS-075 Slice E.2 — renderer-owned host-visible
        // `Histogram.Readback` buffer accessors. Same lazy-allocation
        // pattern as the picking accessors above: an invalid handle / zero
        // size means the operational publisher has not allocated the lease
        // yet (non-operational device, or pre-`Initialize()`).
        [[nodiscard]] RHI::BufferHandle GetHistogramReadbackBuffer() const noexcept override
        {
            if (!m_HistogramReadbackBuffer.has_value() || !m_HistogramReadbackBuffer->IsValid())
            {
                return RHI::BufferHandle{};
            }
            return m_HistogramReadbackBuffer->GetHandle();
        }

        [[nodiscard]] std::uint64_t GetHistogramReadbackBufferSize() const noexcept override
        {
            return m_HistogramReadbackBufferSize;
        }

        void SetLightingPath(FrameRecipeLightingPath path) noexcept override
        {
            m_LightingPath = path;
        }

        [[nodiscard]] FrameRecipeLightingPath GetLightingPath() const noexcept override
        {
            return m_LightingPath;
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

        // GRAPHICS-070 — default-recipe forward surface pipeline descriptor.
        // Mirrors the depth-prepass-on contract from
        // `docs/architecture/rendering-three-pass.md`: surface samples
        // SceneDepth (`Equal` compare) and writes SceneColorHDR without
        // touching depth. Held byte-identical between the initial
        // `Initialize()` and any subsequent `RebuildOperationalResources()`
        // so the pipeline registry/dedupe can return a stable device handle.
        //
        // Shader pairing: `ForwardSurfacePass::Execute()` pushes
        // `RHI::GpuScenePushConstants` (SceneTableBDA / FrameIndex /
        // DrawBucket) and the pipeline layout's `PushConstantSize` matches
        // `sizeof(GpuScenePushConstants)`. The shaders must therefore observe
        // the GpuScene-aware push-constant block and the BDA-only descriptor
        // contract — pairing with the canonical GpuScene shader pair
        // (`forward/default_debug_surface.{vert,frag}`) satisfies both
        // contracts. The legacy `surface.vert/frag` pair predates the
        // GpuScene seam — it declares `mat4 Model` + `PtrPositions`-style
        // push constants plus `set = 0/2/3` descriptor sets — and would
        // either fail Vulkan pipeline-layout validation or read unrelated
        // push-constant bytes. A dedicated lit forward-surface shader is a
        // GRAPHICS-072 follow-up.
        [[nodiscard]] static RHI::PipelineDesc BuildForwardSurfacePipelineDesc() noexcept
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
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Equal;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.ForwardSurface";
            return desc;
        }

        // GRAPHICS-071 — retained line renderables use the default recipe's
        // `LinePass` after the surface pass. Lines load `SceneDepth` and append
        // into `SceneColorHDR`; depth writes stay disabled so surface depth is
        // preserved for later point/selection/postprocess consumers.
        [[nodiscard]] static RHI::PipelineDesc BuildForwardLinePipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/line.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/line.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::LineList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::LessEqual;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = true;
            desc.ColorBlend[0].SrcColorFactor = RHI::BlendFactor::SrcAlpha;
            desc.ColorBlend[0].DstColorFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorBlend[0].SrcAlphaFactor = RHI::BlendFactor::One;
            desc.ColorBlend[0].DstAlphaFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.ForwardLine";
            return desc;
        }

        // GRAPHICS-071 — retained point renderables use the BDA-backed
        // `point.vert` + `point_retained.frag` shader pair. `point_retained` is
        // the canonical retained-renderable variant; transient debug-point
        // expansion stays out-of-scope for GRAPHICS-077.
        [[nodiscard]] static RHI::PipelineDesc BuildForwardPointPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/point.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/point_retained.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::PointList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::LessEqual;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = true;
            desc.ColorBlend[0].SrcColorFactor = RHI::BlendFactor::SrcAlpha;
            desc.ColorBlend[0].DstColorFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorBlend[0].SrcAlphaFactor = RHI::BlendFactor::One;
            desc.ColorBlend[0].DstAlphaFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.ForwardPoint";
            return desc;
        }

        // GRAPHICS-073 Slice A — default-recipe depth-only shadow pipeline.
        // Reuses `shaders/depth_prepass.vert.spv` so the existing GpuScene
        // push-constant block (`SceneTableBDA` / `FrameIndex` / `DrawBucket`)
        // matches `ShadowPass::Execute`. Depth-only: no fragment shader, no
        // color targets, `DepthWriteEnable = true`, `DepthFunc = LessOrEqual`,
        // single `D32_FLOAT` depth target matching the recipe's transient
        // `ShadowAtlas` declaration. A dedicated shadow-depth shader and the
        // `ShadowSystem`-owned atlas/sampler arrive with Slice B per the
        // `GRAPHICS-009Q` decision; the legacy `shaders/shadow_depth.vert`
        // pair pre-dates the GpuScene seam and is deliberately *not*
        // referenced here.
        [[nodiscard]] static RHI::PipelineDesc BuildShadowPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/depth_prepass.vert.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::Back;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = true;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::LessEqual;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 0u;
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.Shadow";
            return desc;
        }

        // GRAPHICS-072 Slice A — default-recipe deferred GBuffer pipeline.
        // Pairs the GpuScene-aware
        // `forward/default_debug_surface.vert.spv` (shared with the forward
        // default-debug-surface pipeline) with a minimal three-RT GBuffer
        // fragment under `deferred/default_debug_gbuffer.frag.spv`. Both
        // declare a `layout(push_constant) ScenePC` block that matches
        // `RHI::GpuScenePushConstants` byte-for-byte, which is what
        // `DeferredGBufferPass::Execute` pushes via `cmd.PushConstants(...)`.
        // The legacy `assets/shaders/surface.vert` + `surface_gbuffer.frag`
        // pair declares the pre-GpuScene `mat4 Model + Ptr*` push block and
        // is deliberately *not* referenced here — feeding `GpuScenePushConstants`
        // bytes into that layout would silently misinterpret
        // `SceneTableBDA` as `mat4 Model` and corrupt every BDA dereference.
        // See `src/graphics/renderer/README.md` ("Shader push-constant
        // compatibility policy") for the parallel forward / line / point /
        // shadow precedents and the explicit policy that prevents this
        // footgun from recurring. Three color targets match the frame
        // recipe's deferred attachment formats: `SceneNormal` (RGBA16F),
        // `Albedo` (RGBA8), `Material0` (RGBA16F). Depth uses the recipe's
        // `SceneDepth` D32_FLOAT. Depth-test on with `DepthOp::Equal`
        // mirrors the forward-surface pipeline because the depth prepass
        // already populated `SceneDepth`.
        [[nodiscard]] static RHI::PipelineDesc BuildDeferredGBufferPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/forward/default_debug_surface.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/deferred/default_debug_gbuffer.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::Back;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Equal;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorBlend[1].Enable = false;
            desc.ColorBlend[2].Enable = false;
            desc.ColorTargetCount = 3u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT; // SceneNormal
            desc.ColorTargetFormats[1] = RHI::Format::RGBA8_UNORM;  // Albedo
            desc.ColorTargetFormats[2] = RHI::Format::RGBA16_FLOAT; // Material0
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.DeferredGBuffer";
            return desc;
        }

        // GRAPHICS-072 Slice B — default-recipe deferred lighting pipeline.
        // Pairs the fullscreen `post_fullscreen.vert.spv` (no vertex inputs,
        // no push constants — just emits a fullscreen triangle and a UV
        // varying) with the GpuScene-aware `deferred/lighting.frag.spv` whose
        // `layout(push_constant, scalar) PushConstants { uint64_t
        // SceneTableBDA; uint _pad0; uint _pad1; }` block matches
        // `DeferredLightingPushConstants` byte-for-byte — what
        // `DeferredLightingPass::Execute` pushes via
        // `cmd.PushConstants(&pc, sizeof(pc))`. The legacy
        // `assets/shaders/deferred_lighting.frag` declares a far larger
        // `Push { mat4 InvViewProj; vec4 ClearColor; ... }` block plus
        // multiple descriptor sets (4 G-buffer samplers + CameraUBO +
        // sampler2DShadow) and would silently truncate / misinterpret the
        // pushed bytes, so it is deliberately *not* referenced here — see
        // `src/graphics/renderer/README.md` ("Shader push-constant
        // compatibility policy") for the policy. Single RGBA16F color
        // target (`SceneColorHDR`); no depth test/write (composition reads
        // depth via shader sampling in a future slice but the pipeline
        // itself runs without a depth attachment). The shadow-atlas
        // descriptor wiring at `set 1, binding 1` is Slice C scope.
        [[nodiscard]] static RHI::PipelineDesc BuildDeferredLightingPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/deferred/lighting.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT; // SceneColorHDR
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = 16u; // sizeof(DeferredLightingPushConstants)
            desc.DebugName = "Renderer.DeferredLighting";
            return desc;
        }

        // GRAPHICS-074 Slice A — default-recipe EntityId selection pipeline.
        // Pairs the GpuScene-aware `selection/entity_id.vert.spv` (reads
        // positions through `GpuScenePushConstants::SceneTableBDA` → instance
        // / dynamic / geometry buffer references and forwards the per-instance
        // stable entity ID as a flat `uint` varying) with the matching
        // `selection/entity_id.frag.spv` (writes two R32_UINT outputs: location
        // 0 = stable entity ID into the `EntityId` target, location 1 =
        // `EncodeSelectionId(SelectionPrimitiveDomain::Entity, 0)` into the
        // `PrimitiveId` target per the GRAPHICS-012Q encoding contract). The
        // legacy `assets/shaders/pick_id.{vert,frag}` declares the pre-GpuScene
        // `mat4 Model + PtrPositions + PtrNormals + PtrAux + uint EntityID`
        // push-constant block and would silently truncate / misinterpret the
        // `RHI::GpuScenePushConstants` bytes that `EntityIdPass::Execute`
        // pushes via `cmd.PushConstants(&pc, sizeof(pc))`, so it is
        // deliberately *not* referenced here — see
        // `src/graphics/renderer/README.md` ("Shader push-constant
        // compatibility policy") for the parallel forward / deferred /
        // shadow precedents. Two color targets match the frame recipe's
        // `PickingPass` attachment formats (`Graphics.FrameRecipe.cpp`,
        // `features.EnablePicking` branch): `EntityId` (R32_UINT) +
        // `PrimitiveId` (R32_UINT).
        //
        // Depth state: `BuildDefaultFrameRecipe` now orders `PickingPass`
        // *after* `DepthPrepass` and declares `Read(SceneDepth, DepthRead)`
        // on the picking pass (GRAPHICS-074 recipe-side follow-up). The
        // framegraph compiler therefore emits a render pass with a
        // `D32_FLOAT` depth attachment in read-only state, so the pipeline
        // mirrors the depth-equal / depth-write-off shape the forward and
        // deferred GBuffer pipelines use against the same depth buffer. The
        // depth-equal test guarantees only the nearest-surface fragment
        // wins each pixel — without it the recipe would last-fragment-win
        // and the Slice D readback drain would return wrong IDs for any
        // pixel covered by more than one draw. The matching recipe gating
        // (`EnablePicking && EnableDepthPrepass`) ensures this pipeline is
        // only requested when a populated `SceneDepth` is available.
        [[nodiscard]] static RHI::PipelineDesc BuildSelectionEntityIdPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/entity_id.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/entity_id.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::Back;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Equal;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorBlend[1].Enable = false;
            desc.ColorTargetCount = 2u;
            desc.ColorTargetFormats[0] = RHI::Format::R32_UINT; // EntityId
            desc.ColorTargetFormats[1] = RHI::Format::R32_UINT; // PrimitiveId
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.SelectionEntityId";
            return desc;
        }

        // GRAPHICS-074 (Slice B) — Face / Edge / Point selection ID
        // pipeline descriptors. Each mirrors the EntityId descriptor's
        // render-pass-compatible shape (two R32_UINT color targets,
        // D32_FLOAT depth target, depth-equal / depth-test-on /
        // depth-write-off) so all four pipelines can be bound inside the
        // same recipe-declared `PickingPass` render pass. They differ
        // only in:
        //   - shader pair (`selection/{face,edge,point}_id.{vert,frag}`),
        //   - primitive topology (TriangleList / LineList / PointList),
        //   - cull mode (Back for faces, None for edges/points; mirrors
        //     `BuildForwardLinePipelineDesc` / `BuildForwardPointPipelineDesc`),
        //   - debug name.
        // The shader-side `EncodeSelectionId(domain, payload)` differs per
        // pipeline and lives in each fragment shader, not the descriptor.
        [[nodiscard]] static RHI::PipelineDesc BuildSelectionFaceIdPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/face_id.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/face_id.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::Back;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Equal;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorBlend[1].Enable = false;
            desc.ColorTargetCount = 2u;
            desc.ColorTargetFormats[0] = RHI::Format::R32_UINT; // EntityId
            desc.ColorTargetFormats[1] = RHI::Format::R32_UINT; // PrimitiveId
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.SelectionFaceId";
            return desc;
        }

        [[nodiscard]] static RHI::PipelineDesc BuildSelectionEdgeIdPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/edge_id.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/edge_id.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::LineList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Equal;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorBlend[1].Enable = false;
            desc.ColorTargetCount = 2u;
            desc.ColorTargetFormats[0] = RHI::Format::R32_UINT; // EntityId
            desc.ColorTargetFormats[1] = RHI::Format::R32_UINT; // PrimitiveId
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.SelectionEdgeId";
            return desc;
        }

        [[nodiscard]] static RHI::PipelineDesc BuildSelectionPointIdPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/point_id.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection/point_id.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::PointList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = true;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.DepthFunc = RHI::DepthOp::Equal;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorBlend[1].Enable = false;
            desc.ColorTargetCount = 2u;
            desc.ColorTargetFormats[0] = RHI::Format::R32_UINT; // EntityId
            desc.ColorTargetFormats[1] = RHI::Format::R32_UINT; // PrimitiveId
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = sizeof(RHI::GpuScenePushConstants);
            desc.DebugName = "Renderer.SelectionPointId";
            return desc;
        }

        // GRAPHICS-074 Slice C — default-recipe selection outline pipeline.
        // Pairs the fullscreen `post_fullscreen.vert.spv` (no vertex inputs,
        // no push constants — just emits a fullscreen triangle and a UV
        // varying) with `selection_outline.frag.spv` (samples the `EntityId`
        // R32_UINT target through `usampler2D uPickID` and writes the outline
        // RGBA into the `SelectionOutline` color target). The recipe's
        // `"SelectionOutlinePass"` declares
        // `Read(presentSource, ShaderRead) + Read(EntityId, ShaderRead) +
        // Read(SceneDepth, DepthRead) + Write(SelectionOutline,
        // ColorAttachmentWrite)`, so the render pass attaches `SelectionOutline`
        // (backbuffer format, per `FrameRecipeSizing::BackbufferFormat`) and
        // `SceneDepth` (D32_FLOAT, read-only). Depth state stays off — the
        // shader does not test or write depth — but the pipeline declares the
        // matching `DepthTargetFormat` so it remains render-pass-compatible
        // with the declared depth attachment.
        //
        // Push constants: `PushConstantSize = 144` matches
        // `SelectionOutlinePushConstants` defined in
        // `Passes/Pass.Selection.Outline.cpp`, which mirrors the
        // `selection_outline.frag` `layout(push_constant) uniform Push`
        // block byte-for-byte under Vulkan std430. The pass body pushes a
        // zero-initialised instance every frame so the shader never reads
        // stale push memory from a prior draw — without this, `OutlineWidth`
        // and `SelectedCount` could be arbitrary, producing nondeterministic
        // outlines and an unbounded fragment loop. Runtime-driven outline
        // state plumbing (selected/hovered IDs, colours, animation) is
        // deferred alongside the `Picking.Readback` drain (Slice D).
        // Portability caveat: 144 bytes exceeds the Vulkan-guaranteed
        // minimum `maxPushConstantsSize` of 128; reducing the block (e.g.
        // moving `SelectedIds[16]` into a UBO or bindless buffer) is the
        // tracked follow-up so the pipeline is portable across all
        // conformant devices. Current desktop Vulkan implementations expose
        // 256-byte push ranges, so this is non-blocking for the default
        // gate.
        //
        // The legacy shaders that previously sourced the outline overlay
        // (`forward/outline_overlay.frag` and friends) declared
        // incompatible push-constant blocks and descriptor sets and are
        // deliberately *not* referenced here — see
        // `src/graphics/renderer/README.md` ("Shader push-constant
        // compatibility policy") for the policy.
        [[nodiscard]] static RHI::PipelineDesc BuildSelectionOutlinePipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/selection_outline.frag.spv");
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
            desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
            desc.PushConstantSize = 144u; // sizeof(SelectionOutlinePushConstants)
            desc.DebugName = "Renderer.SelectionOutline";
            return desc;
        }

        // GRAPHICS-075 Slice A — default-recipe postprocess tonemap pipeline.
        // Pairs the fullscreen `post_fullscreen.vert.spv` (no vertex inputs,
        // no push constants; emits a fullscreen triangle and a UV varying)
        // with `post_tonemap.frag.spv` (samples the prior frame's HDR scene
        // color through `sampler2D uSceneColor` + bloom mix through
        // `sampler2D uBloomColor` and writes LDR back to the recipe's
        // `SceneColorLDR` target). The recipe's `"PostProcessPass"` declares
        // `Read(SceneColorHDR, ShaderRead) + Write(SceneColorLDR,
        // ColorAttachmentWrite)` (plus the bloom / histogram / AATemp
        // transient writes that the later slices' helpers consume), so the
        // render pass attaches `SceneColorLDR` (backbuffer format, per
        // `FrameRecipeSizing::BackbufferFormat`) with no depth attachment.
        //
        // Push constants: `PushConstantSize = sizeof(PostProcessToneMapPushConstants)`
        // (80 bytes) mirrors the shader's `layout(push_constant) Push { ... }`
        // block byte-for-byte under Vulkan std430 (4×4 bytes header + 4×4
        // bytes grading scalars + 3× `vec3 + float pad`). The pass body
        // builds the payload through `BuildPostProcessToneMapPushConstants(
        // m_PostProcessSystem.GetSettings())`, which derives `Exposure` /
        // `BloomIntensity` from settings and uses deterministic defaults
        // (`Operator = 0` ACES, `ColorGradingOn = 0`, neutral
        // `Saturation`/`Contrast`/`Lift`/`Gamma`/`Gain`) for the rest. The
        // canonical 20-byte `PostProcessPushConstants` block shared by the
        // other postprocess stages is intentionally *not* used here: under
        // std430 it aliases `HistogramBinCount` onto `ColorGradingOn` (so a
        // 256-bin default would enable grading) and `StageKind` onto
        // `Saturation` (`bit_cast<float>(2)` ≈ 0 → grayscale), with the
        // remaining 60 bytes of `Lift`/`Gamma`/`Gain` reading implementation
        // -defined memory — the standing "Shader push-constant compatibility
        // policy" hard gate.
        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessToneMapPipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_tonemap.frag.spv");
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
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessToneMapPushConstants));
            desc.DebugName = "Renderer.PostProcess.ToneMap";
            return desc;
        }

        // GRAPHICS-075 Slice B.1 — default-recipe postprocess bloom
        // downsample pipeline. Pairs the fullscreen `post_fullscreen.vert.spv`
        // with `post_bloom_downsample.frag.spv` (samples the prior bloom
        // mip via `sampler2D uInput`, writes the 13-tap downsample result
        // into the next mip of `PostProcess.BloomScratch`). The target
        // format follows the recipe's `BloomScratch` declaration
        // (`RGBA16_FLOAT`) and there is no depth attachment.
        //
        // Push constants: `PushConstantSize =
        // sizeof(PostProcessBloomDownsamplePushConstants)` (16 bytes,
        // `vec2 InvSrcResolution + float Threshold + int IsFirstMip`)
        // mirrors the shader's `layout(push_constant) Push { ... }` block
        // byte-for-byte under std430. The canonical 20-byte
        // `PostProcessPushConstants` block shared by other postprocess
        // stages is intentionally *not* used here per the standing
        // "Shader push-constant compatibility policy": pushing it would
        // alias `Gamma` (2.2) onto `Threshold` and `BloomIntensity` onto
        // `IsFirstMip` (`bit_cast<int>(0.05f)` ≈ 1.04e9 → always-first-mip)
        // while reading past the shader's declared block.
        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessBloomDownsamplePipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_bloom_downsample.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessBloomDownsamplePushConstants));
            desc.DebugName = "Renderer.PostProcess.Bloom.Downsample";
            return desc;
        }

        // GRAPHICS-075 Slice B.1 — default-recipe postprocess bloom
        // upsample pipeline. Pairs `post_fullscreen.vert.spv` with
        // `post_bloom_upsample.frag.spv` (samples the coarser mip via
        // `sampler2D uCoarser` + the current downsample mip via
        // `sampler2D uCurrent`, writes the 9-tap tent-filter accumulation
        // result into the finer mip of `PostProcess.BloomScratch`). Same
        // target shape as the downsample pipeline (RGBA16F color,
        // no depth).
        //
        // Push constants: `PushConstantSize =
        // sizeof(PostProcessBloomUpsamplePushConstants)` (16 bytes,
        // `vec2 InvCoarserResolution + float FilterRadius + float _pad0`).
        // Same std430 alias hazard as the downsample pipeline; the canonical
        // 20-byte block stays out.
        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessBloomUpsamplePipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_bloom_upsample.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA16_FLOAT;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessBloomUpsamplePushConstants));
            desc.DebugName = "Renderer.PostProcess.Bloom.Upsample";
            return desc;
        }

        // GRAPHICS-075 Slice C — default-recipe postprocess FXAA pipeline.
        // Pairs `post_fullscreen.vert.spv` with `post_fxaa.frag.spv`
        // (samples post-tonemap `SceneColorLDR` through one sampled-image
        // binding plus a linear-clamp sampler, writes the anti-aliased
        // result back into the recipe's LDR target). The target format
        // follows the recipe's `SceneColorLDR` declaration (the
        // backbuffer format `FrameRecipeSizing::BackbufferFormat`), so
        // the pipeline takes the same `colorFormat` parameter the
        // tonemap pipeline does. No depth attachment.
        //
        // Push constants: `PushConstantSize =
        // sizeof(PostProcessFXAAPushConstants)` (20 bytes, `vec2
        // InvResolution + float ContrastThreshold + float
        // RelativeThreshold + float SubpixelBlending`) mirrors the
        // shader's `layout(push_constant) Push { ... }` block byte-for-
        // byte under std430. The canonical 20-byte
        // `PostProcessPushConstants` block shared by other postprocess
        // stages is intentionally *not* used here per the standing
        // "Shader push-constant compatibility policy": pushing the
        // canonical block would alias `Exposure` (1.0) onto
        // `InvResolution.x`, `Gamma` (2.2) onto `InvResolution.y`,
        // `BloomIntensity` (0.05) onto `ContrastThreshold`, etc., and
        // produce visually-meaningless FXAA output even though the wire
        // size happens to match.
        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessFXAAPipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fxaa.frag.spv");
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
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessFXAAPushConstants));
            desc.DebugName = "Renderer.PostProcess.FXAA";
            return desc;
        }

        // GRAPHICS-075 Slice D.2a — three default-recipe postprocess SMAA
        // pipelines, each pairing `post_fullscreen.vert.spv` with the
        // matching SMAA fragment shader. The recipe's
        // `PostProcess.AATemp.{Edges,Weights,Resolved}` split allocates
        // three matched-format AA transients, so the edge pipeline is
        // *fixed* at `RG8_UNORM`, the blend pipeline is *fixed* at
        // `RGBA8_UNORM`, and the resolve pipeline keeps the
        // backbuffer-format `colorFormat` parameter (mirroring the FXAA
        // pipeline, which also writes to `AATemp.Resolved` under the
        // resolve graph pass). The edge / blend formats are no longer
        // parameterised because the recipe-level resource declarations
        // pin them — letting a caller pass a different `colorFormat`
        // would diverge from the recipe's AATemp.{Edges,Weights}
        // attachment formats and either fail Vulkan's render-pass-
        // compatibility rule or silently skip the bound stage. Push-
        // constant sizes still match each shader's std430 push block
        // byte-for-byte (16 bytes per stage); the canonical 20-byte
        // `PostProcessPushConstants` is intentionally not reused per the
        // "Shader push-constant compatibility policy" — see
        // `Pass.PostProcess.SMAA.cppm` for the aliasing rationale. The
        // retained `AreaTex` / `SearchTex` LUT textures sampled by the
        // blend pipeline are owned by `PostProcessSystem` and land in
        // Slice D.2b alongside the device-aware `Initialize(device)`
        // overload.
        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessSMAAEdgePipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_smaa_edge.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            // Fixed at `RG8_UNORM` to match the recipe's
            // `PostProcess.AATemp.Edges` transient; the shader writes
            // `vec2 edges` so the unused .ba channels would waste
            // bandwidth on a wider target.
            desc.ColorTargetFormats[0] = RHI::Format::RG8_UNORM;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessSMAAEdgePushConstants));
            desc.DebugName = "Renderer.PostProcess.SMAA.Edge";
            return desc;
        }

        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessSMAABlendPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_smaa_blend.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            // Fixed at `RGBA8_UNORM` to match the recipe's
            // `PostProcess.AATemp.Weights` transient (four-channel
            // blending weights per the SMAA reference). Happens to share
            // the byte shape of the default backbuffer format, but the
            // dependency is on the *recipe* declaration, not the
            // coincidence.
            desc.ColorTargetFormats[0] = RHI::Format::RGBA8_UNORM;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessSMAABlendPushConstants));
            desc.DebugName = "Renderer.PostProcess.SMAA.Blend";
            return desc;
        }

        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessSMAAResolvePipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_smaa_resolve.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.Rasterizer.Winding = RHI::FrontFace::CounterClockwise;
            desc.Rasterizer.Fill = RHI::FillMode::Solid;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.DepthStencil.StencilEnable = false;
            desc.ColorBlend[0].Enable = false;
            desc.ColorTargetCount = 1u;
            // Resolve writes the final anti-aliased LDR to
            // `PostProcess.AATemp.Resolved`, which the recipe allocates
            // with `FrameRecipeSizing::BackbufferFormat`. The FXAA
            // pipeline writes the same resolved attachment and takes the
            // same backbuffer-format `colorFormat` parameter, so both
            // pipelines stay render-pass-compatible with the recipe's
            // resolve graph pass.
            desc.ColorTargetFormats[0] = colorFormat;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessSMAAResolvePushConstants));
            desc.DebugName = "Renderer.PostProcess.SMAA.Resolve";
            return desc;
        }

        // GRAPHICS-075 Slice E.1 — default-recipe postprocess histogram
        // compute pipeline. Standalone compute pipeline (no vertex /
        // fragment stages); the `ComputeShaderPath` field is what the
        // pipeline backend uses to interpret the descriptor as compute
        // per the `PipelineDesc` contract. The dispatch runs in its own
        // ordered graph pass `"PostProcessHistogramPass"` (declared by
        // the recipe with `Read(SceneColorHDR, ShaderRead)` +
        // `Write(PostProcess.Histogram, BufferUsage::ShaderWrite)`) so
        // the framegraph compiler emits the read-after-write barrier
        // and the dispatch executes outside any render-pass scope —
        // Vulkan rejects `vkCmdDispatch` inside an active render-pass
        // scope, which is why the histogram cannot share the
        // `"PostProcessPass"` umbrella's render-pass scope.
        //
        // Push constants: `PushConstantSize =
        // sizeof(PostProcessHistogramPushConstants)` (16 bytes,
        // `uint Width + uint Height + float MinLogLum + float
        // RangeLogLum`) mirrors the shader's
        // `layout(push_constant) PushConstants` block byte-for-byte
        // under std430. The canonical 20-byte `PostProcessPushConstants`
        // block shared by other postprocess stages is intentionally
        // *not* used here per the standing shader-push-constant
        // compatibility policy: under std430 it would alias `Exposure`
        // (1.0) onto `Width` (`bit_cast<uint>(1.0f)` = 0x3F800000 ≈
        // 1.07e9 pixels wide), `Gamma` (2.2) onto `Height`, and
        // `BloomIntensity` (0.05) onto `MinLogLum`, producing a
        // degenerate out-of-bounds dispatch shape and a meaningless
        // luminance histogram.
        [[nodiscard]] static RHI::PipelineDesc BuildPostProcessHistogramPipelineDesc() noexcept
        {
            RHI::PipelineDesc desc{};
            desc.ComputeShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/post_histogram.comp.spv");
            desc.ColorTargetCount = 0u;
            desc.DepthTargetFormat = RHI::Format::Undefined;
            desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(PostProcessHistogramPushConstants));
            desc.DebugName = "Renderer.PostProcess.Histogram";
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

        // GRAPHICS-076 Slice A — canonical default-recipe present pipeline.
        // Pairs with the new `assets/shaders/present.{vert,frag}` shaders:
        // the vertex stage emits the fullscreen triangle (positions
        // [-1,-1], [3,-1], [-1,3]) and the fragment samples
        // `FrameRecipe.PresentSource` and writes the imported backbuffer
        // LDR target with alpha forced to opaque. Held byte-identical
        // between the initial `Initialize()` and any subsequent
        // `RebuildOperationalResources()` so the pipeline registry's
        // dedupe yields a stable device handle. `PushConstantSize = 0u`
        // because the canonical `PresentPass::Execute()` records only
        // `BindPipeline + Draw(3, 1, 0, 0)`; no per-frame push data is
        // required (the present source binding is descriptor-side, owned
        // by the backend's pipeline layout). Distinct from
        // `BuildMinimalVisibleTrianglePipelineDesc(...)` so the
        // MinimalDebug scaffold can retire with `GRAPHICS-081` without
        // disturbing the canonical present pipeline.
        [[nodiscard]] static RHI::PipelineDesc BuildPresentPipelineDesc(
            const RHI::Format colorFormat = RHI::Format::RGBA8_UNORM) noexcept
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/present.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/present.frag.spv");
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
            desc.DebugName = "Renderer.Present";
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

            // GRAPHICS-070 — forward surface pipeline. Drop the lease before
            // republishing so a non-deduped registry would not leak a dangling
            // entry; `SetPipeline(PipelineHandle{})` zeros the cached handle
            // so a failed `Create()` leaves the pass in the fail-closed state
            // that `RecordForwardSurfacePass` interprets as `SkippedUnavailable`.
            m_ForwardSurfacePipelineLease.reset();
            if (m_ForwardSurfacePass)
            {
                m_ForwardSurfacePass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc forwardSurfaceDesc = BuildForwardSurfacePipelineDesc();
            auto forwardSurfacePipeline = m_PipelineManager->Create(forwardSurfaceDesc);
            if (forwardSurfacePipeline.has_value())
            {
                m_ForwardSurfacePipelineLease.emplace(std::move(*forwardSurfacePipeline));
                if (m_ForwardSurfacePass)
                {
                    m_ForwardSurfacePass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_ForwardSurfacePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] ForwardSurface pipeline unavailable; default-recipe surface recording will be skipped: error={}",
                                static_cast<int>(forwardSurfacePipeline.error()));
            }

            // GRAPHICS-071 — retained line and point forward pipelines. These
            // use the same reset/republish pattern as GRAPHICS-070 so failed
            // creates leave the pass in `SkippedUnavailable` rather than
            // retaining stale device handles across rebuilds.
            m_ForwardLinePipelineLease.reset();
            if (m_ForwardLinePass)
            {
                m_ForwardLinePass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc forwardLineDesc = BuildForwardLinePipelineDesc();
            auto forwardLinePipeline = m_PipelineManager->Create(forwardLineDesc);
            if (forwardLinePipeline.has_value())
            {
                m_ForwardLinePipelineLease.emplace(std::move(*forwardLinePipeline));
                if (m_ForwardLinePass)
                {
                    m_ForwardLinePass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_ForwardLinePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] ForwardLine pipeline unavailable; default-recipe line recording will be skipped: error={}",
                                static_cast<int>(forwardLinePipeline.error()));
            }

            m_ForwardPointPipelineLease.reset();
            if (m_ForwardPointPass)
            {
                m_ForwardPointPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc forwardPointDesc = BuildForwardPointPipelineDesc();
            auto forwardPointPipeline = m_PipelineManager->Create(forwardPointDesc);
            if (forwardPointPipeline.has_value())
            {
                m_ForwardPointPipelineLease.emplace(std::move(*forwardPointPipeline));
                if (m_ForwardPointPass)
                {
                    m_ForwardPointPass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_ForwardPointPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] ForwardPoint pipeline unavailable; default-recipe point recording will be skipped: error={}",
                                static_cast<int>(forwardPointPipeline.error()));
            }

            // GRAPHICS-073 Slice A — depth-only shadow pipeline. Same
            // reset/republish pattern as the forward pipelines so a failed
            // `Create()` leaves the pass in `SkippedUnavailable` rather than
            // retaining a stale device handle across rebuilds.
            m_ShadowPipelineLease.reset();
            if (m_ShadowPass)
            {
                m_ShadowPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc shadowDesc = BuildShadowPipelineDesc();
            auto shadowPipeline = m_PipelineManager->Create(shadowDesc);
            if (shadowPipeline.has_value())
            {
                m_ShadowPipelineLease.emplace(std::move(*shadowPipeline));
                if (m_ShadowPass)
                {
                    m_ShadowPass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_ShadowPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] Shadow pipeline unavailable; default-recipe shadow recording will be skipped: error={}",
                                static_cast<int>(shadowPipeline.error()));
            }

            // GRAPHICS-072 Slice A — deferred GBuffer pipeline. Same
            // reset/republish pattern as the forward and shadow pipelines so a
            // failed `Create()` leaves the pass in `SkippedUnavailable` rather
            // than retaining a stale device handle across rebuilds.
            m_DeferredGBufferPipelineLease.reset();
            if (m_DeferredGBufferPass)
            {
                m_DeferredGBufferPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc deferredGBufferDesc = BuildDeferredGBufferPipelineDesc();
            auto deferredGBufferPipeline = m_PipelineManager->Create(deferredGBufferDesc);
            if (deferredGBufferPipeline.has_value())
            {
                m_DeferredGBufferPipelineLease.emplace(std::move(*deferredGBufferPipeline));
                if (m_DeferredGBufferPass)
                {
                    m_DeferredGBufferPass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_DeferredGBufferPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] DeferredGBuffer pipeline unavailable; default-recipe deferred surface recording will be skipped: error={}",
                                static_cast<int>(deferredGBufferPipeline.error()));
            }

            // GRAPHICS-072 Slice B — deferred lighting pipeline. Same
            // reset/republish pattern as the forward, shadow, and GBuffer
            // pipelines so a failed `Create()` leaves the pass in
            // `SkippedUnavailable` rather than retaining a stale device handle
            // across rebuilds.
            m_DeferredLightingPipelineLease.reset();
            if (m_DeferredLightingPass)
            {
                m_DeferredLightingPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc deferredLightingDesc = BuildDeferredLightingPipelineDesc();
            auto deferredLightingPipeline = m_PipelineManager->Create(deferredLightingDesc);
            if (deferredLightingPipeline.has_value())
            {
                m_DeferredLightingPipelineLease.emplace(std::move(*deferredLightingPipeline));
                if (m_DeferredLightingPass)
                {
                    m_DeferredLightingPass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_DeferredLightingPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] DeferredLighting pipeline unavailable; default-recipe deferred composition recording will be skipped: error={}",
                                static_cast<int>(deferredLightingPipeline.error()));
            }

            // GRAPHICS-074 Slice D.1 — renderer-owned host-visible
            // `Picking.Readback` buffer. Allocated lazily so the buffer
            // survives `RebuildOperationalResources()` byte-identical when
            // `device.GetFramesInFlight()` is unchanged (same pattern
            // `ShadowSystem` follows for its depth atlas); the
            // `m_BufferManager` itself is torn down in `Shutdown()` along
            // with the lease, so a fresh `Initialize(device)` after
            // `Shutdown()` will allocate a new buffer against the new
            // manager. Sized for `8 * frames-in-flight` bytes per
            // `GRAPHICS-012Q`'s `EncodedSelectionId` payload (one 4-byte
            // `EntityId` word + one 4-byte `EncodedSelectionId` word per
            // in-flight frame slot). If the expected size differs from the
            // current allocation (e.g. the device reports a different
            // frames-in-flight after a swapchain rebuild), the lease is
            // dropped and re-created so Slice D.2's `slot * 8` addressing
            // never overruns the buffer. Slice D.2 imports the handle into
            // the recipe and records `CopyTextureToBuffer(...)`; Slice D.3
            // drains it on `BeginFrame()`.
            const std::uint32_t pickingFramesInFlight = std::max(1u, device.GetFramesInFlight());
            const std::uint64_t pickingReadbackBytes =
                static_cast<std::uint64_t>(8u) *
                static_cast<std::uint64_t>(pickingFramesInFlight);
            const bool pickingReadbackNeedsAllocation =
                !m_PickingReadbackBuffer.has_value() ||
                !m_PickingReadbackBuffer->IsValid() ||
                m_PickingReadbackBufferSize != pickingReadbackBytes;
            if (pickingReadbackNeedsAllocation)
            {
                m_PickingReadbackBuffer.reset();
                m_PickingReadbackBufferSize = 0u;
                auto pickingReadbackOr = m_BufferManager->Create({
                    .SizeBytes   = pickingReadbackBytes,
                    .Usage       = RHI::BufferUsage::TransferDst,
                    .HostVisible = true,
                    .DebugName   = "Renderer.PickingReadback",
                });
                if (pickingReadbackOr.has_value())
                {
                    m_PickingReadbackBuffer.emplace(std::move(*pickingReadbackOr));
                    m_PickingReadbackBufferSize = pickingReadbackBytes;
                }
                else
                {
                    Core::Log::Warn("[Graphics] Picking.Readback buffer unavailable; default-recipe picking readback will be skipped: error={}",
                                    static_cast<int>(pickingReadbackOr.error()));
                }
            }

            // GRAPHICS-074 Slice D.3 — keep the per-slot picking metadata
            // arrays sized to match the current `frames-in-flight` slot
            // count. Three cases:
            //   1. Slot count shrank (FIF demoted, e.g. triple → double
            //      buffered). Slots at indices `>= newSlotCount` are about
            //      to be truncated, so any pending readback in that tail is
            //      *resolved* by publishing `PublishNoHit()` before the
            //      array shrinks — otherwise the SelectionSystem would
            //      keep its `PendingPick` visible to the runtime/editor
            //      forever (the new slot indexing addresses a strictly
            //      smaller range, so the dropped slots can never be
            //      drained naturally).
            //   2. Slot count unchanged or grew with previously pending
            //      slots still within the new bound. Those still-pending
            //      slots are flagged `Invalidated=true` so the upcoming
            //      `BeginFrame()` drain publishes `PublishNoHit` rather
            //      than a stale pre-rebuild hit (the buffer itself is
            //      preserved across same-FIF rebuilds, so the underlying
            //      bytes would still decode to a hit — that's exactly the
            //      case the test `PublishesNoHitForInvalidatedRequest`
            //      exercises).
            //   3. Slot count grew (FIF promoted). New trailing entries
            //      start in a clean non-pending state.
            const std::size_t newSlotCount = static_cast<std::size_t>(pickingFramesInFlight);
            if (m_PickingSlotPending.size() > newSlotCount)
            {
                for (std::size_t slot = newSlotCount; slot < m_PickingSlotPending.size(); ++slot)
                {
                    if (m_PickingSlotPending[slot] && m_SelectionSystem)
                    {
                        m_SelectionSystem->PublishNoHit();
                    }
                }
                m_PickingSlotPending.resize(newSlotCount);
                m_PickingSlotIssuedFrame.resize(newSlotCount);
                m_PickingSlotRequest.resize(newSlotCount);
                m_PickingSlotInvalidated.resize(newSlotCount);
            }
            for (std::size_t slot = 0; slot < m_PickingSlotPending.size(); ++slot)
            {
                if (m_PickingSlotPending[slot])
                {
                    m_PickingSlotInvalidated[slot] = true;
                }
            }
            if (m_PickingSlotPending.size() < newSlotCount)
            {
                m_PickingSlotPending.resize(newSlotCount, false);
                m_PickingSlotIssuedFrame.resize(newSlotCount, 0u);
                m_PickingSlotRequest.resize(newSlotCount, PickPixelRequest{});
                m_PickingSlotInvalidated.resize(newSlotCount, false);
            }

            // GRAPHICS-075 Slice E.2 — renderer-owned host-visible
            // `Histogram.Readback` buffer. Allocated lazily so the buffer
            // survives `RebuildOperationalResources()` byte-identical when
            // `device.GetFramesInFlight()` is unchanged (same pattern picking
            // follows above). Sized for `kHistogramReadbackSlotBytes *
            // frames-in-flight` bytes (256 uint32 bins per slot, one slot
            // per in-flight frame). If the expected size differs from the
            // current allocation (e.g. the device reports a different
            // frames-in-flight after a swapchain rebuild), the lease is
            // dropped and re-created so the executor's `slot *
            // kHistogramReadbackSlotBytes` per-frame addressing never
            // overruns the buffer.
            const std::uint64_t histogramReadbackBytes =
                kHistogramReadbackSlotBytes *
                static_cast<std::uint64_t>(pickingFramesInFlight);
            const bool histogramReadbackNeedsAllocation =
                !m_HistogramReadbackBuffer.has_value() ||
                !m_HistogramReadbackBuffer->IsValid() ||
                m_HistogramReadbackBufferSize != histogramReadbackBytes;
            if (histogramReadbackNeedsAllocation)
            {
                m_HistogramReadbackBuffer.reset();
                m_HistogramReadbackBufferSize = 0u;
                auto histogramReadbackOr = m_BufferManager->Create({
                    .SizeBytes   = histogramReadbackBytes,
                    .Usage       = RHI::BufferUsage::TransferDst,
                    .HostVisible = true,
                    .DebugName   = "Renderer.HistogramReadback",
                });
                if (histogramReadbackOr.has_value())
                {
                    m_HistogramReadbackBuffer.emplace(std::move(*histogramReadbackOr));
                    m_HistogramReadbackBufferSize = histogramReadbackBytes;
                }
                else
                {
                    Core::Log::Warn("[Graphics] Histogram.Readback buffer unavailable; default-recipe histogram readback will be skipped: error={}",
                                    static_cast<int>(histogramReadbackOr.error()));
                }
            }

            // GRAPHICS-075 Slice E.2 — keep the per-slot histogram metadata
            // arrays sized to match the current `frames-in-flight` slot
            // count. Mirrors the picking-slot resize policy above: shrinking
            // the FIF discards trailing pending readbacks (they would never
            // be drained naturally since the new slot indexing addresses a
            // strictly smaller range), and any still-pending slots are
            // flagged `Invalidated=true` so the upcoming `BeginFrame()`
            // drain skips the publish for slots whose pre-rebuild copy is
            // no longer trustworthy.
            if (m_HistogramSlotPending.size() > newSlotCount)
            {
                m_HistogramSlotPending.resize(newSlotCount);
                m_HistogramSlotIssuedFrame.resize(newSlotCount);
                m_HistogramSlotInvalidated.resize(newSlotCount);
            }
            for (std::size_t slot = 0; slot < m_HistogramSlotPending.size(); ++slot)
            {
                if (m_HistogramSlotPending[slot])
                {
                    m_HistogramSlotInvalidated[slot] = true;
                }
            }
            if (m_HistogramSlotPending.size() < newSlotCount)
            {
                m_HistogramSlotPending.resize(newSlotCount, false);
                m_HistogramSlotIssuedFrame.resize(newSlotCount, 0u);
                m_HistogramSlotInvalidated.resize(newSlotCount, false);
            }

            // GRAPHICS-074 Slice A — EntityId selection pipeline. Same
            // reset/republish pattern as the forward, shadow, and deferred
            // pipelines so a failed `Create()` leaves the pass in
            // `SkippedUnavailable` rather than retaining a stale device handle
            // across rebuilds. Slices B/C add the Face/Edge/Point + outline
            // pipelines; Slice D allocates the `Picking.Readback` buffer.
            m_SelectionEntityIdPipelineLease.reset();
            if (m_SelectionEntityIdPass)
            {
                m_SelectionEntityIdPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc selectionEntityIdDesc = BuildSelectionEntityIdPipelineDesc();
            auto selectionEntityIdPipeline = m_PipelineManager->Create(selectionEntityIdDesc);
            if (selectionEntityIdPipeline.has_value())
            {
                m_SelectionEntityIdPipelineLease.emplace(std::move(*selectionEntityIdPipeline));
                if (m_SelectionEntityIdPass)
                {
                    m_SelectionEntityIdPass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_SelectionEntityIdPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] SelectionEntityId pipeline unavailable; default-recipe picking recording will be skipped: error={}",
                                static_cast<int>(selectionEntityIdPipeline.error()));
            }

            // GRAPHICS-074 Slice B — Face / Edge / Point selection ID
            // pipelines. Same reset/republish pattern as the EntityId
            // pipeline above so a failed `Create()` leaves the matching
            // pass in `SkippedUnavailable` rather than retaining a stale
            // device handle across rebuilds.
            m_SelectionFaceIdPipelineLease.reset();
            if (m_SelectionFaceIdPass)
            {
                m_SelectionFaceIdPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc selectionFaceIdDesc = BuildSelectionFaceIdPipelineDesc();
            auto selectionFaceIdPipeline = m_PipelineManager->Create(selectionFaceIdDesc);
            if (selectionFaceIdPipeline.has_value())
            {
                m_SelectionFaceIdPipelineLease.emplace(std::move(*selectionFaceIdPipeline));
                if (m_SelectionFaceIdPass)
                {
                    m_SelectionFaceIdPass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_SelectionFaceIdPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] SelectionFaceId pipeline unavailable; default-recipe face picking recording will be skipped: error={}",
                                static_cast<int>(selectionFaceIdPipeline.error()));
            }

            m_SelectionEdgeIdPipelineLease.reset();
            if (m_SelectionEdgeIdPass)
            {
                m_SelectionEdgeIdPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc selectionEdgeIdDesc = BuildSelectionEdgeIdPipelineDesc();
            auto selectionEdgeIdPipeline = m_PipelineManager->Create(selectionEdgeIdDesc);
            if (selectionEdgeIdPipeline.has_value())
            {
                m_SelectionEdgeIdPipelineLease.emplace(std::move(*selectionEdgeIdPipeline));
                if (m_SelectionEdgeIdPass)
                {
                    m_SelectionEdgeIdPass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_SelectionEdgeIdPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] SelectionEdgeId pipeline unavailable; default-recipe edge picking recording will be skipped: error={}",
                                static_cast<int>(selectionEdgeIdPipeline.error()));
            }

            m_SelectionPointIdPipelineLease.reset();
            if (m_SelectionPointIdPass)
            {
                m_SelectionPointIdPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc selectionPointIdDesc = BuildSelectionPointIdPipelineDesc();
            auto selectionPointIdPipeline = m_PipelineManager->Create(selectionPointIdDesc);
            if (selectionPointIdPipeline.has_value())
            {
                m_SelectionPointIdPipelineLease.emplace(std::move(*selectionPointIdPipeline));
                if (m_SelectionPointIdPass)
                {
                    m_SelectionPointIdPass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_SelectionPointIdPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] SelectionPointId pipeline unavailable; default-recipe point picking recording will be skipped: error={}",
                                static_cast<int>(selectionPointIdPipeline.error()));
            }

            // GRAPHICS-074 Slice C — selection outline pipeline. Same reset/
            // republish pattern as the four selection-ID pipelines above so a
            // failed `Create()` leaves the outline pass in `SkippedUnavailable`
            // rather than retaining a stale device handle across rebuilds.
            m_SelectionOutlinePipelineLease.reset();
            if (m_SelectionOutlinePass)
            {
                m_SelectionOutlinePass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc selectionOutlineDesc =
                BuildSelectionOutlinePipelineDesc(m_BackbufferFormat);
            auto selectionOutlinePipeline = m_PipelineManager->Create(selectionOutlineDesc);
            if (selectionOutlinePipeline.has_value())
            {
                m_SelectionOutlinePipelineLease.emplace(std::move(*selectionOutlinePipeline));
                if (m_SelectionOutlinePass)
                {
                    m_SelectionOutlinePass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_SelectionOutlinePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] SelectionOutline pipeline unavailable; default-recipe selection outline recording will be skipped: error={}",
                                static_cast<int>(selectionOutlinePipeline.error()));
            }

            // GRAPHICS-075 Slice A — postprocess tonemap pipeline. Same
            // reset/republish pattern as the selection-outline pipeline
            // above so a failed `Create()` leaves the pass in
            // `SkippedUnavailable` rather than retaining a stale device
            // handle across rebuilds. Bloom/Histogram/FXAA/SMAA pipelines
            // arrive with Slices B–E behind the same umbrella executor
            // branch.
            m_PostProcessToneMapPipelineLease.reset();
            if (m_PostProcessToneMapPass)
            {
                m_PostProcessToneMapPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc postProcessToneMapDesc =
                BuildPostProcessToneMapPipelineDesc(m_BackbufferFormat);
            auto postProcessToneMapPipeline = m_PipelineManager->Create(postProcessToneMapDesc);
            if (postProcessToneMapPipeline.has_value())
            {
                m_PostProcessToneMapPipelineLease.emplace(std::move(*postProcessToneMapPipeline));
                if (m_PostProcessToneMapPass)
                {
                    m_PostProcessToneMapPass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_PostProcessToneMapPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.ToneMap pipeline unavailable; default-recipe tonemap recording will be skipped: error={}",
                                static_cast<int>(postProcessToneMapPipeline.error()));
            }

            // GRAPHICS-075 Slice B.1 — postprocess bloom downsample +
            // upsample pipelines. Same reset/republish pattern as the
            // tonemap pipeline above so a failed `Create()` leaves the
            // bloom pass in `SkippedUnavailable` (per the independent
            // early-skips in `PostProcessBloomPass::Execute`) rather than
            // retaining a stale device handle across rebuilds. Slice B.2
            // keeps both pipelines and adds per-mip iteration on top.
            m_PostProcessBloomDownsamplePipelineLease.reset();
            m_PostProcessBloomUpsamplePipelineLease.reset();
            if (m_PostProcessBloomPass)
            {
                m_PostProcessBloomPass->SetDownsamplePipeline(RHI::PipelineHandle{});
                m_PostProcessBloomPass->SetUpsamplePipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc postProcessBloomDownsampleDesc =
                BuildPostProcessBloomDownsamplePipelineDesc();
            auto postProcessBloomDownsamplePipeline = m_PipelineManager->Create(postProcessBloomDownsampleDesc);
            if (postProcessBloomDownsamplePipeline.has_value())
            {
                m_PostProcessBloomDownsamplePipelineLease.emplace(std::move(*postProcessBloomDownsamplePipeline));
                if (m_PostProcessBloomPass)
                {
                    m_PostProcessBloomPass->SetDownsamplePipeline(
                        m_PipelineManager->GetDeviceHandle(m_PostProcessBloomDownsamplePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.Bloom.Downsample pipeline unavailable; default-recipe bloom downsample recording will be skipped: error={}",
                                static_cast<int>(postProcessBloomDownsamplePipeline.error()));
            }

            const RHI::PipelineDesc postProcessBloomUpsampleDesc =
                BuildPostProcessBloomUpsamplePipelineDesc();
            auto postProcessBloomUpsamplePipeline = m_PipelineManager->Create(postProcessBloomUpsampleDesc);
            if (postProcessBloomUpsamplePipeline.has_value())
            {
                m_PostProcessBloomUpsamplePipelineLease.emplace(std::move(*postProcessBloomUpsamplePipeline));
                if (m_PostProcessBloomPass)
                {
                    m_PostProcessBloomPass->SetUpsamplePipeline(
                        m_PipelineManager->GetDeviceHandle(m_PostProcessBloomUpsamplePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.Bloom.Upsample pipeline unavailable; default-recipe bloom upsample recording will be skipped: error={}",
                                static_cast<int>(postProcessBloomUpsamplePipeline.error()));
            }

            // GRAPHICS-075 Slice C — postprocess FXAA pipeline. Same
            // reset/republish pattern as the tonemap + bloom pipelines
            // above so a failed `Create()` leaves the pass in
            // `SkippedUnavailable` (per the early-skip inside
            // `PostProcessFXAAPass::Execute` and the umbrella helper's
            // `IsValid()` gate) rather than retaining a stale device
            // handle across rebuilds. The pipeline targets the recipe's
            // LDR backbuffer format so it stays render-pass-compatible
            // with the tonemap leg's output target.
            m_PostProcessFXAAPipelineLease.reset();
            if (m_PostProcessFXAAPass)
            {
                m_PostProcessFXAAPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc postProcessFXAADesc =
                BuildPostProcessFXAAPipelineDesc(m_BackbufferFormat);
            auto postProcessFXAAPipeline = m_PipelineManager->Create(postProcessFXAADesc);
            if (postProcessFXAAPipeline.has_value())
            {
                m_PostProcessFXAAPipelineLease.emplace(std::move(*postProcessFXAAPipeline));
                if (m_PostProcessFXAAPass)
                {
                    m_PostProcessFXAAPass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_PostProcessFXAAPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.FXAA pipeline unavailable; default-recipe FXAA recording will be skipped: error={}",
                                static_cast<int>(postProcessFXAAPipeline.error()));
            }

            // GRAPHICS-075 Slice D.1 — postprocess SMAA pipelines (edge,
            // blend, resolve). Same reset/republish pattern as the tonemap
            // + bloom + FXAA pipelines above so a failed `Create()` on any
            // stage leaves that stage's bind/push/draw silenced inside
            // `PostProcessSMAAPass::Execute` rather than retaining a stale
            // device handle across rebuilds. The pass-side `IsValid()`
            // gate inside `PostProcessSMAAPass::Execute` independently
            // short-circuits each stage's bind/push/draw on its own lease
            // validity, mirroring the bloom helper's per-stage early-skip
            // — a partial outage (e.g. only the resolve shader compiles)
            // still records the surviving stages. The umbrella helper
            // requires *at least one* valid SMAA lease to proceed so a
            // complete-shader-outage downgrades the helper to
            // `SkippedUnavailable` rather than reporting a Recorded
            // no-op against missing pipelines.
            m_PostProcessSMAAEdgePipelineLease.reset();
            m_PostProcessSMAABlendPipelineLease.reset();
            m_PostProcessSMAAResolvePipelineLease.reset();
            if (m_PostProcessSMAAPass)
            {
                m_PostProcessSMAAPass->SetEdgePipeline(RHI::PipelineHandle{});
                m_PostProcessSMAAPass->SetBlendPipeline(RHI::PipelineHandle{});
                m_PostProcessSMAAPass->SetResolvePipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc postProcessSMAAEdgeDesc =
                BuildPostProcessSMAAEdgePipelineDesc();
            auto postProcessSMAAEdgePipeline = m_PipelineManager->Create(postProcessSMAAEdgeDesc);
            if (postProcessSMAAEdgePipeline.has_value())
            {
                m_PostProcessSMAAEdgePipelineLease.emplace(std::move(*postProcessSMAAEdgePipeline));
                if (m_PostProcessSMAAPass)
                {
                    m_PostProcessSMAAPass->SetEdgePipeline(
                        m_PipelineManager->GetDeviceHandle(m_PostProcessSMAAEdgePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.SMAA.Edge pipeline unavailable; default-recipe SMAA edge recording will be skipped: error={}",
                                static_cast<int>(postProcessSMAAEdgePipeline.error()));
            }
            const RHI::PipelineDesc postProcessSMAABlendDesc =
                BuildPostProcessSMAABlendPipelineDesc();
            auto postProcessSMAABlendPipeline = m_PipelineManager->Create(postProcessSMAABlendDesc);
            if (postProcessSMAABlendPipeline.has_value())
            {
                m_PostProcessSMAABlendPipelineLease.emplace(std::move(*postProcessSMAABlendPipeline));
                if (m_PostProcessSMAAPass)
                {
                    m_PostProcessSMAAPass->SetBlendPipeline(
                        m_PipelineManager->GetDeviceHandle(m_PostProcessSMAABlendPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.SMAA.Blend pipeline unavailable; default-recipe SMAA blend recording will be skipped: error={}",
                                static_cast<int>(postProcessSMAABlendPipeline.error()));
            }
            const RHI::PipelineDesc postProcessSMAAResolveDesc =
                BuildPostProcessSMAAResolvePipelineDesc(m_BackbufferFormat);
            auto postProcessSMAAResolvePipeline = m_PipelineManager->Create(postProcessSMAAResolveDesc);
            if (postProcessSMAAResolvePipeline.has_value())
            {
                m_PostProcessSMAAResolvePipelineLease.emplace(std::move(*postProcessSMAAResolvePipeline));
                if (m_PostProcessSMAAPass)
                {
                    m_PostProcessSMAAPass->SetResolvePipeline(
                        m_PipelineManager->GetDeviceHandle(m_PostProcessSMAAResolvePipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.SMAA.Resolve pipeline unavailable; default-recipe SMAA resolve recording will be skipped: error={}",
                                static_cast<int>(postProcessSMAAResolvePipeline.error()));
            }

            // GRAPHICS-075 Slice E.1 — postprocess histogram compute
            // pipeline. Same reset/republish pattern as the tonemap +
            // bloom + FXAA + SMAA leases above so a failed `Create()`
            // leaves the histogram helper in `SkippedUnavailable`
            // rather than retaining a stale device handle across
            // rebuilds.
            m_PostProcessHistogramPipelineLease.reset();
            if (m_PostProcessHistogramPass)
            {
                m_PostProcessHistogramPass->SetPipeline(RHI::PipelineHandle{});
            }
            const RHI::PipelineDesc postProcessHistogramDesc =
                BuildPostProcessHistogramPipelineDesc();
            auto postProcessHistogramPipeline = m_PipelineManager->Create(postProcessHistogramDesc);
            if (postProcessHistogramPipeline.has_value())
            {
                m_PostProcessHistogramPipelineLease.emplace(std::move(*postProcessHistogramPipeline));
                if (m_PostProcessHistogramPass)
                {
                    m_PostProcessHistogramPass->SetPipeline(
                        m_PipelineManager->GetDeviceHandle(m_PostProcessHistogramPipelineLease->GetHandle()));
                }
            }
            else
            {
                Core::Log::Warn("[Graphics] PostProcess.Histogram pipeline unavailable; default-recipe histogram recording will be skipped: error={}",
                                static_cast<int>(postProcessHistogramPipeline.error()));
            }

            // GRAPHICS-076 Slice A — canonical default-recipe present
            // pipeline. Created LAST so the test fixtures that target
            // `FailPipelineCreateCall` against specific upstream pipelines
            // (culling=1, depth=2, defaultDebugSurface=3,
            // minimalVisibleTriangle=4, forward/shadow/deferred at 5-10,
            // selection at 11-15, postprocess at 16-23) keep their
            // documented call indices unchanged. The present slot is
            // call #24. Same reset/republish + fail-closed pattern as
            // the other leases above so a failed `Create()` leaves
            // `m_PresentPass` in the fail-closed state that
            // `RecordPresentPass` interprets as `SkippedUnavailable`.
            m_PresentPipelineLease.reset();
            m_PresentPass.SetPipeline(RHI::PipelineHandle{});
            const RHI::PipelineDesc presentDesc =
                BuildPresentPipelineDesc(m_BackbufferFormat);
            auto presentPipeline = m_PipelineManager->Create(presentDesc);
            if (presentPipeline.has_value())
            {
                m_PresentPipelineLease.emplace(std::move(*presentPipeline));
                m_PresentPass.SetPipeline(
                    m_PipelineManager->GetDeviceHandle(m_PresentPipelineLease->GetHandle()));
            }
            else
            {
                Core::Log::Warn("[Graphics] Present pipeline unavailable; default-recipe present recording will be skipped: error={}",
                                static_cast<int>(presentPipeline.error()));
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

        // GRAPHICS-074 Slice D.3 — drain any picking-readback slot whose
        // issuing frame has completed since its copy was recorded. Called at
        // the top of `BeginFrame()` so the `SelectionSystem` observes the
        // pick result before the runtime extracts the next frame's
        // `RenderWorld` (the runtime/editor decides what to do with the
        // resolved hit during this frame's extraction phase). The drain is
        // gated on (a) a live operational device, (b) a valid
        // renderer-owned `Picking.Readback` lease, and (c) at least one
        // slot whose `IssuedFrame` predates the current `GlobalFrameNumber`
        // (slots issued this very frame have not yet had the chance to
        // run their copy + complete, so they stay pending). The routing
        // matches the Slice D.3 task contract:
        //   - `Invalidated` (set by `RebuildOperationalResources()` for
        //     in-flight slots, simulating a device-lost recovery whose
        //     pre-rebuild copy is no longer trustworthy) → `PublishNoHit()`.
        //   - `EntityId == 0` (background pixel after the depth prepass
        //     decided no surface won the depth-equal test for that pixel)
        //     → `PublishNoHit()`.
        //   - Otherwise → `PublishPickResult({EncodedId, StableEntityId,
        //     Hit=true})`.
        // A read failure (no host-mapped contents, MockDevice without
        // seeded bytes, etc.) leaves the local decode buffer zeroed and
        // therefore falls through to the `EntityId == 0` NoHit branch —
        // safe by construction.
        void DrainCompletedPickingSlots()
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return;
            }
            if (!m_PickingReadbackBuffer.has_value() || !m_PickingReadbackBuffer->IsValid())
            {
                return;
            }
            if (m_PickingSlotPending.empty() || !m_SelectionSystem)
            {
                return;
            }
            const std::uint64_t completedFrameNumber = m_Device->GetGlobalFrameNumber();
            const RHI::BufferHandle bufferHandle = m_PickingReadbackBuffer->GetHandle();
            for (std::size_t slot = 0; slot < m_PickingSlotPending.size(); ++slot)
            {
                if (!m_PickingSlotPending[slot])
                {
                    continue;
                }
                // Slot's copy was issued in `IssuedFrame`. The post-EndFrame
                // counter advances exactly once per `EndFrame(...)`, so
                // `IssuedFrame < GlobalFrameNumber` means the issuing frame
                // has at minimum been submitted. See the comment at
                // `BeginFrame()` for the async-backend caveat.
                if (m_PickingSlotIssuedFrame[slot] >= completedFrameNumber)
                {
                    continue;
                }
                std::uint32_t entityId    = 0u;
                std::uint32_t encodedBits = 0u;
                const std::uint64_t slotOffset = static_cast<std::uint64_t>(slot) * 8ull;
                if (slotOffset + 8ull <= m_PickingReadbackBufferSize)
                {
                    m_Device->ReadBuffer(bufferHandle, &entityId,    sizeof(entityId),    slotOffset);
                    m_Device->ReadBuffer(bufferHandle, &encodedBits, sizeof(encodedBits), slotOffset + 4ull);
                }
                const EncodedSelectionId encoded{.Value = encodedBits};
                if (m_PickingSlotInvalidated[slot] || entityId == 0u)
                {
                    m_SelectionSystem->PublishNoHit();
                }
                else
                {
                    m_SelectionSystem->PublishPickResult(PickReadbackResult{
                        .EncodedId      = encoded,
                        .StableEntityId = entityId,
                        .Hit            = true,
                    });
                }
                m_PickingSlotPending[slot] = false;
                m_PickingSlotInvalidated[slot] = false;
                m_PickingSlotIssuedFrame[slot] = 0u;
                m_PickingSlotRequest[slot] = PickPixelRequest{};
            }
        }

        // GRAPHICS-075 Slice E.2 — drain any histogram-readback slot whose
        // issuing frame has completed since its copy was recorded. Called
        // at the top of `BeginFrame()` so the `PostProcessSystem` observes
        // the exposure-history update before the runtime extracts the next
        // frame's `RenderWorld` (the tonemap leg of the upcoming frame
        // benefits from the freshly published adaptation state). The drain
        // is gated on (a) a live operational device, (b) a valid
        // renderer-owned `Histogram.Readback` lease, and (c) at least one
        // slot whose `IssuedFrame` predates the current `GlobalFrameNumber`
        // (slots issued this very frame have not yet completed their copy
        // and stay pending). Slots flagged `Invalidated` (e.g. by a
        // `RebuildOperationalResources()` device-lost recovery) are
        // released without publishing — the publish handshake intentionally
        // never sees stale pre-rebuild bytes.
        void DrainCompletedHistogramSlots()
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return;
            }
            if (!m_HistogramReadbackBuffer.has_value() || !m_HistogramReadbackBuffer->IsValid())
            {
                return;
            }
            if (m_HistogramSlotPending.empty() || !m_PostProcessSystem.has_value())
            {
                return;
            }
            const std::uint64_t completedFrameNumber = m_Device->GetGlobalFrameNumber();
            const RHI::BufferHandle bufferHandle = m_HistogramReadbackBuffer->GetHandle();
            for (std::size_t slot = 0; slot < m_HistogramSlotPending.size(); ++slot)
            {
                if (!m_HistogramSlotPending[slot])
                {
                    continue;
                }
                if (m_HistogramSlotIssuedFrame[slot] >= completedFrameNumber)
                {
                    continue;
                }

                if (m_HistogramSlotInvalidated[slot])
                {
                    // Release without publishing — pre-rebuild bytes are
                    // not trustworthy, and `PublishHistogramReadback` would
                    // anchor the retained adaptation history to them.
                    m_HistogramSlotPending[slot] = false;
                    m_HistogramSlotInvalidated[slot] = false;
                    m_HistogramSlotIssuedFrame[slot] = 0u;
                    continue;
                }

                std::array<std::uint32_t, 256> bins{};
                const std::uint64_t slotOffset =
                    static_cast<std::uint64_t>(slot) * kHistogramReadbackSlotBytes;
                if (slotOffset + kHistogramReadbackSlotBytes <= m_HistogramReadbackBufferSize)
                {
                    m_Device->ReadBuffer(bufferHandle,
                                         bins.data(),
                                         kHistogramReadbackSlotBytes,
                                         slotOffset);
                }
                m_PostProcessSystem->PublishHistogramReadback(
                    std::span<const std::uint32_t>{bins.data(), bins.size()},
                    m_HistogramSlotIssuedFrame[slot],
                    m_Device);

                m_HistogramSlotPending[slot] = false;
                m_HistogramSlotInvalidated[slot] = false;
                m_HistogramSlotIssuedFrame[slot] = 0u;
            }
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

        // GRAPHICS-070 — default-recipe forward surface command recording.
        // Called from the executor's `"SurfacePass"` branch when the active
        // recipe is the forward variant (`!usesDeferred`). Routes through the
        // same `RenderCommandPassStatus` taxonomy as the depth prepass: a
        // non-operational device returns `SkippedNonOperational`; a missing
        // pipeline lease or culling output / SurfaceOpaque bucket returns
        // `SkippedUnavailable`; otherwise the existing `ForwardSurfacePass`
        // body records the `Bind/Bind/Push/DrawIndexedIndirectCount` shape and
        // we return `Recorded`. The deferred-mode surface body is owned by
        // GRAPHICS-072 and falls through to the catch-all soft-skip.
        [[nodiscard]] RenderCommandPassStatus RecordForwardSurfacePass(RHI::ICommandContext& cmd,
                                                                       const RHI::CameraUBO& camera,
                                                                       const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_ForwardSurfacePass.has_value() ||
                !m_ForwardSurfacePipelineLease.has_value() ||
                !m_ForwardSurfacePipelineLease->IsValid() ||
                !m_GpuWorld.has_value() || !m_CullingSystem.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_ForwardSurfacePass->Execute(cmd, camera, *m_GpuWorld, *m_CullingSystem, frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordForwardLinePass(RHI::ICommandContext& cmd,
                                                                    const RHI::CameraUBO& camera,
                                                                    const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_ForwardLinePass.has_value() ||
                !m_ForwardLinePipelineLease.has_value() ||
                !m_ForwardLinePipelineLease->IsValid() ||
                !m_GpuWorld.has_value() || !m_CullingSystem.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_ForwardLinePass->Execute(cmd, camera, *m_GpuWorld, *m_CullingSystem, frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordForwardPointPass(RHI::ICommandContext& cmd,
                                                                     const RHI::CameraUBO& camera,
                                                                     const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_ForwardPointPass.has_value() ||
                !m_ForwardPointPipelineLease.has_value() ||
                !m_ForwardPointPipelineLease->IsValid() ||
                !m_GpuWorld.has_value() || !m_CullingSystem.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_ForwardPointPass->Execute(cmd, camera, *m_GpuWorld, *m_CullingSystem, frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-073 Slice A — default-recipe `"ShadowPass"` route. The
        // recipe only declares the pass when `EnableShadows` is on, so the
        // executor reaches this helper only for shadow-enabled frames. The
        // `ShadowSystem::IsEnabled()` gate inside `ShadowPass::Execute` keeps
        // the bind/draw shape silent if cascade/atlas params end up disabled
        // after the recipe build.
        [[nodiscard]] RenderCommandPassStatus RecordShadowPass(RHI::ICommandContext& cmd,
                                                                const RHI::CameraUBO& camera,
                                                                const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_ShadowPass.has_value() ||
                !m_ShadowPipelineLease.has_value() ||
                !m_ShadowPipelineLease->IsValid() ||
                !m_GpuWorld.has_value() || !m_CullingSystem.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_ShadowPass->Execute(cmd, camera, *m_GpuWorld, *m_CullingSystem, frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-074 Slice A — default-recipe `"PickingPass"` route. The
        // recipe only declares the pass when `features.EnablePicking` is
        // true (set from `world.HasPendingPick || world.PickRequest.Pending`
        // in `DeriveDefaultFrameRecipeFeatures`), so this helper is reached
        // only when a pick request was pending for the frame. Mirrors
        // `RecordForwardSurfacePass` / `RecordShadowPass`: a non-operational
        // device → `SkippedNonOperational`; a missing culling output, pass,
        // lease, `GpuWorld`, or culling system → `SkippedUnavailable`;
        // otherwise `EntityIdPass::Execute` records the
        // `Bind/Bind/Push/DrawIndexedIndirectCount` shape against the
        // `SurfaceOpaque` cull bucket and we return `Recorded`. The
        // Face/Edge/Point selection sub-passes (Slice B) and the
        // `Picking.Readback` drain + `PublishPickResult`/`PublishNoHit`
        // wiring (Slice D) are intentionally not exercised here.
        [[nodiscard]] RenderCommandPassStatus RecordSelectionEntityIdPass(RHI::ICommandContext& cmd,
                                                                           const RHI::CameraUBO& camera,
                                                                           const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_SelectionEntityIdPass.has_value() ||
                !m_SelectionEntityIdPipelineLease.has_value() ||
                !m_SelectionEntityIdPipelineLease->IsValid() ||
                !m_GpuWorld.has_value() || !m_CullingSystem.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_SelectionEntityIdPass->Execute(cmd, camera, *m_GpuWorld, *m_CullingSystem, frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-074 Slice B — default-recipe Face / Edge / Point
        // sub-pass routes inside the `"PickingPass"` executor branch.
        // Each helper mirrors `RecordSelectionEntityIdPass` exactly: a
        // non-operational device → `SkippedNonOperational`; missing
        // culling output, pass, lease, GpuWorld, or CullingSystem →
        // `SkippedUnavailable`; otherwise the bucket-bound
        // `FaceIdPass` / `EdgeIdPass` / `PointIdPass` `Execute(...)`
        // records the matching `Bind/Bind/Push/DrawIndexedIndirectCount`
        // (or non-indexed `DrawIndirectCount` for points) shape and
        // returns `Recorded`. The four sub-passes share the recipe's
        // `PickingPass` render pass, so all bound pipelines are
        // render-pass-compatible (two R32_UINT color targets + D32_FLOAT
        // depth, depth-equal / depth-write-off). The Face/Edge/Point
        // pipelines write the matching `EncodeSelectionId(domain,
        // gl_PrimitiveID)` value into `PrimitiveId` while still emitting
        // the per-instance stable entity ID into `EntityId`, so the
        // last-pass-wins-per-pixel behavior after depth-equal yields the
        // most refined domain code that survives the prepass depth test.
        // The `Picking.Readback` drain + `PublishPickResult` /
        // `PublishNoHit` wiring remain Slice D scope.
        [[nodiscard]] RenderCommandPassStatus RecordSelectionFaceIdPass(RHI::ICommandContext& cmd,
                                                                         const RHI::CameraUBO& camera,
                                                                         const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_SelectionFaceIdPass.has_value() ||
                !m_SelectionFaceIdPipelineLease.has_value() ||
                !m_SelectionFaceIdPipelineLease->IsValid() ||
                !m_GpuWorld.has_value() || !m_CullingSystem.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_SelectionFaceIdPass->Execute(cmd, camera, *m_GpuWorld, *m_CullingSystem, frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordSelectionEdgeIdPass(RHI::ICommandContext& cmd,
                                                                         const RHI::CameraUBO& camera,
                                                                         const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_SelectionEdgeIdPass.has_value() ||
                !m_SelectionEdgeIdPipelineLease.has_value() ||
                !m_SelectionEdgeIdPipelineLease->IsValid() ||
                !m_GpuWorld.has_value() || !m_CullingSystem.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_SelectionEdgeIdPass->Execute(cmd, camera, *m_GpuWorld, *m_CullingSystem, frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordSelectionPointIdPass(RHI::ICommandContext& cmd,
                                                                          const RHI::CameraUBO& camera,
                                                                          const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_SelectionPointIdPass.has_value() ||
                !m_SelectionPointIdPipelineLease.has_value() ||
                !m_SelectionPointIdPipelineLease->IsValid() ||
                !m_GpuWorld.has_value() || !m_CullingSystem.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_SelectionPointIdPass->Execute(cmd, camera, *m_GpuWorld, *m_CullingSystem, frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-074 Slice C/D.4 — default-recipe `"SelectionOutlinePass"`
        // route. The recipe only declares the pass when
        // `features.EnableSelectionOutline` is true, so this helper is
        // reached only when at least one selectable entity is present this
        // frame. Mirrors the selection-ID helpers above with the
        // fullscreen-pass shape (no culling/GpuWorld prerequisites since
        // the pass body is `BindPipeline + PushConstants + Draw(3,1,0,0)`):
        // a non-operational device → `SkippedNonOperational`; missing pass /
        // lease → `SkippedUnavailable`; otherwise
        // `SelectionOutlinePass::Execute` records the fullscreen draw and
        // we return `Recorded`. Slice D.4 sources the push payload from
        // `renderWorld.Selection` so the shader actually sees the seeded
        // hovered/selected ids and outline style instead of the Slice C
        // all-zero placeholder.
        [[nodiscard]] RenderCommandPassStatus RecordSelectionOutlinePass(RHI::ICommandContext& cmd,
                                                                          const RHI::CameraUBO& camera,
                                                                          const std::uint32_t frameIndex,
                                                                          const SelectionSnapshot& selection)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_SelectionOutlinePass.has_value() ||
                !m_SelectionOutlinePipelineLease.has_value() ||
                !m_SelectionOutlinePipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            const SelectionOutlinePushConstants pushConstants =
                BuildSelectionOutlinePushConstants(selection);
            m_SelectionOutlinePass->Execute(cmd, camera, frameIndex, pushConstants);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-075 Slice A — default-recipe `"PostProcessPass"` umbrella
        // executor route, ToneMap leg. Mirrors the selection-outline helper
        // above (fullscreen-pass shape: no `GpuWorld` / `CullingSystem`
        // prerequisites since `PostProcessToneMapPass::Execute` records
        // `BindPipeline + PushConstants + Draw(3,1,0,0)`): a non-operational
        // device → `SkippedNonOperational`; a missing pass / lease /
        // `PostProcessSystem` → `SkippedUnavailable`; otherwise the tonemap
        // pass records the fullscreen draw and we return `Recorded`. The
        // `Pass::Execute` body additionally early-returns when
        // `IsStageEnabled(ToneMap)` is false (i.e. when
        // `PostProcessSettings::Enabled` was flipped off), so a disabled
        // chain becomes a structurally-recorded no-op rather than altering
        // the executor's per-pass status taxonomy. The Slices B–E
        // Histogram / Bloom / FXAA / SMAA helpers fan out from the same
        // umbrella branch (mirroring the GRAPHICS-074 `"PickingPass"`
        // sub-pass pattern).
        [[nodiscard]] RenderCommandPassStatus RecordPostProcessToneMapPass(RHI::ICommandContext& cmd,
                                                                            const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_PostProcessSystem.has_value() ||
                !m_PostProcessToneMapPass.has_value() ||
                !m_PostProcessToneMapPipelineLease.has_value() ||
                !m_PostProcessToneMapPipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_PostProcessToneMapPass->Execute(cmd, camera);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-075 Slice B.1 — default-recipe `"PostProcessPass"`
        // umbrella executor route, Bloom leg. Mirrors the tonemap helper
        // above: a non-operational device → `SkippedNonOperational`; a
        // missing pass / system → `SkippedUnavailable`; both bloom leases
        // missing or invalid → `SkippedUnavailable`; otherwise the bloom
        // pass records its placeholder downsample + upsample bind/push/
        // draw for whichever stage's lease succeeded and we return
        // `Recorded`. Crucially the helper requires only ONE valid bloom
        // pipeline lease to proceed — the per-stage early-skips inside
        // `PostProcessBloomPass::Execute` independently gate the
        // downsample and upsample bind/push/draw on their own lease
        // validity, so a partial pipeline outage (e.g. only the upsample
        // shader compiles) still records the surviving stage rather than
        // collapsing the whole bloom leg into a SkippedUnavailable. The
        // `Execute` body additionally early-returns when
        // `IsStageEnabled(Bloom)` is false (i.e. when
        // `PostProcessSettings::EnableBloom` is off or the chain is
        // globally disabled), so a disabled bloom chain becomes a
        // structurally-recorded no-op rather than altering the executor's
        // per-pass status taxonomy. Slice B.2 keeps this helper and adds
        // per-mip iteration with the matching inline barriers.
        [[nodiscard]] RenderCommandPassStatus RecordPostProcessBloomPass(RHI::ICommandContext& cmd,
                                                                          const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            const bool hasDownsamplePipeline =
                m_PostProcessBloomDownsamplePipelineLease.has_value() &&
                m_PostProcessBloomDownsamplePipelineLease->IsValid();
            const bool hasUpsamplePipeline =
                m_PostProcessBloomUpsamplePipelineLease.has_value() &&
                m_PostProcessBloomUpsamplePipelineLease->IsValid();
            if (!m_PostProcessSystem.has_value() ||
                !m_PostProcessBloomPass.has_value() ||
                (!hasDownsamplePipeline && !hasUpsamplePipeline))
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_PostProcessBloomPass->Execute(cmd, camera);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-075 Slice E.1 — default-recipe
        // `"PostProcessHistogramPass"` executor route. The histogram is
        // a compute dispatch (`vkCmdDispatch(ceil(W/16), ceil(H/16),
        // 1)`) and Vulkan rejects dispatches inside an active render-
        // pass scope, so it runs in its own ordered graph pass before
        // `"PostProcessPass"` (declared by the recipe with
        // `Read(SceneColorHDR, ShaderRead)` + `Write(PostProcess.Histogram,
        // BufferUsage::ShaderWrite)`). The helper follows the same
        // status taxonomy as the tonemap / bloom helpers: a non-
        // operational device returns `SkippedNonOperational`; a missing
        // system / pass / pipeline lease returns `SkippedUnavailable`;
        // otherwise the helper invokes `Execute(...)` and returns
        // `Recorded`. The pass body independently gates on
        // `IsStageEnabled(Histogram)` so when histogram is gated off
        // the body emits no bind/push/dispatch but the helper still
        // records `Recorded` ("structurally-recorded no-op" taxonomy,
        // same as bloom-disabled and the Slice C/D.1 FXAA/SMAA
        // helpers). Slice E.2 adds the renderer-owned host-visible
        // `Histogram.Readback` buffer + `BeginFrame()`-side drain +
        // `PostProcessSystem::PublishHistogramReadback(...)` that
        // consumes the exposure-adaptation history buffer.
        [[nodiscard]] RenderCommandPassStatus RecordPostProcessHistogramPass(RHI::ICommandContext& cmd,
                                                                              const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_PostProcessSystem.has_value() ||
                !m_PostProcessHistogramPass.has_value() ||
                !m_PostProcessHistogramPipelineLease.has_value() ||
                !m_PostProcessHistogramPipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_PostProcessHistogramPass->Execute(cmd, camera);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-075 Slice D.2a — per-stage AA helpers. The AA umbrella
        // splits into three ordered graph passes so edge / blend /
        // resolve pipelines can target format-incompatible color
        // attachments. FXAA records under the resolve pass only; SMAA
        // records under all three. Each helper follows the same status
        // taxonomy as the tonemap / bloom helpers: a non-operational
        // device returns `SkippedNonOperational`; a missing system /
        // pass / pipeline lease returns `SkippedUnavailable`; otherwise
        // the helper invokes the matching per-stage Execute and returns
        // `Recorded`. The pass body independently gates on
        // `IsStageEnabled(...)` so when AA is gated off the body emits
        // no bind/push/draw but the helper still records `Recorded`
        // ("structurally-recorded no-op" taxonomy, same as bloom-
        // disabled and the Slice D.1 SMAA/FXAA helpers).
        [[nodiscard]] RenderCommandPassStatus RecordPostProcessAAEdgePass(RHI::ICommandContext& cmd,
                                                                          const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_PostProcessSystem.has_value() ||
                !m_PostProcessSMAAPass.has_value() ||
                !m_PostProcessSMAAEdgePipelineLease.has_value() ||
                !m_PostProcessSMAAEdgePipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_PostProcessSMAAPass->ExecuteEdge(cmd, camera);
            return RenderCommandPassStatus::Recorded;
        }

        [[nodiscard]] RenderCommandPassStatus RecordPostProcessAABlendPass(RHI::ICommandContext& cmd,
                                                                           const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_PostProcessSystem.has_value() ||
                !m_PostProcessSMAAPass.has_value() ||
                !m_PostProcessSMAABlendPipelineLease.has_value() ||
                !m_PostProcessSMAABlendPipelineLease->IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_PostProcessSMAAPass->ExecuteBlend(cmd, camera);
            return RenderCommandPassStatus::Recorded;
        }

        // Reports whether the currently-selected AA mode's pipeline(s)
        // are all available. For `None` this is trivially false (AA is
        // off). For `FXAA` it requires the FXAA pipeline lease. For
        // `SMAA` it requires all three SMAA pipeline leases — the
        // resolve shader reads `AATemp.Weights` and the blend shader
        // reads `AATemp.Edges`, so if either upstream pipeline is
        // missing the resolved attachment is sourced from cleared
        // inputs and the AA leg cannot produce a usable image. The
        // recipe-build site uses this to gate
        // `FrameRecipeFeatures::EnableAntiAliasing`, and
        // `RecordPostProcessAAResolvePass` mirrors the same gate so a
        // user-selected AA mode without its matching pipeline returns
        // `SkippedUnavailable` instead of falsely reporting `Recorded`
        // against a no-op draw.
        [[nodiscard]] bool SelectedAntiAliasingPipelinesAvailable() const noexcept
        {
            if (!m_PostProcessSystem.has_value())
            {
                return false;
            }
            const PostProcessAntiAliasing aa = m_PostProcessSystem->GetSettings().AntiAliasing;
            switch (aa)
            {
            case PostProcessAntiAliasing::None:
                return false;
            case PostProcessAntiAliasing::FXAA:
                return m_PostProcessFXAAPipelineLease.has_value() &&
                       m_PostProcessFXAAPipelineLease->IsValid();
            case PostProcessAntiAliasing::SMAA:
                return m_PostProcessSMAAEdgePipelineLease.has_value() &&
                       m_PostProcessSMAAEdgePipelineLease->IsValid() &&
                       m_PostProcessSMAABlendPipelineLease.has_value() &&
                       m_PostProcessSMAABlendPipelineLease->IsValid() &&
                       m_PostProcessSMAAResolvePipelineLease.has_value() &&
                       m_PostProcessSMAAResolvePipelineLease->IsValid();
            }
            return false;
        }

        // Resolve runs whichever stage matches
        // `PostProcessSettings::AntiAliasing`. When AA is `None` the
        // body is a structurally-recorded no-op (neither sub-stage
        // emits draws, and `presentSource` stays on `SceneColorLDR` via
        // `FrameRecipeFeatures::EnableAntiAliasing = false`). When AA
        // is `FXAA` or `SMAA` the matching pipeline must be available
        // — otherwise we return `SkippedUnavailable` and the
        // recipe-build site has already kept `presentSource` on
        // `SceneColorLDR`, so present still sees a usable image.
        // Falling back to "either pipeline is good enough" here would
        // hide the mismatch: with AA = FXAA + only the SMAA-resolve
        // lease (or vice versa) both pass bodies' `IsStageEnabled`
        // gate would short-circuit, neither stage would draw, the
        // helper would report `Recorded`, and the recipe could already
        // route present to the unwritten resolved attachment.
        [[nodiscard]] RenderCommandPassStatus RecordPostProcessAAResolvePass(RHI::ICommandContext& cmd,
                                                                             const RHI::CameraUBO& camera)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_PostProcessSystem.has_value() ||
                !m_PostProcessFXAAPass.has_value() ||
                !m_PostProcessSMAAPass.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            const PostProcessAntiAliasing aa = m_PostProcessSystem->GetSettings().AntiAliasing;
            const bool hasFxaaPipeline =
                m_PostProcessFXAAPipelineLease.has_value() &&
                m_PostProcessFXAAPipelineLease->IsValid();
            const bool hasSmaaResolvePipeline =
                m_PostProcessSMAAResolvePipelineLease.has_value() &&
                m_PostProcessSMAAResolvePipelineLease->IsValid();

            switch (aa)
            {
            case PostProcessAntiAliasing::None:
                // Structurally-recorded no-op: both bodies' selector
                // gate short-circuits regardless of which pipelines
                // exist; `presentSource` stays on `SceneColorLDR`.
                break;
            case PostProcessAntiAliasing::FXAA:
                if (!hasFxaaPipeline)
                {
                    return RenderCommandPassStatus::SkippedUnavailable;
                }
                m_PostProcessFXAAPass->Execute(cmd, camera);
                break;
            case PostProcessAntiAliasing::SMAA:
                if (!hasSmaaResolvePipeline)
                {
                    return RenderCommandPassStatus::SkippedUnavailable;
                }
                m_PostProcessSMAAPass->ExecuteResolve(cmd, camera);
                break;
            }
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-072 Slice A — default-recipe deferred-mode `"SurfacePass"`
        // route. Reached from the executor lambda when
        // `defaultRecipeUsesDeferred` is true and the active pass is
        // `"SurfacePass"`. Mirrors `RecordForwardSurfacePass` exactly: a
        // non-operational device → `SkippedNonOperational`; a missing
        // culling output, pass, lease, GpuWorld, or culling system →
        // `SkippedUnavailable`; otherwise `DeferredGBufferPass::Execute`
        // records the `Bind/Bind/Push/DrawIndexedIndirectCount` shape and
        // we return `Recorded`. The deferred-lighting composition body is
        // owned by GRAPHICS-072 Slice B and currently falls through to the
        // catch-all soft-skip.
        [[nodiscard]] RenderCommandPassStatus RecordDeferredGBufferPass(RHI::ICommandContext& cmd,
                                                                         const RHI::CameraUBO& camera,
                                                                         const std::uint32_t frameIndex)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_CullingOutputAvailable || !m_DeferredGBufferPass.has_value() ||
                !m_DeferredGBufferPipelineLease.has_value() ||
                !m_DeferredGBufferPipelineLease->IsValid() ||
                !m_GpuWorld.has_value() || !m_CullingSystem.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_DeferredGBufferPass->Execute(cmd, camera, *m_GpuWorld, *m_CullingSystem, frameIndex);
            return RenderCommandPassStatus::Recorded;
        }

        // GRAPHICS-072 Slice B — default-recipe `"CompositionPass"` route.
        // Reached from the executor lambda when `BuildDefaultFrameRecipe`
        // declared the deferred lighting pass (i.e. `usesDeferred`). The
        // recipe wires `CompositionPass` to read `SceneNormal`/`Albedo`/
        // `Material0` produced by the deferred-mode `SurfacePass`, so this
        // helper inherits the GBuffer pass's prerequisites in addition to
        // its own: a non-operational device → `SkippedNonOperational`;
        // missing culling output, GBuffer pass/lease, lighting pass/lease,
        // GpuWorld, or CullingSystem → `SkippedUnavailable` (so a failed
        // GBuffer record never lets lighting consume cleared/unwritten
        // attachments); otherwise `DeferredLightingPass::Execute` records
        // the `Bind/Push/Draw(3,1,0,0)` fullscreen shape and we return
        // `Recorded`. The shadow-atlas descriptor binding at `set 1,
        // binding 1` is Slice C scope.
        [[nodiscard]] RenderCommandPassStatus RecordDeferredLightingPass(RHI::ICommandContext& cmd,
                                                                          const RHI::CameraUBO& camera,
                                                                          const std::uint32_t frameIndex)
        {
            (void)frameIndex;
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            // GBuffer prerequisites: if `RecordDeferredGBufferPass` would
            // have returned `SkippedUnavailable`, the lighting pass must
            // mirror that taxonomy rather than recording against
            // uninitialized GBuffer attachments.
            if (!m_CullingOutputAvailable || !m_DeferredGBufferPass.has_value() ||
                !m_DeferredGBufferPipelineLease.has_value() ||
                !m_DeferredGBufferPipelineLease->IsValid() ||
                !m_CullingSystem.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }
            if (!m_DeferredLightingPass.has_value() ||
                !m_DeferredLightingPipelineLease.has_value() ||
                !m_DeferredLightingPipelineLease->IsValid() ||
                !m_GpuWorld.has_value())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_DeferredLightingPass->Execute(cmd, camera, *m_GpuWorld);
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

        // GRAPHICS-076 Slice A — canonical default-recipe present executor
        // helper. Mirrors `RecordMinimalDebugPresentPass` but drives the
        // canonical `PresentPass` (sampling `FrameRecipe.PresentSource`)
        // instead of the MinimalDebug scaffold (rendering a fixed-color
        // triangle). The `PresentPass::Execute()` body records the
        // `BindPipeline + Draw(3, 1, 0, 0)` shape unconditionally when its
        // pipeline handle is valid, so the helper only needs the
        // device-operational / pipeline-lease prerequisite checks the rest
        // of the default recipe's pass helpers already use; no per-pass
        // counter is added because the executor's `RenderCommandPassStatus`
        // taxonomy already distinguishes `Recorded` from
        // `SkippedNonOperational` / `SkippedUnavailable`.
        [[nodiscard]] RenderCommandPassStatus RecordPresentPass(RHI::ICommandContext& cmd)
        {
            if (m_Device == nullptr || !m_Device->IsOperational())
            {
                return RenderCommandPassStatus::SkippedNonOperational;
            }
            if (!m_PresentPipelineLease.has_value() ||
                !m_PresentPipelineLease->IsValid() ||
                !m_PresentPass.GetPipeline().IsValid())
            {
                return RenderCommandPassStatus::SkippedUnavailable;
            }

            m_PresentPass.Execute(cmd);
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
        // GRAPHICS-076 Slice A — canonical default-recipe present pass.
        // Default-constructed (no system dependency); the publisher in
        // `InitializeOperationalPassResources(device)` calls
        // `SetPipeline(...)` once the present pipeline lease is created.
        // `Execute(cmd)` records the `BindPipeline + Draw(3, 1, 0, 0)`
        // shape unconditionally when its cached pipeline handle is valid,
        // matching the contract enforced by the new `PresentPassContract`
        // tests. Lifetime contract mirrors `m_MinimalDebugPresentPass`
        // above: lives for the renderer's full lifetime, pipeline handle
        // zeroed in `Shutdown()` before `m_PipelineManager` is reset.
        PresentPass                          m_PresentPass;
        // GRAPHICS-070 — default-recipe forward surface pass. Owned as an
        // `optional` so the explicit `ForwardSystem&` constructor invariant is
        // preserved: emplaced in `Initialize()` immediately after the
        // `m_ForwardSystem` slot is constructed, and reset in `Shutdown()`
        // before the `ForwardSystem` slot is torn down.
        std::optional<ForwardSurfacePass>    m_ForwardSurfacePass;
        std::optional<ForwardLinePass>       m_ForwardLinePass;
        std::optional<ForwardPointPass>      m_ForwardPointPass;
        // GRAPHICS-073 Slice A — default-recipe shadow pass. Same lifetime
        // contract as the forward pass optionals: emplaced after
        // `m_ShadowSystem` in `Initialize()` and reset before the system in
        // `Shutdown()`.
        std::optional<ShadowPass>            m_ShadowPass;
        // GRAPHICS-072 Slice A — default-recipe deferred GBuffer pass. Same
        // lifetime contract: emplaced after `m_DeferredSystem` is initialised
        // and before the operational publisher runs; reset before
        // `m_DeferredSystem` in `Shutdown()`.
        std::optional<DeferredGBufferPass>   m_DeferredGBufferPass;
        // GRAPHICS-072 Slice B — default-recipe deferred lighting pass. Same
        // lifetime contract as the GBuffer pass: emplaced alongside
        // `m_DeferredGBufferPass` after `m_DeferredSystem` is initialised,
        // reset before `m_DeferredSystem` in `Shutdown()`.
        std::optional<DeferredLightingPass>  m_DeferredLightingPass;
        // GRAPHICS-074 Slice A — default-recipe EntityId selection pass.
        // Same lifetime contract as the forward / shadow / deferred passes:
        // emplaced after `m_SelectionSystem` is initialised and before the
        // operational publisher runs; reset before `m_SelectionSystem` in
        // `Shutdown()`.
        std::optional<EntityIdPass>          m_SelectionEntityIdPass;
        // GRAPHICS-074 Slice B — default-recipe Face / Edge / Point
        // selection ID passes. Same lifetime contract as the EntityId pass
        // above: each is emplaced after `m_SelectionSystem` is initialised
        // and before the operational publisher runs; reset before
        // `m_SelectionSystem` in `Shutdown()`.
        std::optional<FaceIdPass>            m_SelectionFaceIdPass;
        std::optional<EdgeIdPass>            m_SelectionEdgeIdPass;
        std::optional<PointIdPass>           m_SelectionPointIdPass;
        // GRAPHICS-074 Slice C — default-recipe selection outline pass.
        // Same lifetime contract as the selection-ID passes above: emplaced
        // after `m_SelectionSystem` is initialised and before the operational
        // publisher runs; reset before `m_SelectionSystem` in `Shutdown()`.
        std::optional<SelectionOutlinePass>  m_SelectionOutlinePass;
        // GRAPHICS-075 Slice A — default-recipe postprocess tonemap pass.
        // Same lifetime contract as the selection / forward / deferred /
        // shadow passes above: emplaced after `m_PostProcessSystem` is
        // initialised and before the operational publisher runs; reset
        // before `m_PostProcessSystem` in `Shutdown()`. Slices B–E add the
        // sibling Histogram / Bloom / FXAA / SMAA pass instances behind
        // the same `"PostProcessPass"` umbrella executor branch.
        std::optional<PostProcessToneMapPass> m_PostProcessToneMapPass;
        // GRAPHICS-075 Slice B.1 — default-recipe postprocess bloom pass.
        // Same lifetime contract as the tonemap pass above.
        std::optional<PostProcessBloomPass>   m_PostProcessBloomPass;
        // GRAPHICS-075 Slice C — default-recipe postprocess FXAA pass.
        // Same lifetime contract as the tonemap + bloom passes above.
        std::optional<PostProcessFXAAPass>    m_PostProcessFXAAPass;
        // GRAPHICS-075 Slice D.2a — default-recipe postprocess SMAA pass.
        // Holds the three SMAA pipelines (edge / blend / resolve) and
        // fans out across three ordered graph passes
        // (`"PostProcessAA{Edge,Blend,Resolve}Pass"`); FXAA records on
        // the resolve pass only. Mutually exclusive with FXAA per
        // `PostProcessSettings::AntiAliasing` (each per-stage Execute
        // gates on `IsStageEnabled(SMAA)`). Same lifetime contract as
        // the tonemap + bloom + FXAA passes above: emplaced after
        // `m_PostProcessSystem` is initialised and before the
        // operational publisher runs; reset before `m_PostProcessSystem`
        // in `Shutdown()`.
        std::optional<PostProcessSMAAPass>    m_PostProcessSMAAPass;
        // GRAPHICS-075 Slice E.1 — default-recipe postprocess histogram
        // compute pass. The histogram is a compute dispatch and so cannot
        // share the `"PostProcessPass"` umbrella's render-pass scope
        // (Vulkan forbids `vkCmdDispatch` inside an active render-pass
        // scope); it therefore fans out under its own ordered graph pass
        // `"PostProcessHistogramPass"` declared by the recipe. Same
        // lifetime contract as the tonemap + bloom + FXAA + SMAA passes
        // above: emplaced after `m_PostProcessSystem` is initialised and
        // before the operational publisher runs; reset before
        // `m_PostProcessSystem` in `Shutdown()`.
        std::optional<PostProcessHistogramPass> m_PostProcessHistogramPass;
        std::optional<RHI::PipelineManager::PipelineLease> m_DepthPrepassPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_DefaultDebugSurfacePipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_MinimalDebugPresentPipelineLease;
        // GRAPHICS-076 Slice A — canonical default-recipe present pipeline
        // lease. Same reset/republish pattern as the MinimalDebug present
        // lease above so a failed `Create()` leaves `m_PresentPass` in
        // the fail-closed state that `RecordPresentPass` interprets as
        // `SkippedUnavailable`.
        std::optional<RHI::PipelineManager::PipelineLease> m_PresentPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_ForwardSurfacePipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_ForwardLinePipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_ForwardPointPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_ShadowPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_DeferredGBufferPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_DeferredLightingPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_SelectionEntityIdPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_SelectionFaceIdPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_SelectionEdgeIdPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_SelectionPointIdPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_SelectionOutlinePipelineLease;
        // GRAPHICS-075 Slice A — postprocess tonemap pipeline lease. Same
        // reset/republish pattern as the selection-outline lease above so a
        // failed `Create()` leaves `m_PostProcessToneMapPass` in
        // `SkippedUnavailable` rather than retaining a stale device handle
        // across rebuilds.
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessToneMapPipelineLease;
        // GRAPHICS-075 Slice B.1 — postprocess bloom downsample + upsample
        // pipeline leases. Same reset/republish pattern as the tonemap
        // lease above so a failed `Create()` leaves the bloom helper in
        // `SkippedUnavailable` rather than retaining a stale device
        // handle across rebuilds.
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessBloomDownsamplePipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessBloomUpsamplePipelineLease;
        // GRAPHICS-075 Slice C — postprocess FXAA pipeline lease. Same
        // reset/republish pattern as the tonemap + bloom leases above so
        // a failed `Create()` leaves the FXAA helper in
        // `SkippedUnavailable` rather than retaining a stale device
        // handle across rebuilds.
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessFXAAPipelineLease;
        // GRAPHICS-075 Slice D.1 — postprocess SMAA pipeline leases (edge,
        // blend, resolve). Same reset/republish pattern as the tonemap +
        // bloom + FXAA leases above so a failed `Create()` on any stage
        // leaves that stage's bind/push/draw silenced inside
        // `PostProcessSMAAPass::Execute` while the umbrella helper still
        // returns `Recorded` per the structurally-recorded-no-op taxonomy.
        // Retained `AreaTex` / `SearchTex` LUT textures sampled by the
        // blend pipeline land in Slice D.2 alongside the recipe-side
        // `PostProcess.AATemp.{Edges,Weights}` split.
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessSMAAEdgePipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessSMAABlendPipelineLease;
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessSMAAResolvePipelineLease;
        // GRAPHICS-075 Slice E.1 — postprocess histogram compute pipeline
        // lease. Same reset/republish pattern as the tonemap + bloom +
        // FXAA + SMAA leases above so a failed `Create()` leaves the
        // histogram helper in `SkippedUnavailable` rather than retaining
        // a stale device handle across rebuilds.
        std::optional<RHI::PipelineManager::PipelineLease> m_PostProcessHistogramPipelineLease;
        // GRAPHICS-074 Slice D.1 — renderer-owned host-visible `Picking.Readback`
        // buffer. Allocated by `InitializeOperationalPassResources()` when
        // the device first becomes operational and re-used across
        // `RebuildOperationalResources()` calls as long as the expected
        // `8 * device.GetFramesInFlight()` size matches the current
        // `m_PickingReadbackBufferSize` (same pattern `ShadowSystem` follows
        // for its depth atlas). When the device reports a different
        // frames-in-flight after a swapchain rebuild the lease is dropped
        // and re-created so Slice D.2's `slot * 8` per-frame copy
        // addressing never overruns the allocation. The lease is reset in
        // `Shutdown()` before `m_BufferManager` so the destruction order
        // matches the rest of the lease-owning members above. Slice D.1
        // only exposes the buffer through `GetPickingReadbackBuffer()` /
        // `GetPickingReadbackBufferSize()`; Slice D.2 imports it into the
        // recipe and records the `CopyTextureToBuffer` calls, Slice D.3
        // drains it on `BeginFrame()`.
        std::optional<RHI::BufferManager::BufferLease> m_PickingReadbackBuffer;
        std::uint64_t                                  m_PickingReadbackBufferSize{0u};
        // GRAPHICS-074 Slice D.3 — per-slot picking-readback metadata. Sized
        // to match the buffer's `frames-in-flight` slot count whenever
        // `InitializeOperationalPassResources()` (re-)allocates the buffer,
        // so every slot in `m_PickingReadbackBuffer` has matching bookkeeping
        // entries. The metadata is populated in `ExecuteFrame` immediately
        // after the D.2 `CopyTextureToBuffer` pair records (`Pending` flips
        // to true, `IssuedFrame` captures `frame.FrameIndex`, `Request`
        // captures the world-space pick coordinates, `Invalidated` resets to
        // false), and drained at the start of the *next* `BeginFrame(...)`
        // call before `m_Device->BeginFrame(...)` acquires the next frame.
        // The drain decodes the 8 bytes the executor copied — one 4-byte
        // `EntityId` word + one 4-byte `EncodedSelectionId` word per
        // `GRAPHICS-012Q` — and routes to `SelectionSystem::PublishPickResult`
        // (`EntityId != 0` && !`Invalidated`) or `PublishNoHit()`
        // (`EntityId == 0`, `Invalidated`, or read failure). `Invalidated`
        // is set to true by `RebuildOperationalResources()` for every
        // pending slot at the time of the rebuild so a device-lost recovery
        // path publishes NoHit rather than a stale pre-rebuild hit; the
        // buffer itself is preserved across same-FIF rebuilds (Slice D.1
        // invariant), so the drained bytes are well-defined even when
        // invalidation forces a NoHit publish.
        std::vector<bool>                              m_PickingSlotPending;
        std::vector<std::uint64_t>                     m_PickingSlotIssuedFrame;
        std::vector<PickPixelRequest>                  m_PickingSlotRequest;
        std::vector<bool>                              m_PickingSlotInvalidated;
        // GRAPHICS-075 Slice E.2 — renderer-owned host-visible
        // `Histogram.Readback` buffer + per-slot drain metadata. Sized for
        // `kHistogramReadbackSlotBytes * frames-in-flight` bytes (256 uint32
        // bins per slot, one slot per in-flight frame). Allocated by
        // `InitializeOperationalPassResources()` on first operational init,
        // re-allocated on `RebuildOperationalResources()` only when the
        // device's `GetFramesInFlight()` changes (same pattern picking
        // follows). Drained from the top of `BeginFrame()` once the issuing
        // frame's `GlobalFrameNumber` has advanced; the drain decodes the
        // 256 uint32 bins and forwards them to
        // `PostProcessSystem::PublishHistogramReadback(...)`. `Invalidated`
        // is set by `RebuildOperationalResources()` for in-flight slots so
        // the drain skips the publish for slots whose pre-rebuild copy is
        // no longer trustworthy (same device-lost-recovery contract as
        // picking).
        std::optional<RHI::BufferManager::BufferLease> m_HistogramReadbackBuffer;
        std::uint64_t                                  m_HistogramReadbackBufferSize{0u};
        std::vector<bool>                              m_HistogramSlotPending;
        std::vector<std::uint64_t>                     m_HistogramSlotIssuedFrame;
        std::vector<bool>                              m_HistogramSlotInvalidated;
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
        // GRAPHICS-072 Slice A — renderer-stored lighting-path override
        // applied after `DeriveDefaultFrameRecipeFeatures(world)`. Default is
        // `Forward` so the legacy contract tests stay green; contract tests
        // can flip this to `Deferred` via `SetLightingPath(...)` to drive the
        // `"SurfacePass"` deferred executor branch added in this slice.
        FrameRecipeLightingPath              m_LightingPath{FrameRecipeLightingPath::Forward};
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
