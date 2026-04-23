//
// Created by alex on 22.04.26.
//

module;

export module Extrinsic.Graphics.Pass.Present;

import Extrinsic.RHI.CommandContext;

namespace Extrinsic::Graphics
{
    export class PresentPass
    {
    public:
        PresentPass() = default;

        PresentPass(const PresentPass&)            = delete;
        PresentPass& operator=(const PresentPass&) = delete;

        void Execute(RHI::ICommandContext& cmd);
    };
}
