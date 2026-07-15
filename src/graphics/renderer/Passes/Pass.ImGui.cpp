module;

#include <cstdint>
#include <limits>
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

            cmd.BindIndexBuffer(drawList.IndexBuffer,
                                drawList.IndexOffsetBytes,
                                RHI::IndexType::Uint32);
            if (drawList.Commands.empty())
            {
                if (upload.DisplayWidth == 0u || upload.DisplayHeight == 0u)
                {
                    continue;
                }
                cmd.SetScissor(
                    0,
                    0,
                    upload.DisplayWidth,
                    upload.DisplayHeight);
                const ImGuiOverlayPushConstants pc =
                    m_OverlaySystem.BuildPushConstants(
                        drawList.VertexBufferBDA,
                        drawList.FirstVertex,
                        drawList.IndexCount);
                cmd.PushConstants(&pc, sizeof(pc));
                cmd.DrawIndexed(drawList.IndexCount, 1u, 0u, 0, 0u);
                ++recordedDrawCalls;
                continue;
            }

            for (const ImGuiDrawCommandUploadResult& command : drawList.Commands)
            {
                if (command.IndexCount == 0u ||
                    command.IndexOffset > drawList.IndexCount ||
                    command.IndexCount > drawList.IndexCount - command.IndexOffset ||
                    command.Scissor.IsEmpty() ||
                    command.Scissor.X < 0 || command.Scissor.Y < 0 ||
                    static_cast<std::uint64_t>(command.Scissor.X) + command.Scissor.Width >
                        upload.DisplayWidth ||
                    static_cast<std::uint64_t>(command.Scissor.Y) + command.Scissor.Height >
                        upload.DisplayHeight)
                {
                    continue;
                }

                // Renderer passes use the RHI viewport convention whose
                // Vulkan implementation has negative height. ImGui clip
                // rectangles are top-left oriented, so mirror their Y extent
                // into the attachment coordinate space used by SetScissor.
                const std::uint64_t mirroredY =
                    static_cast<std::uint64_t>(upload.DisplayHeight) -
                    (static_cast<std::uint64_t>(command.Scissor.Y) +
                     command.Scissor.Height);
                if (mirroredY >
                    static_cast<std::uint64_t>(
                        std::numeric_limits<std::int32_t>::max()))
                {
                    continue;
                }
                cmd.SetScissor(
                    command.Scissor.X,
                    static_cast<std::int32_t>(mirroredY),
                    command.Scissor.Width,
                    command.Scissor.Height);
                const std::uint32_t flags =
                    command.UsesUserTexture ? kImGuiOverlayPushFlagUserTexture : 0u;
                const ImGuiOverlayPushConstants pc =
                    m_OverlaySystem.BuildPushConstants(
                        drawList.VertexBufferBDA,
                        drawList.FirstVertex + command.VertexOffset,
                        command.IndexCount,
                        command.TextureBindlessIndex,
                        flags);
                cmd.PushConstants(&pc, sizeof(pc));
                cmd.DrawIndexed(command.IndexCount, 1u, command.IndexOffset, 0, 0u);
                ++recordedDrawCalls;
            }
        }
        m_OverlaySystem.RecordDrawCalls(recordedDrawCalls);
    }
}
