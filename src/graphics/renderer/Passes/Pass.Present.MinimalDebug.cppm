module;

export module Extrinsic.Graphics.Pass.Present.MinimalDebug;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    // GRAPHICS-032C — CPU-mock command body for the minimal-debug-surface
    // recipe's present pass. Records the fullscreen-triangle present form
    // (`BindPipeline` + `Draw(3, 1, 0, 0)`) and short-circuits when its
    // pipeline lease is unset, mirroring the canonical PresentPass shape.
    // Removed by GRAPHICS-081 once the canonical default recipe records the
    // present pass operationally.
    export class MinimalDebugPresentPass
    {
    public:
        MinimalDebugPresentPass() = default;

        MinimalDebugPresentPass(const MinimalDebugPresentPass&)            = delete;
        MinimalDebugPresentPass& operator=(const MinimalDebugPresentPass&) = delete;

        void SetPipeline(RHI::PipelineHandle pipeline) noexcept;

        [[nodiscard]] RHI::PipelineHandle GetPipeline() const noexcept { return m_Pipeline; }

        void Execute(RHI::ICommandContext& cmd);

    private:
        RHI::PipelineHandle m_Pipeline{};
    };
}
