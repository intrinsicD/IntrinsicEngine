module Extrinsic.Graphics.RenderGraph:Compiler;

import Extrinsic.Core.Error;

namespace Extrinsic::Graphics
{
    Core::Expected<CompiledRenderGraph> RenderGraphCompiler::Compile(
        const std::uint32_t passCount,
        const std::uint32_t resourceCount)
    {
        return CompiledRenderGraph{
            .PassCount = passCount,
            .ResourceCount = resourceCount,
        };
    }
}
