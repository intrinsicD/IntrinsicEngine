module;

export module Extrinsic.Graphics.Pass.TransientDebug.Surface;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    // GRAPHICS-077 Slice A — scaffold-only shell class for the canonical
    // default-recipe `TransientDebugSurfacePass`. Mirrors the
    // `PresentPass` / `MinimalDebugPresentPass` shape: default-
    // constructible (no system dependency in Slice A), `SetPipeline` /
    // `GetPipeline` for fail-closed prerequisite checks, and an
    // `Execute(...)` body that no-ops when no pipeline is bound. Slice B
    // adds the triangle-lane pipelines and helper-driven recording;
    // Slice C extends to the line + point lanes.
    export class TransientDebugSurfacePass
    {
    public:
        TransientDebugSurfacePass() = default;

        TransientDebugSurfacePass(const TransientDebugSurfacePass&)            = delete;
        TransientDebugSurfacePass& operator=(const TransientDebugSurfacePass&) = delete;

        void SetPipeline(RHI::PipelineHandle pipeline) noexcept;

        [[nodiscard]] RHI::PipelineHandle GetPipeline() const noexcept { return m_Pipeline; }

        void Execute(RHI::ICommandContext& cmd);

    private:
        RHI::PipelineHandle m_Pipeline{};
    };
}
