module;

export module Extrinsic.Graphics.Pass.DebugView;

import Extrinsic.Graphics.DebugViewSystem;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    export class DebugViewPass
    {
    public:
        explicit DebugViewPass(DebugViewSystem& debugView) : m_DebugViewSystem(debugView) {}

        DebugViewPass(const DebugViewPass&) = delete;
        DebugViewPass& operator=(const DebugViewPass&) = delete;

        void SetPipeline(RHI::PipelineHandle pipeline) noexcept;

        // GRAPHICS-076 Slice B — accessor mirroring `PresentPass::GetPipeline()`
        // so the renderer's fail-closed `RecordDebugViewPass` prerequisite
        // check observes the same shape on every canonical default-recipe pass
        // path. The pass body short-circuits on
        // `!m_Pipeline.IsValid()` internally; surfacing the handle lets
        // the executor return `SkippedUnavailable` deterministically
        // instead of silently recording a no-op.
        [[nodiscard]] RHI::PipelineHandle GetPipeline() const noexcept { return m_Pipeline; }

        void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

    private:
        DebugViewSystem& m_DebugViewSystem;
        RHI::PipelineHandle m_Pipeline{};
    };
}
