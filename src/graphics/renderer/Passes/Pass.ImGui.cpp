module;

#include <cstdint>
#include <vector>

module Extrinsic.Graphics.Pass.ImGui;

import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    void ImGuiPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void ImGuiPass::Execute(RHI::ICommandContext& cmd, const ImGuiUploadResult& upload)
    {
        if (!m_OverlaySystem.HasOverlayWork() || !m_Pipeline.IsValid() || !upload.Uploaded)
        {
            return;
        }

        cmd.BindPipeline(m_Pipeline);
        std::uint32_t recordedDrawCalls = 0u;
        for (const ImGuiDrawListUploadResult& drawList : upload.DrawLists)
        {
            if (!drawList.Uploaded || !drawList.IndexBuffer.IsValid() ||
                drawList.IndexCount == 0u)
            {
                continue;
            }

            const ImGuiOverlayPushConstants pc =
                m_OverlaySystem.BuildPushConstants(
                    drawList.VertexBufferBDA,
                    drawList.FirstVertex,
                    drawList.IndexCount);
            cmd.BindIndexBuffer(drawList.IndexBuffer,
                                drawList.IndexOffsetBytes,
                                RHI::IndexType::Uint32);
            cmd.PushConstants(&pc, sizeof(pc));
            cmd.DrawIndexed(drawList.IndexCount, 1u, 0u, 0, 0u);
            ++recordedDrawCalls;
        }
        m_OverlaySystem.RecordDrawCalls(recordedDrawCalls);
    }
}
