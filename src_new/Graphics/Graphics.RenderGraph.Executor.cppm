module;

export module Extrinsic.Graphics.RenderGraph:Executor;

import Extrinsic.Core.Error;
import :Compiler;

namespace Extrinsic::Graphics
{
    export class RenderGraphExecutor final
    {
    public:
        [[nodiscard]] Core::Result Execute(const CompiledRenderGraph& graph) const;
    };
}
