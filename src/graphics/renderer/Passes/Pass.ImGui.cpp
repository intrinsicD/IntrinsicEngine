module;

module Extrinsic.Graphics.Pass.ImGui;

namespace Extrinsic::Graphics
{
    void ImGuiPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void ImGuiPass::Execute(RHI::ICommandContext& cmd)
    {
        if (!m_OverlaySystem.HasOverlayWork() || !m_Pipeline.IsValid())
        {
            return;
        }

        const ImGuiOverlayPushConstants pc = m_OverlaySystem.BuildPushConstants();
        cmd.BindPipeline(m_Pipeline);
        cmd.PushConstants(&pc, sizeof(pc));
        cmd.DrawIndexed(pc.IndexCount, 1u, 0u, 0, 0u);
    }
}

