module;

module Extrinsic.Graphics.Pass.PostProcess.SMAA;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] float InvAxis(const float viewportAxis) noexcept
        {
            return viewportAxis > 0.0f ? 1.0f / viewportAxis : 0.0f;
        }
    }

    PostProcessSMAAEdgePushConstants BuildPostProcessSMAAEdgePushConstants(
        const PostProcessSettings& settings,
        const float viewportWidth,
        const float viewportHeight) noexcept
    {
        PostProcessSMAAEdgePushConstants pc{};
        pc.InvResolution[0] = InvAxis(viewportWidth);
        pc.InvResolution[1] = InvAxis(viewportHeight);
        // SMAA reference luma contrast threshold; future
        // `PostProcessSettings::SMAA*` fields flow through this builder so
        // the pass body and pipeline desc stay unchanged.
        pc.EdgeThreshold = 0.1f;
        pc.Pad0 = 0.0f;
        (void)settings;
        return pc;
    }

    PostProcessSMAABlendPushConstants BuildPostProcessSMAABlendPushConstants(
        const PostProcessSettings& settings,
        const float viewportWidth,
        const float viewportHeight) noexcept
    {
        PostProcessSMAABlendPushConstants pc{};
        pc.InvResolution[0] = InvAxis(viewportWidth);
        pc.InvResolution[1] = InvAxis(viewportHeight);
        // SMAA reference horizontal/vertical and diagonal search distances.
        pc.MaxSearchSteps = 16;
        pc.MaxSearchStepsDiag = 8;
        (void)settings;
        return pc;
    }

    PostProcessSMAAResolvePushConstants BuildPostProcessSMAAResolvePushConstants(
        const PostProcessSettings& settings,
        const float viewportWidth,
        const float viewportHeight) noexcept
    {
        PostProcessSMAAResolvePushConstants pc{};
        pc.InvResolution[0] = InvAxis(viewportWidth);
        pc.InvResolution[1] = InvAxis(viewportHeight);
        pc.Pad0 = 0.0f;
        pc.Pad1 = 0.0f;
        (void)settings;
        return pc;
    }

    void PostProcessSMAAPass::SetEdgePipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_EdgePipeline = pipeline;
    }

    void PostProcessSMAAPass::SetBlendPipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_BlendPipeline = pipeline;
    }

    void PostProcessSMAAPass::SetResolvePipeline(const RHI::PipelineHandle pipeline) noexcept
    {
        m_ResolvePipeline = pipeline;
    }

    void PostProcessSMAAPass::Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
    {
        // Gate the SMAA branch on `PostProcessSettings::AntiAliasing ==
        // SMAA` (`IsStageEnabled(SMAA)` enforces this through
        // `DescribeChain()` — mutually exclusive with FXAA per the same
        // selector). When AA is `None` or `FXAA` the body emits no
        // bind/push/draw, mirroring the bloom helper's
        // "structurally-recorded no-op" taxonomy: the umbrella helper
        // still returns `Recorded` because the pass / pipelines exist.
        if (!m_PostProcessSystem.IsInitialized() ||
            !m_PostProcessSystem.IsStageEnabled(PostProcessStageKind::SMAA))
        {
            return;
        }

        const PostProcessSettings& settings = m_PostProcessSystem.GetSettings();

        if (m_EdgePipeline.IsValid())
        {
            const PostProcessSMAAEdgePushConstants edgePc =
                BuildPostProcessSMAAEdgePushConstants(settings,
                                                     camera.ViewportWidth,
                                                     camera.ViewportHeight);
            cmd.BindPipeline(m_EdgePipeline);
            cmd.PushConstants(&edgePc, sizeof(edgePc));
            cmd.Draw(3u, 1u, 0u, 0u);
        }

        if (m_BlendPipeline.IsValid())
        {
            const PostProcessSMAABlendPushConstants blendPc =
                BuildPostProcessSMAABlendPushConstants(settings,
                                                      camera.ViewportWidth,
                                                      camera.ViewportHeight);
            cmd.BindPipeline(m_BlendPipeline);
            cmd.PushConstants(&blendPc, sizeof(blendPc));
            cmd.Draw(3u, 1u, 0u, 0u);
        }

        if (m_ResolvePipeline.IsValid())
        {
            const PostProcessSMAAResolvePushConstants resolvePc =
                BuildPostProcessSMAAResolvePushConstants(settings,
                                                        camera.ViewportWidth,
                                                        camera.ViewportHeight);
            cmd.BindPipeline(m_ResolvePipeline);
            cmd.PushConstants(&resolvePc, sizeof(resolvePc));
            cmd.Draw(3u, 1u, 0u, 0u);
        }
    }
}
