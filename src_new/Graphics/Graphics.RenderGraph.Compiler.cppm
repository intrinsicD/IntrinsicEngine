module;

#include <cstdint>

export module Extrinsic.Graphics.RenderGraph:Compiler;

import Extrinsic.Core.Error;

namespace Extrinsic::Graphics
{
    export struct CompiledRenderGraph
    {
        std::uint32_t PassCount = 0;
        std::uint32_t ResourceCount = 0;
    };

    export class RenderGraphCompiler final
    {
    public:
        [[nodiscard]] static Core::Expected<CompiledRenderGraph> Compile(
            std::uint32_t passCount,
            std::uint32_t resourceCount);
    };
}
