// Per-frame render diagnostics records: framegraph compile/execute stats,
// command-record and contract-integration summaries, GPU profile envelopes,
// pass command-shape counters and the transient-debug / visualization-overlay
// upload counters aggregated into `RenderGraphFrameStats`.
//
// Declarations only. This is the canonical owner every producer and reader
// shares, so runtime pacing, editor diagnostics and contract tests observe
// frame diagnostics without importing renderer execution modules.
module;

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module Extrinsic.Graphics.RenderDiagnostics;

import Extrinsic.Graphics.RenderCommandRouter;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.QueueAffinity;

export namespace Extrinsic::Graphics
{
    struct RenderGraphCompileStats
    {
        bool Succeeded = false;
        std::uint32_t AttemptCount = 0;
        std::uint32_t CacheHitCount = 0;
        std::uint32_t CacheMissCount = 0;
        bool ReusedCachedGraph = false;
        bool DebugDumpGenerated = false;
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
        std::uint64_t TransientNaiveMemoryEstimateBytes = 0;
        std::uint64_t TransientPlacedPeakMemoryEstimateBytes = 0;
        std::uint64_t TimeMicros = 0;
    };

    struct RenderGraphExecuteStats
    {
        bool Succeeded = false;
        bool DeviceOperational = false;
        bool ParallelRecordingRequested = false;
        bool ParallelRecordingAccepted = false;
        bool SerialFallbackUsed = false;
        std::uint32_t ParallelCommandContextCount = 0;
        std::uint32_t ParallelRecordedPassCount = 0;
        bool ParallelRecordUsedScheduler = false;
        std::uint32_t ParallelRecordWorkerTaskCount = 0;
        std::uint32_t ParallelRecordCallerRecordCount = 0;
        std::uint64_t TimeMicros = 0;
    };

    struct RenderGraphCommandPassStats
    {
        std::string Name{};
        FramePassId Id{};
        RenderCommandPassStatus Status = RenderCommandPassStatus::SkippedUnavailable;
    };

    struct RenderGraphCommandRecordStats
    {
        std::uint32_t Recorded = 0;
        std::uint32_t Skipped = 0;
        std::uint32_t SkippedNonOperational = 0;
        std::uint32_t SkippedUnavailable = 0;
        std::vector<RenderGraphCommandPassStats> Passes{};
    };

    struct RenderGraphContractIntegrationStats
    {
        bool Evaluated = false;
        bool ContractCompatible = false;
        bool SharedProductsCompatible = false;
        bool ArtifactMetadataValid = false;
        std::string RendererId{};
        std::string SnapshotId{};
        std::string RecipeId{};
        std::string ViewOutputRecipeId{};
        std::uint32_t SnapshotSourceRevisionCount = 0;
        std::uint32_t BindingIntentCount = 0;
        std::uint32_t RecipeSlotCount = 0;
        std::uint32_t ViewOutputCount = 0;
        std::uint32_t DeclaredArtifactCount = 0;
        std::uint32_t VisibilityProductCount = 0;
        std::uint32_t VisibilityVisibleItemCount = 0;
        std::uint32_t VisibilityRejectedItemCount = 0;
        std::uint32_t LightingProductCount = 0;
        std::uint32_t LightingResolvedLightCount = 0;
        std::uint32_t LightingIntentCount = 0;
        std::uint32_t UnsupportedProductDiagnosticCount = 0;
        std::uint32_t MissingOutputDiagnosticCount = 0;
        std::uint32_t DegradedFallbackDiagnosticCount = 0;
        std::uint32_t ArtifactPublicationFailureDiagnosticCount = 0;
        std::vector<RenderArtifactMetadata> DeclaredArtifacts{};
        std::vector<std::string> Diagnostics{};
    };

    enum class RenderGraphGpuProfileStatus : std::uint8_t
    {
        Disabled = 0,
        Unavailable,
        Unsupported,
        Recording,
        Submitted,
        NotReady,
        Resolved,
        Exhausted,
        InvalidLifecycle,
        DeviceLost,
    };

    struct RenderGraphGpuProfileQueueStats
    {
        RHI::QueueAffinity Queue{RHI::QueueAffinity::Graphics};
        RHI::GpuTimestampSource Source{
            RHI::GpuTimestampSource::Unavailable};
        std::optional<std::uint64_t> DurationNs{};
    };

    struct RenderGraphGpuProfilePassStats
    {
        std::string Name{};
        FramePassId Id{};
        RHI::QueueAffinity Queue{RHI::QueueAffinity::Graphics};
        RenderCommandPassStatus CommandStatus{
            RenderCommandPassStatus::SkippedUnavailable};
        RHI::GpuTimestampSource Source{
            RHI::GpuTimestampSource::Unavailable};
        std::optional<std::uint64_t> DurationNs{};
    };

    struct RenderGraphGpuProfileStats
    {
        RenderGraphGpuProfileStatus Status{
            RenderGraphGpuProfileStatus::Disabled};
        RHI::GpuTimestampSource Source{
            RHI::GpuTimestampSource::Unavailable};
        std::string Diagnostic{};
        bool Fresh{false};
        bool Stale{false};
        bool HasResolvedFrame{false};
        std::uint64_t ResolvedSubmittedFrameNumber{0u};
        std::uint32_t ResolvedFrameSlot{0u};
        std::uint64_t SampleAgeFrames{0u};
        std::vector<RenderGraphGpuProfileQueueStats> QueueEnvelopes{};
        std::vector<RenderGraphGpuProfilePassStats> Passes{};
    };

    // Deterministic CPU diagnostics for the `TransientDebugSurfacePass` upload
    // and recording path. `MissingPipelineSkipCount` increments when the
    // executor reaches the pass branch with the device operational but at least
    // one required pipeline lease missing (so the pass returns
    // `SkippedUnavailable`); it distinguishes "feature off" (counter stays zero
    // and the pass is absent from the stats) from "feature on but pipeline
    // missing". `UploadOverflowCount` reports transient-buffer-allocator
    // capacity exhaustion from the upload helper.
    //
    // Reset per frame through the renderer's `m_LastRenderGraphStats = {}`
    // cadence in `ExecuteFrame()`.
    struct TransientDebugUploadDiagnostics
    {
        std::uint64_t UploadOverflowCount = 0;
        std::uint64_t LineRecordsSubmitted = 0;
        std::uint64_t PointRecordsSubmitted = 0;
        std::uint64_t TriangleRecordsSubmitted = 0;
        std::uint64_t LineRecordsRecorded = 0;
        std::uint64_t PointRecordsRecorded = 0;
        std::uint64_t TriangleRecordsRecorded = 0;
        std::uint64_t MissingPipelineSkipCount = 0;
    };

    // Deterministic CPU diagnostics for the `VisualizationOverlayPass` upload
    // and recording path. Same taxonomy as the transient-debug counters above:
    // `MissingPipelineSkipCount` increments per lane whose required pipeline
    // lease is missing on an operational device, and `UploadOverflowCount`
    // reports upload-helper capacity exhaustion.
    //
    // Reset per frame through the renderer's `m_LastRenderGraphStats = {}`
    // cadence in `ExecuteFrame()`.
    struct VisualizationOverlayUploadDiagnostics
    {
        std::uint64_t UploadOverflowCount = 0;
        std::uint64_t VectorFieldRecordsSubmitted = 0;
        std::uint64_t IsolineRecordsSubmitted = 0;
        std::uint64_t VectorFieldRecordsRecorded = 0;
        // Submitted vector-field packets without a draw (unresolved buffers,
        // invalid style, missing scene table or failed record upload).
        std::uint64_t VectorFieldPacketsSkipped = 0;
        std::uint64_t VectorFieldGlyphsRecorded = 0;
        std::uint64_t IsolineRecordsRecorded = 0;
        std::uint64_t MissingPipelineSkipCount = 0;
    };

    struct RenderGraphFrameStats
    {
        RenderGraphCompileStats Compile{};
        RenderGraphExecuteStats Execute{};
        RenderGraphCommandRecordStats CommandRecords{};
        RenderGraphContractIntegrationStats Contract{};
        RenderGraphGpuProfileStats GpuProfile{};
        std::string DebugDump{};
        std::string Diagnostic{};
        std::string LifecycleDiagnostic{};
        bool FrameRecipeOverrideActive{false};
        bool FrameRecipeOverrideApplied{false};
        std::uint32_t FrameRecipeOverrideDisabledSlotCount{0u};
        std::uint32_t FrameRecipeOverrideDiagnosticCount{0u};
        std::vector<FrameRecipeOverrideDiagnostic> FrameRecipeOverrideDiagnostics{};
        // Count of frames in which the default recipe produced an accepted
        // multi-queue submit plan containing an `AsyncCompute` batch. Stays at
        // zero when the backend has no async queue, when the framegraph demotes
        // optional queues to graphics, or when the backend rejects the
        // submit-plan seam.
        std::uint32_t AsyncComputeUtilizedFrames = 0;
        // HZB build pass command-shape counters. The default renderer records
        // the deterministic per-mip fallback path until a concrete backend
        // capability plumbs the SPD-style single-pass path.
        std::uint32_t HZBBuildRecordedFrames = 0;
        std::uint32_t HZBBuildDispatchCount = 0;
        std::uint32_t HZBBuildMipCount = 0;
        std::uint32_t HZBBuildFallbackFrames = 0;
        std::uint32_t HZBBuildSinglePassFrames = 0;
        // Clustered-light build/assignment command-shape counters. These
        // increment only when the retained buffers and compute-pipeline leases
        // are available and the executor records the cluster passes.
        std::uint32_t ClusterGridBuildRecordedFrames = 0;
        std::uint32_t ClusterGridBuildDispatchCount = 0;
        std::uint32_t ClusterLightAssignmentRecordedFrames = 0;
        std::uint32_t ClusterLightAssignmentDispatchCount = 0;
        // Count of frames in which the opt-in default-recipe
        // backbuffer-to-host readback seam recorded the
        // `Present → TransferSrc → CopyImageToBuffer → Present` triplet. Stays
        // at zero unless `SetDefaultRecipeBackbufferReadbackBuffer()` was
        // configured with a valid HostVisible+TransferDst buffer and the device
        // is operational during the frame.
        std::uint32_t DefaultRecipeBackbufferReadbackCopyCount = 0;
        // Count of frames in which the opt-in transient-debug
        // backbuffer-to-host readback seam recorded the same triplet after the
        // default-recipe graph completed. Unlike the canonical default-recipe
        // counter above, this increments only when `"TransientDebugSurfacePass"`
        // recorded in the same frame, a valid transient-debug readback buffer
        // is armed, and the device is operational.
        std::uint32_t TransientDebugBackbufferReadbackCopyCount = 0;
        // Count of frames in which the opt-in visualization-overlay
        // backbuffer-to-host readback seam recorded the same triplet after the
        // default-recipe graph completed. Increments only when
        // `"VisualizationOverlayPass"` recorded in the same frame, a valid
        // visualization-overlay readback buffer is armed, and the device is
        // operational.
        std::uint32_t VisualizationOverlayBackbufferReadbackCopyCount = 0;
        // Count of frames in which the default recipe's `PickingPass` executor
        // branch recorded the picking-readback copy pair (EntityId +
        // PrimitiveId → renderer-owned `Picking.Readback` buffer at slot
        // `frame.FrameIndex % frames-in-flight`). Each operational frame with a
        // pending pick request increments by 1 (the pair records together or
        // not at all); stays at zero when no pick is pending, when the device is
        // non-operational, or when the picking pass is otherwise gated off.
        std::uint32_t PickingReadbackCopyCount = 0;
        // Count of frames in which the default-recipe `PickingPass` route
        // recorded the one-target EntityId producer for selection outline
        // without a pending click pick. This distinguishes selected/hovered
        // outline ID work from full primitive-picking work.
        std::uint32_t SelectionOutlineEntityIdPassCount = 0;
        // Count of frames in which pending click-picking recorded the primitive
        // ID refinement subpasses (face/edge/point). A successful pending-pick
        // route increments this once per frame after the three primitive
        // subpasses are recorded under `PickingPass`.
        std::uint32_t SelectionPrimitiveIdPassCount = 0;
        // Count of frames in which the default recipe's
        // `PostProcessHistogramPass` executor branch recorded the
        // histogram-readback `CopyBuffer(PostProcess.Histogram →
        // Histogram.Readback @ slot * 1024)` after the compute dispatch. Each
        // operational frame with a valid renderer-owned readback buffer
        // increments by 1; stays at zero when the device is non-operational,
        // when the readback buffer is unavailable, or when the histogram stage
        // itself is gated off. The `BeginFrame()`-side drain consumes pending
        // slots and forwards the 256-bin payload to
        // `PostProcessSystem::PublishHistogramReadback(...)`.
        std::uint32_t HistogramReadbackCopyCount = 0;
        // Count of frames in which the default recipe's canonical
        // `DebugViewPass` executor branch recorded the fullscreen
        // `BindPipeline + PushConstants + Draw(3, 1, 0, 0)` shape. Increments by
        // 1 per operational frame in which the resolved selection is enabled and
        // the pipeline lease is valid; stays at zero when the device is
        // non-operational, when `DebugViewSettings::Enabled` is false, when the
        // pipeline lease is missing, or when the resolved selection's fallback
        // path also disabled the pass
        // (`DebugViewFallbackReason::FallbackUnavailable`).
        std::uint32_t DebugViewPassExecutions = 0;
        // Count of frames in which the default recipe's
        // `DebugViewSystem::ResolveSelection(...)` reported `UsedFallback = true`
        // because the requested resource was missing / disabled / unsupported and
        // the system substituted the configured fallback resource. This is the
        // deterministic "no silent failure on invalid resource" diagnostic;
        // tests assert it increments by exactly 1 per frame in which the request
        // resolved through fallback.
        std::uint32_t DebugViewFallbackInvocationCount = 0;
        TransientDebugUploadDiagnostics TransientDebugUpload{};
        VisualizationOverlayUploadDiagnostics VisualizationOverlayUpload{};
        VisualizationPropertyBufferDiagnostics VisualizationPropertyBuffers{};
        // Temporal reconstruction diagnostics surfaced on the renderer stats
        // path. These remain CPU/null observable and do not expose
        // backend/vendor details.
        std::uint32_t ReconstructorAppliedFrames = 0;
        float HistoryDisocclusionPercent = 0.0f;
        float JitterOffsetX = 0.0f;
        float JitterOffsetY = 0.0f;
    };
}
