module;

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

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
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.RenderGraph;
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
        // GRAPHICS-032A — minimal-debug-surface recipe diagnostics. The two
        // execution counters increment in GRAPHICS-032B/C once the pass bodies
        // land; until then they remain zero even when the recipe is selected.
        // `MinimalRecipeMissingPrerequisiteCount` reflects per-frame counts of
        // material/pipeline/surface-bucket residency gaps detected at recipe
        // build time. All three reset per-frame at `BeginFrame`/`ExecuteFrame`
        // through the existing `m_LastRenderGraphStats = {}` cadence.
        std::uint32_t MinimalSurfacePassExecutions = 0;
        std::uint32_t MinimalPresentPassExecutions = 0;
        std::uint32_t MinimalRecipeMissingPrerequisiteCount = 0;
        // GRAPHICS-033D — count of frames in which the opt-in MinimalDebug
        // backbuffer-to-host readback seam recorded the
        // `Present → TransferSrc → CopyImageToBuffer → Present` triplet. Stays
        // at zero unless `SetMinimalDebugBackbufferReadbackBuffer()` was
        // configured with a valid HostVisible+TransferDst buffer and the
        // recipe + device are operational during the frame. The smoke fixture
        // asserts this is 1 after a single operational frame so the readback
        // wiring cannot silently regress to a no-op.
        std::uint32_t MinimalDebugBackbufferReadbackCopyCount = 0;
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

        virtual void SubmitRuntimeSnapshots(const RuntimeRenderSnapshotBatch& snapshots) = 0;

        [[nodiscard]] virtual RenderWorld ExtractRenderWorld(
            const RenderFrameInput& input) = 0;

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

        // GRAPHICS-075 (Slice D.1) — accessors for the three SMAA pipelines
        // (vertex `post_fullscreen.vert.spv` paired with fragments
        // `post_smaa_edge.frag.spv` / `post_smaa_blend.frag.spv` /
        // `post_smaa_resolve.frag.spv`). All three pipelines target the
        // current `PostProcess.AATemp` recipe attachment (allocated with
        // `FrameRecipeSizing::BackbufferFormat`), so the renderer
        // creates them with the same `m_BackbufferFormat` it feeds the
        // FXAA pipeline. Per-stage push constants:
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
        // would alias `Exposure` / `Gamma` / etc. onto `InvResolution.x` /
        // `InvResolution.y` / threshold scalars and produce
        // visually-meaningless SMAA output. Handles are invalid until an
        // operational device path publishes the leases; the descriptors
        // remain deterministic so contract tests can assert byte-identical
        // rebuild behavior. Slice D.2 retargets edge to `RG8_UNORM` and
        // blend to `RGBA8_UNORM` once the recipe-side
        // `PostProcess.AATemp.{Edges,Weights}` split lands; D.1
        // deliberately matches the current single-attachment AA pass so
        // the umbrella render pass / pipeline stay format-compatible on
        // Vulkan. Retained `AreaTex` / `SearchTex` LUT textures (sampled
        // by the blend pipeline) + exposure-adaptation history buffer
        // land in Slice D.2 alongside the recipe-side AATemp split.
        [[nodiscard]] virtual RHI::PipelineHandle GetPostProcessSMAAEdgePipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetPostProcessSMAAEdgePipelineDesc() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineHandle GetPostProcessSMAABlendPipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetPostProcessSMAABlendPipelineDesc() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineHandle GetPostProcessSMAAResolvePipeline() const noexcept = 0;
        [[nodiscard]] virtual RHI::PipelineDesc GetPostProcessSMAAResolvePipelineDesc() const noexcept = 0;

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

        // GRAPHICS-072 (Slice A) — test seam for the default recipe's runtime
        // lighting path. `DeriveDefaultFrameRecipeFeatures` derives a default
        // of `Forward` so the legacy contract tests stay green; the renderer
        // overrides that derivation with this stored value when set to
        // `Deferred` or `Hybrid`, allowing contract tests to drive the
        // deferred surface/composition executor branches without re-deriving
        // features at the call site. Default is `Forward`.
        virtual void SetLightingPath(FrameRecipeLightingPath path) noexcept = 0;
        [[nodiscard]] virtual FrameRecipeLightingPath GetLightingPath() const noexcept = 0;

        // GRAPHICS-032A — opt-in selector for the minimal-debug-surface frame
        // recipe. Default is `FrameRecipeKind::Default`, preserving the
        // existing `BuildDefaultFrameRecipe` path. Runtime callers translate
        // `Core::Config::RenderConfig::FrameRecipe` into this setter; scaffold
        // retired by GRAPHICS-081.
        virtual void SetFrameRecipe(Core::Config::FrameRecipeKind kind) noexcept = 0;

        [[nodiscard]] virtual Core::Config::FrameRecipeKind GetFrameRecipe() const noexcept = 0;

        // GRAPHICS-033D — opt-in backbuffer-to-host readback wiring for the
        // MinimalDebug visible-triangle smoke (and the canonical
        // GRAPHICS-076/081 default-recipe equivalent once those land). The
        // caller owns the buffer's lifetime via `BufferManager::BufferLease`
        // and passes the raw handle; the renderer issues
        // `vkCmdCopyImageToBuffer` after the present pass and before the
        // backbuffer transitions to PRESENT_SRC on hosts where the device is
        // operational and the MinimalDebug recipe is selected. Calling with
        // `RHI::BufferHandle{}` disables the readback path (the default
        // post-Initialize state). Scaffold retired by GRAPHICS-081.
        virtual void SetMinimalDebugBackbufferReadbackBuffer(RHI::BufferHandle handle) noexcept = 0;

        [[nodiscard]] virtual RHI::BufferHandle GetMinimalDebugBackbufferReadbackBuffer() const noexcept = 0;
    };

    export std::unique_ptr<IRenderer> CreateRenderer();
}
