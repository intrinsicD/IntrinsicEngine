module Extrinsic.Graphics.RenderGraph:Executor;

import Extrinsic.Core.Error;
import :Compiler;

namespace Extrinsic::Graphics
{
    Core::Result RenderGraphExecutor::Execute(const CompiledRenderGraph&) const
    {
        return Core::Ok();
    }
}
