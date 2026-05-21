module;

module Extrinsic.Graphics.Pass.PostProcess.ToneMap;

namespace Extrinsic::Graphics
{
    PostProcessToneMapPushConstants BuildPostProcessToneMapPushConstants(
        const PostProcessSettings& settings) noexcept
    {
        PostProcessToneMapPushConstants pc{};
        pc.Exposure = settings.Exposure;
        pc.BloomIntensity = settings.BloomIntensity;
        // Operator + ColorGradingOn + Saturation/Contrast/ColorTemp/Tint +
        // Lift/Gamma/Gain keep their constructor defaults (ACES, grading
        // off, neutral grading), which match the shader's deterministic
        // ACES-with-grading-bypassed code path. Future settings extensions
        // (e.g. exposing the tonemap operator or grading inputs) flow
        // through this builder so the pipeline desc + pass body stay
        // unchanged.
        return pc;
    }

    void PostProcessToneMapPass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_Pipeline = pipeline;
    }

    void PostProcessToneMapPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
    {
        (void)camera;
        if (!m_PostProcessSystem.IsInitialized() || !m_Pipeline.IsValid() ||
            !m_PostProcessSystem.IsStageEnabled(PostProcessStageKind::ToneMap))
        {
            return;
        }

        const PostProcessToneMapPushConstants pc =
            BuildPostProcessToneMapPushConstants(m_PostProcessSystem.GetSettings());
        cmd.BindPipeline(m_Pipeline);
        cmd.PushConstants(&pc, sizeof(pc));
        cmd.Draw(3u, 1u, 0u, 0u);
    }
}

