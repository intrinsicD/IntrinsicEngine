module;

module Graphics:Passes.Graph.Impl;

import :Passes.Graph;
import :RenderPipeline;

namespace Graphics::Passes
{

    // =========================================================================
    // AddPasses — No-op.
    // =========================================================================
    //
    // Node rendering is now handled by PointPass (BDA retained-mode).
    // Edge rendering is handled by LinePass (BDA retained-mode).
    // This shell is retained for pipeline compatibility; deletion in TODO §1.5.

    void GraphRenderPass::AddPasses(RenderPassContext& /*ctx*/)
    {
        // Intentionally empty — all graph rendering is handled by
        // LinePass (edges) and PointPass (nodes) via BDA retained-mode.
    }

} // namespace Graphics::Passes
