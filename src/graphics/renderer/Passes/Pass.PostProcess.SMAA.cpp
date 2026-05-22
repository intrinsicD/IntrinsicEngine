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

    namespace
    {
        // Gate the SMAA per-stage bodies on
        // `PostProcessSettings::AntiAliasing == SMAA` (`IsStageEnabled(SMAA)`
        // enforces this through `DescribeChain()` — mutually exclusive
        // with FXAA per the same selector). When AA is `None` or `FXAA`
        // every per-stage Execute returns false and emits no bind/push/
        // draw, mirroring the bloom helper's "structurally-recorded
        // no-op" taxonomy: the per-stage umbrella helpers still return
        // `Recorded` because the pass / pipelines exist.
        [[nodiscard]] bool SMAAEnabled(const PostProcessSystem& post) noexcept
        {
            return post.IsInitialized() && post.IsStageEnabled(PostProcessStageKind::SMAA);
        }
    }

    void PostProcessSMAAPass::ExecuteEdge(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
    {
        if (!SMAAEnabled(m_PostProcessSystem) || !m_EdgePipeline.IsValid())
        {
            return;
        }
        const PostProcessSMAAEdgePushConstants edgePc =
            BuildPostProcessSMAAEdgePushConstants(m_PostProcessSystem.GetSettings(),
                                                  camera.ViewportWidth,
                                                  camera.ViewportHeight);
        cmd.BindPipeline(m_EdgePipeline);
        cmd.PushConstants(&edgePc, sizeof(edgePc));
        cmd.Draw(3u, 1u, 0u, 0u);
    }

    void PostProcessSMAAPass::ExecuteBlend(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
    {
        if (!SMAAEnabled(m_PostProcessSystem) || !m_BlendPipeline.IsValid())
        {
            return;
        }
        const PostProcessSMAABlendPushConstants blendPc =
            BuildPostProcessSMAABlendPushConstants(m_PostProcessSystem.GetSettings(),
                                                   camera.ViewportWidth,
                                                   camera.ViewportHeight);
        cmd.BindPipeline(m_BlendPipeline);
        cmd.PushConstants(&blendPc, sizeof(blendPc));
        cmd.Draw(3u, 1u, 0u, 0u);
    }

    void PostProcessSMAAPass::ExecuteResolve(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera)
    {
        if (!SMAAEnabled(m_PostProcessSystem) || !m_ResolvePipeline.IsValid())
        {
            return;
        }
        const PostProcessSMAAResolvePushConstants resolvePc =
            BuildPostProcessSMAAResolvePushConstants(m_PostProcessSystem.GetSettings(),
                                                     camera.ViewportWidth,
                                                     camera.ViewportHeight);
        cmd.BindPipeline(m_ResolvePipeline);
        cmd.PushConstants(&resolvePc, sizeof(resolvePc));
        cmd.Draw(3u, 1u, 0u, 0u);
    }
}
