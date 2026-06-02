//
// Created by alex on 22.04.26.
//

module;

export module Extrinsic.Graphics.Pass.ImGui;

import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.ImGuiUploadHelper;
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
        [[nodiscard]] RHI::PipelineHandle GetPipeline() const noexcept { return m_Pipeline; }
        void Execute(RHI::ICommandContext& cmd, const ImGuiUploadResult& upload);

    private:
        ImGuiOverlaySystem& m_OverlaySystem;
        RHI::PipelineHandle m_Pipeline{};
    };
}
