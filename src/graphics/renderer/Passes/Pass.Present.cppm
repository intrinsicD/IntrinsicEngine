//
// Created by alex on 22.04.26.
//

module;

export module Extrinsic.Graphics.Pass.Present;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    export class PresentPass
    {
    public:
        PresentPass() = default;

        PresentPass(const PresentPass&)            = delete;
        PresentPass& operator=(const PresentPass&) = delete;

        void SetPipeline(RHI::PipelineHandle pipeline) noexcept;

        [[nodiscard]] RHI::PipelineHandle GetPipeline() const noexcept { return m_Pipeline; }

        void Execute(RHI::ICommandContext& cmd);

    private:
        RHI::PipelineHandle m_Pipeline{};
    };
}
