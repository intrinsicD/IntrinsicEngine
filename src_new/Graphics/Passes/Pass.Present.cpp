module;

module Extrinsic.Graphics.Pass.Present;

import Extrinsic.RHI.CommandContext;

namespace Extrinsic::Graphics
{
    class PresentPass;

    void PresentPass::Execute(RHI::ICommandContext& cmd)
    {
        (void)cmd;
    }
}
