module;

#include <cstdint>

export module Extrinsic.Graphics.Pass.VisualizationOverlay;

import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    // GRAPHICS-078 Slice A — deterministic CPU diagnostics for the
    // `VisualizationOverlayPass` upload + recording path. All counters
    // stay at zero in Slice A (no pipelines, scaffold executor branch
    // only). Slice B populates the vector-field counters; Slice C
    // populates the isoline counters. `MissingPipelineSkipCount`
    // increments when the executor reaches the pass branch with the
    // device operational but at least one required pipeline lease is
    // missing (so the pass returns `SkippedUnavailable`); useful for
    // distinguishing "feature off" (counter stays zero, pass not in
    // stats) from "feature on but pipeline missing" (counter
    // increments). `UploadOverflowCount` reports transient-buffer
    // allocator capacity exhaustion from the upload helper landing in
    // Slice B/C.
    //
    // Reset per-frame through the renderer's existing
    // `m_LastRenderGraphStats = {}` cadence in `ExecuteFrame()`.
    //
    // Co-located with the pass module in Slice A to keep the new
    // module surface small; Slice B can move this struct into a
    // dedicated `Extrinsic.Graphics.VisualizationOverlayUploadHelper`
    // module if/when the helper interface lands, mirroring the
    // GRAPHICS-077 Slice B placement of
    // `TransientDebugUploadDiagnostics`.
    export struct VisualizationOverlayUploadDiagnostics
    {
        std::uint64_t UploadOverflowCount = 0;
        std::uint64_t VectorFieldRecordsSubmitted = 0;
        std::uint64_t IsolineRecordsSubmitted = 0;
        std::uint64_t VectorFieldRecordsRecorded = 0;
        std::uint64_t IsolineRecordsRecorded = 0;
        std::uint64_t MissingPipelineSkipCount = 0;
    };

    // GRAPHICS-078 Slice A — scaffold shell class for the canonical
    // default-recipe `VisualizationOverlayPass`. Mirrors the
    // `TransientDebugSurfacePass` shape: default-constructible (no
    // system dependency), per-kind pipeline accessors for fail-closed
    // prerequisite checks. Slice A only ships the pipeline-handle
    // bookkeeping; `Execute*` bodies land in Slices B/C alongside the
    // pipeline-desc helpers and upload-helper wiring.
    export class VisualizationOverlayPass
    {
    public:
        VisualizationOverlayPass() = default;

        VisualizationOverlayPass(const VisualizationOverlayPass&)            = delete;
        VisualizationOverlayPass& operator=(const VisualizationOverlayPass&) = delete;

        void SetVectorFieldDepthTestedPipeline(RHI::PipelineHandle pipeline) noexcept;
        void SetVectorFieldAlwaysOnTopPipeline(RHI::PipelineHandle pipeline) noexcept;
        void SetIsolineDepthTestedPipeline(RHI::PipelineHandle pipeline) noexcept;
        void SetIsolineAlwaysOnTopPipeline(RHI::PipelineHandle pipeline) noexcept;

        [[nodiscard]] RHI::PipelineHandle GetVectorFieldDepthTestedPipeline() const noexcept
        {
            return m_VectorFieldDepthTestedPipeline;
        }
        [[nodiscard]] RHI::PipelineHandle GetVectorFieldAlwaysOnTopPipeline() const noexcept
        {
            return m_VectorFieldAlwaysOnTopPipeline;
        }
        [[nodiscard]] RHI::PipelineHandle GetIsolineDepthTestedPipeline() const noexcept
        {
            return m_IsolineDepthTestedPipeline;
        }
        [[nodiscard]] RHI::PipelineHandle GetIsolineAlwaysOnTopPipeline() const noexcept
        {
            return m_IsolineAlwaysOnTopPipeline;
        }

    private:
        RHI::PipelineHandle m_VectorFieldDepthTestedPipeline{};
        RHI::PipelineHandle m_VectorFieldAlwaysOnTopPipeline{};
        RHI::PipelineHandle m_IsolineDepthTestedPipeline{};
        RHI::PipelineHandle m_IsolineAlwaysOnTopPipeline{};
    };
}
