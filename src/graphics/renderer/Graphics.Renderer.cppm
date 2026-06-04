module;

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.Renderer;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.FrameHandle;
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
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.RenderGraph;
// GRAPHICS-077 — re-export the upload helper module so consumers of
// `RenderGraphFrameStats::TransientDebugUpload` (e.g. contract tests,
// editor diagnostics) reach the `TransientDebugUploadDiagnostics`
// struct without separately importing this internal renderer module.
// The helper interfaces themselves (`ITransientDebugUploadHelper`,
// `TransientDebugTriangleUploadResult`) ride along for the same
// consumers but are not part of the renderer's narrow public API.
export import Extrinsic.Graphics.TransientDebugUploadHelper;
// GRAPHICS-078 Slice B — re-export the visualization-overlay upload
// helper module so consumers of
// `RenderGraphFrameStats::VisualizationOverlayUpload` (e.g. contract
// tests, editor diagnostics) reach the
// `VisualizationOverlayUploadDiagnostics` struct without separately
// importing this internal renderer module. The helper interfaces
// themselves (`IVisualizationOverlayUploadHelper`,
// `VisualizationVectorFieldUploadResult`) ride along for the same
// consumers but are not part of the renderer's narrow public API. The
// `VisualizationOverlayPass` class is also re-exported so contract
// tests that name the pass type (e.g. for push-constant size checks)
// keep their import shape unchanged.
export import Extrinsic.Graphics.VisualizationOverlayUploadHelper;
// GRAPHICS-079 Slice C — re-export the ImGui upload helper result packets for
// pass-level contract tests. The renderer still owns the concrete helper.
export import Extrinsic.Graphics.ImGuiUploadHelper;
export import Extrinsic.Graphics.Pass.VisualizationOverlay;
// RUNTIME-082 Slice D — `RuntimeRenderSnapshotBatch` carries spans of
// `SpatialDebugAabb` / `SpatialDebugHierarchyNode` / `SpatialDebugSplitPlane`
// / `SpatialDebugWireEdge` produced by the runtime spatial-debug adapter
// pump. The types live in `Extrinsic.Graphics.SpatialDebugVisualizers`; the
// import is local (non-export) because the existing graphics consumers that
// build the matching wireframe packets already import the module directly.
import Extrinsic.Graphics.SpatialDebugVisualizers;
import Extrinsic.Core.Config.Render;

namespace Extrinsic::Graphics
{
    export enum class RenderCommandPassStatus : std::uint8_t
    {
        Recorded,
        SkippedNonOperational,
        SkippedUnavailable,
    };

    export struct RenderGraphCompileStats
    {
        bool Succeeded = false;
        std::uint32_t PassCount = 0;
        std::uint32_t CulledPassCount = 0;
        std::uint32_t ResourceCount = 0;
        std::uint32_t BarrierCount = 0;
        std::uint32_t QueueHandoffEdgeCount = 0;
        std::uint32_t CrossQueueTimelineEdgeCount = 0;
        std::uint32_t CrossQueueTimelineSignalCount = 0;
        std::uint32_t CrossQueueTimelineWaitCount = 0;
        std::uint32_t CrossQueueOwnershipTransferCount = 0;
        std::uint64_t TransientMemoryEstimateBytes = 0;
        std::uint64_t TimeMicros = 0;
    };

    export struct RenderGraphExecuteStats
    {
        bool Succeeded = false;
        bool DeviceOperational = false;
        std::uint64_t TimeMicros = 0;
    };

    export struct RenderGraphCommandPassStats
    {
        std::string Name{};
        RenderCommandPassStatus Status = RenderCommandPassStatus::SkippedUnavailable;
    };

    export struct RenderGraphCommandRecordStats
    {
        std::uint32_t Recorded = 0;
        std::uint32_t Skipped = 0;
        std::uint32_t SkippedNonOperational = 0;
        std::uint32_t SkippedUnavailable = 0;
        std::vector<RenderGraphCommandPassStats> Passes{};
    };

    export struct RenderGraphFrameStats
    {
        RenderGraphCompileStats Compile{};
        RenderGraphExecuteStats Execute{};
        RenderGraphCommandRecordStats CommandRecords{};
        std::string DebugDump{};
        std::string Diagnostic{};
        std::string LifecycleDiagnostic{};
        // GRAPHICS-037D Slice D — count of frames in which the default
        // recipe produced an accepted multi-queue submit plan containing an
        // `AsyncCompute` batch. Stays at zero when the backend has no async
        // queue, when the framegraph demotes optional queues to graphics, or
        // when the backend rejects the submit-plan seam.
        std::uint32_t AsyncComputeUtilizedFrames = 0;
        // GRAPHICS-076E — count of frames in which the opt-in default-recipe
        // backbuffer-to-host readback seam recorded the
        // `Present → TransferSrc → CopyImageToBuffer → Present` triplet.
        // Stays at zero unless `SetDefaultRecipeBackbufferReadbackBuffer()` was
        // configured with a valid HostVisible+TransferDst buffer and the device
        // is operational during the frame.
        std::uint32_t DefaultRecipeBackbufferReadbackCopyCount = 0;
        // GRAPHICS-077E — count of frames in which the opt-in transient-debug
        // backbuffer-to-host readback seam recorded the same
        // `Present -> TransferSrc -> CopyImageToBuffer -> Present` triplet
        // after the default-recipe graph completed. Unlike the canonical
        // default-recipe counter above, this increments only when
        // `"TransientDebugSurfacePass"` recorded in the same frame, a valid
        // transient-debug readback buffer is armed, and the device is
        // operational.
        std::uint32_t TransientDebugBackbufferReadbackCopyCount = 0;
        // GRAPHICS-078E — count of frames in which the opt-in visualization-
        // overlay backbuffer-to-host readback seam recorded the same
        // `Present -> TransferSrc -> CopyImageToBuffer -> Present` triplet
        // after the default-recipe graph completed. This increments only when
        // `"VisualizationOverlayPass"` recorded in the same frame, a valid
        // visualization-overlay readback buffer is armed, and the device is
        // operational.
        std::uint32_t VisualizationOverlayBackbufferReadbackCopyCount = 0;
        // GRAPHICS-074 Slice D.2 — count of frames in which the default
        // recipe's PickingPass executor branch recorded the picking-readback
        // copy pair (EntityId + PrimitiveId → renderer-owned
        // `Picking.Readback` buffer at slot
        // `frame.FrameIndex % frames-in-flight`). Each operational frame with
        // a pending pick request increments by 1 (the pair records together
        // or not at all); stays at zero when no pick is pending, when the
        // device is non-operational, or when the picking pass is otherwise
        // gated off. Slice D.3 builds on top of this counter for the
        // `BeginFrame()` drain + `SelectionSystem::PublishPickResult` /
        // `PublishNoHit` routing.
        std::uint32_t PickingReadbackCopyCount = 0;
        // GRAPHICS-075 Slice E.2 — count of frames in which the default
        // recipe's `PostProcessHistogramPass` executor branch recorded the
        // histogram-readback `CopyBuffer(PostProcess.Histogram →
        // Histogram.Readback @ slot * 1024)` after the compute dispatch.
        // Each operational frame with a valid renderer-owned readback buffer
        // increments by 1; stays at zero when the device is non-operational,
        // when the readback buffer is unavailable, or when the histogram
        // stage itself is gated off. The `BeginFrame()`-side drain consumes
        // pending slots and forwards the 256-bin payload to
        // `PostProcessSystem::PublishHistogramReadback(...)`.
        std::uint32_t HistogramReadbackCopyCount = 0;
        // GRAPHICS-076 Slice B — count of frames in which the default
        // recipe's canonical `DebugViewPass` executor branch recorded the
        // fullscreen `BindPipeline + PushConstants + Draw(3, 1, 0, 0)`
        // shape. Increments by 1 per operational frame in which the
        // resolved selection is enabled and the pipeline lease is valid;
        // stays at zero when the device is non-operational, when
        // `DebugViewSettings::Enabled` is false, when the pipeline lease
        // is missing, or when the resolved selection's fallback path also
        // disabled the pass (`DebugViewFallbackReason::FallbackUnavailable`).
        std::uint32_t DebugViewPassExecutions = 0;
        // GRAPHICS-076 Slice B — count of frames in which the default
        // recipe's `DebugViewSystem::ResolveSelection(...)` reported
        // `UsedFallback = true` because the requested resource was
        // missing / disabled / unsupported and the system substituted the
        // configured fallback resource. Surfaces deterministically the
        // diagnostic that the task's "no silent failure on invalid
        // resource" acceptance criterion requires; tests assert this
        // counter increments by exactly 1 per frame in which the request
        // resolved through fallback.
        std::uint32_t DebugViewFallbackInvocationCount = 0;
        // GRAPHICS-077 Slice A — aggregate diagnostics for the
        // `TransientDebugSurfacePass` upload + recording path. All
        // counters stay at zero in Slice A (no pipelines, scaffold
        // executor branch only). Slice B starts populating the triangle
        // counters; Slice C starts populating the line + point counters.
        // Reset per-frame through the existing
        // `m_LastRenderGraphStats = {}` cadence in `ExecuteFrame()`.
        TransientDebugUploadDiagnostics TransientDebugUpload{};
        // GRAPHICS-078 Slice A — aggregate diagnostics for the
        // `VisualizationOverlayPass` upload + recording path. All
        // counters stay at zero in Slice A (no pipelines, scaffold
        // executor branch only) except `MissingPipelineSkipCount` which
        // increments once per operational-scaffold frame to distinguish
        // "feature on but pipeline missing" from "feature off". Slice B
        // starts populating the vector-field counters; Slice C starts
        // populating the isoline counters. Reset per-frame through the
        // existing `m_LastRenderGraphStats = {}` cadence in
        // `ExecuteFrame()`.
        VisualizationOverlayUploadDiagnostics VisualizationOverlayUpload{};
    };

    export struct RuntimeRenderSnapshotBatch
    {
        std::span<const TransformSyncRecord>     Transforms{};
        std::span<const LightSnapshot>           Lights{};
        std::span<const VisualizationSyncRecord> Visualizations{};
        std::span<const VisualizationAttributeBufferPacket> VisualizationAttributeBuffers{};
        std::span<const ScalarAttributePacket>              VisualizationScalars{};
        std::span<const ColorAttributePacket>               VisualizationColors{};
        std::span<const VectorFieldOverlayPacket>           VisualizationVectorFields{};
        std::span<const IsolineOverlayPacket>               VisualizationIsolines{};
        std::span<const HtexPatchPreviewAtlasPacket>        VisualizationHtexAtlases{};
        std::span<const FragmentBakeAtlasPacket>            VisualizationFragmentBakeAtlases{};
        std::span<const DebugLinePacket>         DebugLines{};
        std::span<const DebugPointPacket>        DebugPoints{};
        std::span<const DebugTrianglePacket>     DebugTriangles{};
        std::span<const TransformGizmoRenderPacket> TransformGizmos{};

        // RUNTIME-082 Slice D — spatial-debug snapshot spans produced by
        // the runtime adapter pump. Aggregated by
        // `RenderExtractionCache::ExtractAndSubmit` from per-entity
        // `ECS::Components::SpatialDebugBinding` lookups through the
        // cache-owned `SpatialDebugAdapterRegistry`. Consumers that build
        // wireframe packets feed these into
        // `BuildSpatialDebug{Bounds,Hierarchy,SplitPlane,ConvexHull,...}Wireframes`.
        // Default-empty for backends and tests that do not exercise the
        // pump.
        std::span<const SpatialDebugAabb>          SpatialDebugBounds{};
        std::span<const SpatialDebugHierarchyNode> SpatialDebugHierarchyNodes{};
        std::span<const SpatialDebugSplitPlane>    SpatialDebugSplitPlanes{};
        std::span<const glm::vec3>                 SpatialDebugConvexHullVertices{};
        std::span<const SpatialDebugWireEdge>      SpatialDebugConvexHullEdges{};
        std::span<const glm::vec3>                 SpatialDebugPointMarkers{};

        // RUNTIME-089 Slice B — runtime/editor selection snapshot identity,
        // aggregated by `RenderExtractionCache::ExtractAndSubmit` from the
        // runtime-owned `SelectionController`. `SubmitRuntimeSnapshots` copies
        // these into stable renderer storage and `ExtractRenderWorld` surfaces
        // them as `RenderWorld::Selection` so `SelectionOutlinePass` can outline
        // selected/hovered renderables without graphics reading live ECS. The
        // identity-only fields (selected ids, hovered id, has-hovered) are
        // filled here; the outline styling on `SelectionSnapshot` keeps its
        // recipe defaults. Default-empty when no controller is wired.
        std::span<const std::uint32_t> SelectionSelectedStableIds{};
        std::uint32_t                  SelectionHoveredStableId{0u};
        bool                           SelectionHasHovered{false};
    };

    export class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        // ── Subsystem lifecycle ───────────────────────────────────────────

        virtual void Initialize(RHI::IDevice& device) = 0;

        // Runtime calls this after a device that was initialized in a
        // fail-closed/non-operational state becomes operational. Implementations
        // must rebuild GPU-only state through RHI managers without importing or
        // special-casing concrete backends.
        [[nodiscard]] virtual bool RebuildOperationalResources(RHI::IDevice& device) = 0;

        virtual void Shutdown() = 0;

        virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;

        // ── Per-frame phases (called in this order every frame) ───────────
        //
        //  1. BeginFrame     — acquire swapchain image, open command contexts.
        //                      Returns false if the frame must be skipped
        //                      (out-of-date swapchain, device lost, minimized).
        //
        //  2. ExtractRenderWorld — snapshot immutable render data from the
        //                      committed world state.  No mutable ECS/asset
        //                      references survive this call.
        //
        //  3. PrepareFrame   — CPU frustum cull, sort, build draw-packet
        //                      lists, upload per-frame staging data.
        //
        //  4. ExecuteFrame   — record and submit GPU command buffers.
        //
        //  5. EndFrame       — release frame-context ownership back to the
        //                      ring.  Returns IDevice::GetGlobalFrameNumber()
        //                      after the device EndFrame call. This is the
        //                      device's post-EndFrame global frame counter,
        //                      not necessarily the just-completed
        //                      frame.FrameIndex.

        [[nodiscard]] virtual bool BeginFrame(RHI::FrameHandle& outFrame) = 0;

        // Runtime snapshot payloads are copied into renderer-owned stable
        // storage. `storageSlot` is a small caller-owned ring index (slot 0 is
        // the legacy/default path); pipelined extraction passes the
        // RenderWorldPool back slot here and later extracts from the acquired
        // front/previous-front slot. The RuntimeRenderSnapshotBatch shape stays
        // unchanged: only the renderer-side retained lifetime is multi-buffered.
        virtual void SubmitRuntimeSnapshots(const RuntimeRenderSnapshotBatch& snapshots,
                                            std::uint32_t storageSlot = 0u) = 0;

        // GRAPHICS-079 Slice A — runtime composition hands the engine-owned
        // `ImGuiOverlaySystem` (the producer the `RUNTIME-090` adapter submits
        // to) to the renderer so the consumer-side `Pass.ImGui` reads the same
        // overlay state. Runtime owns composition; this is the allowed
        // `runtime -> graphics` handoff edge. Passing `nullptr` detaches the
        // overlay so the `ImGuiPass` route reports `SkippedUnavailable`.
        virtual void SetImGuiOverlaySystem(ImGuiOverlaySystem* overlay) noexcept = 0;
        // GRAPHICS-079 Slice B — diagnostic observer for runtime composition
        // tests. It reports whether the renderer currently has an ImGui
        // consumer bound to a runtime-owned overlay; it does not expose the
        // borrowed overlay or allow graphics to call back into runtime.
        [[nodiscard]] virtual bool HasImGuiOverlaySystem() const noexcept = 0;

        [[nodiscard]] virtual RenderWorld ExtractRenderWorld(
            const RenderFrameInput& input,
            std::uint32_t storageSlot = 0u) = 0;

        virtual void PrepareFrame(RenderWorld& world) = 0;

        virtual void ExecuteFrame(const RHI::FrameHandle& frame,
                                  const RenderWorld&      world) = 0;

        [[nodiscard]] virtual std::uint64_t EndFrame(
            const RHI::FrameHandle& frame) = 0;

        // ── Resource managers ─────────────────────────────────────────────
        // Initialised inside Initialize() once IDevice is live.
        // Shutdown() destroys them in dependency order so no manager
        // outlives a resource it references.

        [[nodiscard]] virtual RHI::BufferManager&   GetBufferManager()   = 0;
        [[nodiscard]] virtual RHI::TextureManager&  GetTextureManager()  = 0;
        [[nodiscard]] virtual RHI::SamplerManager&  GetSamplerManager()  = 0;
        [[nodiscard]] virtual RHI::PipelineManager& GetPipelineManager() = 0;
        [[nodiscard]] virtual GpuWorld&              GetGpuWorld()       = 0;
        [[nodiscard]] virtual MaterialSystem&        GetMaterialSystem()  = 0;
        [[nodiscard]] virtual ColormapSystem&        GetColormapSystem()  = 0;
        [[nodiscard]] virtual VisualizationSyncSystem& GetVisualizationSyncSystem() = 0;
        [[nodiscard]] virtual CullingSystem&         GetCullingSystem()   = 0;
        [[nodiscard]] virtual TransformSyncSystem&   GetTransformSyncSystem() = 0;
        [[nodiscard]] virtual LightSystem&           GetLightSystem()     = 0;
        [[nodiscard]] virtual SelectionSystem&       GetSelectionSystem() = 0;
        [[nodiscard]] virtual ForwardSystem&         GetForwardSystem()   = 0;
        [[nodiscard]] virtual DeferredSystem&        GetDeferredSystem()  = 0;
        [[nodiscard]] virtual PostProcessSystem&     GetPostProcessSystem() = 0;
        [[nodiscard]] virtual ShadowSystem&          GetShadowSystem()    = 0;
        [[nodiscard]] virtual const RenderGraphFrameStats& GetLastRenderGraphStats() const = 0;

        // GRAPHICS-031A — accessor for the canonical missing-material fallback
        // pipeline. Returns the operational device-side handle when the
        // pipeline has been compiled (operational device path), and an
        // invalid handle otherwise. The pipeline state itself is the
        // canonical default-debug-surface recipe; see GetDefaultDebugSurfacePipelineDesc().
        [[nodiscard]] virtual RHI::PipelineHandle GetDefaultDebugSurfacePipeline() const noexcept = 0;

        // GRAPHICS-031A — descriptor used to compile the default-debug-surface
        // pipeline. Returned by value so callers can assert byte-identical
        // republish across InitializeOperationalPassResources() invocations
        // (initial init and RebuildOperationalResources).
        [[nodiscard]] virtual RHI::PipelineDesc GetDefaultDebugSurfacePipelineDesc() const noexcept = 0;

        // GRAPHICS-070 — accessors for the default-recipe forward surface
        // pipeline. `GetForwardSurfacePipeline()` returns the operational
        // device-side handle (or an invalid handle when the device path is
        // not operational); `GetForwardSurfacePipelineDesc()` returns the
        // canonical descriptor so contract tests can assert byte-identical
        // republish across `InitializeOperationalPassResources()` and
        // `RebuildOperationalResources()`.
        [[nodiscard]] virtual RHI::PipelineHandle GetForwardSurfacePipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetForwardSurfacePipelineDesc() const noexcept = 0;

        // GRAPHICS-071 — accessors for the default-recipe retained line/point
        // forward pipelines. The handles are invalid until an operational
        // device path publishes the leases; descriptors remain deterministic so
        // contract tests can assert byte-identical rebuild behavior.
        [[nodiscard]] virtual RHI::PipelineHandle GetForwardLinePipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetForwardLinePipelineDesc() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineHandle GetForwardPointPipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetForwardPointPipelineDesc() const noexcept = 0;

        // GRAPHICS-073 (Slice A) — accessor for the default-recipe depth-only
        // shadow pipeline. Handle is invalid until an operational device path
        // publishes the lease; the descriptor remains deterministic so contract
        // tests can assert byte-identical rebuild behavior.
        [[nodiscard]] virtual RHI::PipelineHandle GetShadowPipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetShadowPipelineDesc() const noexcept = 0;

        // GRAPHICS-072 (Slice A) — accessor for the default-recipe deferred
        // GBuffer pipeline (vertex `surface.vert.spv` + fragment
        // `surface_gbuffer.frag.spv`, three color targets `SceneNormal` /
        // `Albedo` / `Material0`, `D32_FLOAT` depth). Handle is invalid until
        // an operational device path publishes the lease; the descriptor
        // remains deterministic so contract tests can assert byte-identical
        // rebuild behavior.
        [[nodiscard]] virtual RHI::PipelineHandle GetDeferredGBufferPipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetDeferredGBufferPipelineDesc() const noexcept = 0;

        // GRAPHICS-072 (Slice B) — accessor for the default-recipe deferred
        // lighting pipeline (vertex `post_fullscreen.vert.spv` + fragment
        // `deferred/lighting.frag.spv`, single `SceneColorHDR` RGBA16F color
        // target, no depth target). Handle is invalid until an operational
        // device path publishes the lease; the descriptor remains
        // deterministic so contract tests can assert byte-identical rebuild
        // behavior.
        [[nodiscard]] virtual RHI::PipelineHandle GetDeferredLightingPipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetDeferredLightingPipelineDesc() const noexcept = 0;

        // GRAPHICS-074 (Slice A + recipe-side follow-up) — accessor for the
        // default-recipe EntityId selection pipeline (vertex
        // `selection/entity_id.vert.spv` + fragment
        // `selection/entity_id.frag.spv`, two R32_UINT color targets
        // `EntityId` and `PrimitiveId`, `D32_FLOAT` depth attachment,
        // `DepthOp::Equal` + `DepthWriteEnable=false`). The depth-equal
        // shape matches `BuildDefaultFrameRecipe`, which now orders
        // `PickingPass` after `DepthPrepass` and declares
        // `Read(SceneDepth, DepthRead)` on the picking pass so the
        // pipeline samples the prepass-populated nearest-surface depth and
        // never last-fragment-wins into `EntityId`/`PrimitiveId`. The
        // recipe also gates the pass on `EnablePicking &&
        // EnableDepthPrepass` so this pipeline is only requested when a
        // populated `SceneDepth` exists. Handle is invalid until an
        // operational device path publishes the lease; the descriptor
        // remains deterministic so contract tests can assert byte-identical
        // rebuild behavior. Slices B/C/D add the Face/Edge/Point selection
        // pipelines (same depth-equal shape), the outline pipeline, and the
        // `Picking.Readback` drain.
        [[nodiscard]] virtual RHI::PipelineHandle GetSelectionEntityIdPipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetSelectionEntityIdPipelineDesc() const noexcept = 0;

        // GRAPHICS-074 (Slice B) — accessors for the default-recipe Face /
        // Edge / Point selection ID pipelines. Each pipeline mirrors the
        // EntityId pipeline's depth-equal / depth-write-off / two-R32_UINT
        // color-target shape required by the recipe's `PickingPass`
        // render-pass declaration, differing only in primitive topology
        // (`TriangleList` / `LineList` / `PointList`), cull bucket (consumed
        // through the respective `FaceIdPass` / `EdgeIdPass` / `PointIdPass`
        // `Execute(...)`), and the shader-side `EncodeSelectionId(domain,
        // payload)` value packed into `PrimitiveId`. Handles are invalid
        // until an operational device path publishes the leases; descriptors
        // remain deterministic so contract tests can assert byte-identical
        // rebuild behavior.
        [[nodiscard]] virtual RHI::PipelineHandle GetSelectionFaceIdPipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetSelectionFaceIdPipelineDesc() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineHandle GetSelectionEdgeIdPipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetSelectionEdgeIdPipelineDesc() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineHandle GetSelectionPointIdPipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetSelectionPointIdPipelineDesc() const noexcept = 0;

        // GRAPHICS-074 (Slice C) — accessor for the default-recipe selection
        // outline pipeline (vertex `post_fullscreen.vert.spv` + fragment
        // `selection_outline.frag.spv`, single color target matching the
        // backbuffer format the recipe uses for the `SelectionOutline`
        // texture, depth-test/write off). The pipeline is a fullscreen
        // triangle (no vertex inputs) that the recipe's
        // `"SelectionOutlinePass"` branch binds and draws into the
        // `SelectionOutline` color target. Handle is invalid until an
        // operational device path publishes the lease; the descriptor
        // remains deterministic so contract tests can assert byte-identical
        // rebuild behavior. Slice D adds the `Picking.Readback` buffer +
        // drain + `PublishPickResult` / `PublishNoHit` wiring.
        [[nodiscard]] virtual RHI::PipelineHandle GetSelectionOutlinePipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetSelectionOutlinePipelineDesc() const noexcept = 0;

        // GRAPHICS-075 (Slice A) — accessor for the default-recipe postprocess
        // tonemap pipeline (vertex `post_fullscreen.vert.spv` + fragment
        // `post_tonemap.frag.spv`, single backbuffer-format color target, no
        // depth target, `PushConstantSize = sizeof(PostProcessPushConstants)`).
        // The pipeline is a fullscreen triangle that the `"PostProcessPass"`
        // umbrella executor branch binds and draws into the recipe's
        // `SceneColorLDR` color target after reading the prior frame's
        // `SceneColorHDR`. Handle is invalid until an operational device path
        // publishes the lease; the descriptor remains deterministic so
        // contract tests can assert byte-identical rebuild behavior. Slices
        // B–E add the bloom / FXAA / SMAA / histogram pipelines behind the
        // same umbrella branch.
        [[nodiscard]] virtual RHI::PipelineHandle GetPostProcessToneMapPipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetPostProcessToneMapPipelineDesc() const noexcept = 0;

        // GRAPHICS-075 (Slice B.1) — accessors for the default-recipe
        // postprocess bloom pipelines: a fullscreen 13-tap downsample
        // (`post_fullscreen.vert.spv` + `post_bloom_downsample.frag.spv`)
        // and a 9-tap tent-filter upsample (`post_fullscreen.vert.spv` +
        // `post_bloom_upsample.frag.spv`). Both pipelines target the
        // recipe's `PostProcess.BloomScratch` RGBA16F transient (`RGBA16_FLOAT`
        // is the BloomScratch declaration in `BuildDefaultFrameRecipe`), no
        // depth attachment, `PushConstantSize =
        // sizeof(PostProcessBloomDownsamplePushConstants)` /
        // `sizeof(PostProcessBloomUpsamplePushConstants)` (each 16 bytes
        // matching the shader's std430 push block). Handles are invalid
        // until an operational device publishes the leases; the descriptors
        // remain deterministic so contract tests can assert byte-identical
        // rebuild behavior. Slice B.2 keeps the same pipeline shapes and
        // adds per-mip iteration + recipe-side `BloomScratch.MipLevels` +
        // the multi-mip barrier-sequence contract test.
        [[nodiscard]] virtual RHI::PipelineHandle GetPostProcessBloomDownsamplePipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetPostProcessBloomDownsamplePipelineDesc() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineHandle GetPostProcessBloomUpsamplePipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetPostProcessBloomUpsamplePipelineDesc() const noexcept = 0;

        // GRAPHICS-075 (Slice C) — accessor for the default-recipe
        // postprocess FXAA pipeline (vertex `post_fullscreen.vert.spv` +
        // fragment `post_fxaa.frag.spv`, single backbuffer-format color
        // target, no depth target, `PushConstantSize =
        // sizeof(PostProcessFXAAPushConstants)` — 20 bytes mirroring the
        // shader's `vec2 InvResolution + float ContrastThreshold + float
        // RelativeThreshold + float SubpixelBlending` std430 push block).
        // The pipeline is a fullscreen triangle that the `"PostProcessPass"`
        // umbrella executor branch binds and draws after tonemap when
        // `PostProcessSettings::AntiAliasing == FXAA`; with `None` the
        // pass body short-circuits and the helper still reports
        // `Recorded` under the umbrella's accumulator (the same
        // "structurally-recorded no-op" taxonomy Slice B.1 added for
        // bloom-disabled). Handle is invalid until an operational device
        // path publishes the lease; the descriptor remains deterministic
        // so contract tests can assert byte-identical rebuild behavior.
        // SMAA + retained `AreaTex`/`SearchTex` LUTs land with Slice D;
        // histogram compute + readback drain lands with Slice E behind
        // the same umbrella branch.
        [[nodiscard]] virtual RHI::PipelineHandle GetPostProcessFXAAPipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetPostProcessFXAAPipelineDesc() const noexcept = 0;

        // GRAPHICS-075 (Slice D.2a) — accessors for the three SMAA
        // pipelines (vertex `post_fullscreen.vert.spv` paired with
        // fragments `post_smaa_edge.frag.spv` /
        // `post_smaa_blend.frag.spv` / `post_smaa_resolve.frag.spv`).
        // The recipe's `PostProcess.AATemp.{Edges,Weights,Resolved}`
        // split allocates three matched-format AA transients, so the
        // pipeline color-target formats are:
        // - edge → `RG8_UNORM` (matches `AATemp.Edges`);
        // - blend → `RGBA8_UNORM` (matches `AATemp.Weights`);
        // - resolve → backbuffer format (matches `AATemp.Resolved`).
        // Per-stage push constants:
        // - edge: `PushConstantSize =
        //   sizeof(PostProcessSMAAEdgePushConstants)` (16 bytes, `vec2
        //   InvResolution + float EdgeThreshold + float _pad0` std430
        //   mirroring `post_smaa_edge.frag`);
        // - blend: `PushConstantSize =
        //   sizeof(PostProcessSMAABlendPushConstants)` (16 bytes, `vec2
        //   InvResolution + int MaxSearchSteps + int MaxSearchStepsDiag`
        //   std430 mirroring `post_smaa_blend.frag`);
        // - resolve: `PushConstantSize =
        //   sizeof(PostProcessSMAAResolvePushConstants)` (16 bytes, `vec2
        //   InvResolution + float _pad0 + float _pad1` std430 mirroring
        //   `post_smaa_resolve.frag`).
        // All three pipelines have no depth attachment. The canonical
        // 20-byte `PostProcessPushConstants` block is intentionally not
        // reused per the standing "Shader push-constant compatibility
        // policy": pushing it under any SMAA shader's std430 push block
        // would alias `Exposure` / `Gamma` / etc. onto `InvResolution.x`
        // / `InvResolution.y` / threshold scalars and produce
        // visually-meaningless SMAA output. Handles are invalid until an
        // operational device path publishes the leases; the descriptors
        // remain deterministic so contract tests can assert byte-
        // identical rebuild behavior. Retained `AreaTex` / `SearchTex`
        // LUT textures (sampled by the blend pipeline) +
        // exposure-adaptation history buffer land in Slice D.2b
        // alongside the device-aware `PostProcessSystem::Initialize`
        // overload.
        [[nodiscard]] virtual RHI::PipelineHandle GetPostProcessSMAAEdgePipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetPostProcessSMAAEdgePipelineDesc() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineHandle GetPostProcessSMAABlendPipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetPostProcessSMAABlendPipelineDesc() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineHandle GetPostProcessSMAAResolvePipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetPostProcessSMAAResolvePipelineDesc() const noexcept = 0;

        // GRAPHICS-075 (Slice E.1) — accessor for the default-recipe
        // postprocess histogram compute pipeline (`post_histogram.comp`,
        // no vertex/fragment stages, `PushConstantSize =
        // sizeof(PostProcessHistogramPushConstants)` — 16 bytes mirroring
        // the shader's `uint Width + uint Height + float MinLogLum +
        // float RangeLogLum` std430 push block). The pipeline is a
        // compute dispatch (`local_size_x = local_size_y = 16`) bound
        // and dispatched by the new ordered graph pass
        // `"PostProcessHistogramPass"` declared by the recipe with
        // `Read(SceneColorHDR, ShaderRead)` +
        // `Write(PostProcess.Histogram, BufferUsage::ShaderWrite)` so
        // the framegraph compiler emits the read-after-write barrier
        // the shader needs and the dispatch executes outside any
        // render-pass scope (Vulkan rejects `vkCmdDispatch` inside an
        // active render-pass scope, which is why the histogram cannot
        // share the `"PostProcessPass"` umbrella's render-pass scope).
        // Handle is invalid until an operational device path publishes
        // the lease; the descriptor remains deterministic so contract
        // tests can assert byte-identical rebuild behavior. Slice E.2
        // adds the renderer-owned host-visible `Histogram.Readback`
        // buffer + `BeginFrame()`-side drain + `PublishHistogramReadback`
        // wiring that consumes the exposure-adaptation history buffer.
        [[nodiscard]] virtual RHI::PipelineHandle GetPostProcessHistogramPipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetPostProcessHistogramPipelineDesc() const noexcept = 0;

        // GRAPHICS-074 (Slice D.1) — accessor for the renderer-owned host-
        // visible `Picking.Readback` buffer. The buffer is sized for
        // `8 * frames-in-flight` bytes (one 4-byte `EntityId` word + one
        // 4-byte `EncodedSelectionId` word per in-flight frame slot per
        // `GRAPHICS-012Q`'s `EncodedSelectionId` payload layout), allocated
        // with `HostVisible = true` and `BufferUsage::TransferDst` so the
        // recipe (Slice D.2) can import it as the destination of a
        // `CopyTextureToBuffer(EntityId/PrimitiveId, ...)` after the
        // selection-ID sub-passes record, and the renderer (Slice D.3) can
        // map it on `BeginFrame()` once the issuing frame has completed.
        // Handle is invalid until an operational device path allocates the
        // lease; size accessor returns the allocation size in bytes (0 when
        // the buffer is not yet allocated). Slices D.2/D.3 wire the
        // recipe-side import, per-frame copy, drain, and
        // `PublishPickResult` / `PublishNoHit` routing.
        [[nodiscard]] virtual RHI::BufferHandle GetPickingReadbackBuffer() const noexcept = 0;
        [[nodiscard]] virtual std::uint64_t GetPickingReadbackBufferSize() const noexcept = 0;

        // GRAPHICS-075 Slice E.2 — accessor for the renderer-owned host-
        // visible `Histogram.Readback` buffer. Sized for
        // `1024 * frames-in-flight` bytes (256 uint32 bins per slot, one
        // slot per in-flight frame), allocated with `HostVisible = true`
        // and `BufferUsage::TransferDst` so the recipe can import it as the
        // destination of a `CopyBuffer(PostProcess.Histogram →
        // Histogram.Readback)` after the histogram dispatch, and the
        // renderer can map it on `BeginFrame()` once the issuing frame has
        // completed. Handle is invalid until an operational device path
        // allocates the lease; size accessor returns the allocation size in
        // bytes (0 when the buffer is not yet allocated). The drain
        // publishes the 256-bin payload to
        // `PostProcessSystem::PublishHistogramReadback(...)`.
        [[nodiscard]] virtual RHI::BufferHandle GetHistogramReadbackBuffer() const noexcept = 0;
        [[nodiscard]] virtual std::uint64_t GetHistogramReadbackBufferSize() const noexcept = 0;

        // GRAPHICS-072 (Slice A) — test seam for the default recipe's runtime
        // lighting path. `DeriveDefaultFrameRecipeFeatures` derives a default
        // of `Forward` so the legacy contract tests stay green; the renderer
        // overrides that derivation with this stored value when set to
        // `Deferred` or `Hybrid`, allowing contract tests to drive the
        // deferred surface/composition executor branches without re-deriving
        // features at the call site. Default is `Forward`.
        virtual void SetLightingPath(FrameRecipeLightingPath path) noexcept = 0;
        [[nodiscard]] virtual FrameRecipeLightingPath GetLightingPath() const noexcept = 0;

        // GRAPHICS-076E — opt-in backbuffer-to-host readback wiring for the
        // canonical default recipe's visible-triangle parity harness. The
        // caller owns the buffer lifetime and passes a raw HostVisible +
        // TransferDst handle. The renderer records the
        // Present→TransferSrc→CopyTextureToBuffer→Present triplet when the
        // device is operational. Calling with an invalid handle disables the
        // path (default post-Initialize state).
        virtual void SetDefaultRecipeBackbufferReadbackBuffer(RHI::BufferHandle handle) noexcept = 0;

        [[nodiscard]] virtual RHI::BufferHandle GetDefaultRecipeBackbufferReadbackBuffer() const noexcept = 0;

        // GRAPHICS-077E — opt-in backbuffer-to-host readback wiring for
        // transient-debug pixel parity. The caller owns the HostVisible +
        // TransferDst buffer lifetime. The renderer records the copy triplet
        // only on operational frames where `TransientDebugSurfacePass`
        // recorded; invalid handle disables the path.
        virtual void SetTransientDebugBackbufferReadbackBuffer(RHI::BufferHandle handle) noexcept = 0;

        [[nodiscard]] virtual RHI::BufferHandle GetTransientDebugBackbufferReadbackBuffer() const noexcept = 0;

        // GRAPHICS-078E — opt-in backbuffer-to-host readback wiring for
        // visualization-overlay pixel parity. The caller owns the
        // HostVisible + TransferDst buffer lifetime. The renderer records the
        // copy triplet only on operational frames where
        // `VisualizationOverlayPass` recorded; invalid handle disables the
        // path.
        virtual void SetVisualizationOverlayBackbufferReadbackBuffer(RHI::BufferHandle handle) noexcept = 0;

        [[nodiscard]] virtual RHI::BufferHandle GetVisualizationOverlayBackbufferReadbackBuffer() const noexcept = 0;

        // GRAPHICS-076 Slice B — public seam for the renderer-owned
        // `DebugViewSystem`'s `RequestedResourceName` setting. Runtime /
        // editor callers translate UI selections into canonical
        // `FrameRecipeIntrospection::Resources[i].Name` keys (per the
        // GRAPHICS-013BQ §"UI-name to FrameRecipeIntrospection mapping"
        // decision) and pass them in here; contract tests use this seam
        // to force the fallback path so the
        // `RenderGraphFrameStats::DebugViewFallbackInvocationCount`
        // diagnostic can be observed deterministically. The `Enabled`
        // field is driven by the renderer from
        // `world.DebugOverlayEnabled || world.DebugPrimitives.HasTransientDebug`
        // each frame (so a stale `Enabled = true` from this setter
        // cannot keep the pass live across frames where the world has
        // turned the overlay off); callers should treat
        // `RequestedResourceName` as the durable field they control.
        virtual void SetDebugViewRequestedResourceName(std::string name) = 0;

        [[nodiscard]] virtual std::string GetDebugViewRequestedResourceName() const = 0;
    };

    export std::unique_ptr<IRenderer> CreateRenderer();
}
