module;

#include <cstdint>

module Extrinsic.Graphics.Pass.Selection.Outline;

namespace Extrinsic::Graphics
{
    void SelectionOutlinePass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void SelectionOutlinePass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
    {
        Execute(cmd, camera, 0u);
    }

    void SelectionOutlinePass::Execute(RHI::ICommandContext& cmd,
                                       const RHI::CameraUBO& camera,
                                       const std::uint32_t frameIndex)
    {
        (void)camera;
        (void)frameIndex;
        if (!m_SelectionSystem.IsInitialized() || !m_Pipeline.IsValid())
        {
            return;
        }

        cmd.BindPipeline(m_Pipeline);
        cmd.Draw(3u, 1u, 0u, 0u);
    }
}
