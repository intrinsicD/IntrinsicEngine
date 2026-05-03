//
// Created by alex on 22.04.26.
//

module;

export module Extrinsic.Graphics.Pass.ImGui;

import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;

namespace Extrinsic::Graphics
{
    export class ImGuiPass
    {
    public:
        explicit ImGuiPass(ImGuiOverlaySystem& overlay) : m_OverlaySystem(overlay) {}

        ImGuiPass(const ImGuiPass&)            = delete;
        ImGuiPass& operator=(const ImGuiPass&) = delete;

        void SetPipeline(RHI::PipelineHandle pipeline) noexcept;
        void Execute(RHI::ICommandContext& cmd);

    private:
        ImGuiOverlaySystem& m_OverlaySystem;
        RHI::PipelineHandle m_Pipeline{};
    };
}
