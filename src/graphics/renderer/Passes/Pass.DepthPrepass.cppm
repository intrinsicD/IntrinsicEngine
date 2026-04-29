//
// Created by alex on 22.04.26.
//

module;

export module Extrinsic.Graphics.Pass.DepthPrepass;

import Extrinsic.RHI.CommandContext;

namespace Extrinsic::Graphics
{
    export class DepthPrepassPass
    {
    public:
        DepthPrepassPass() = default;

        DepthPrepassPass(const DepthPrepassPass&)            = delete;
        DepthPrepassPass& operator=(const DepthPrepassPass&) = delete;

        void Execute(RHI::ICommandContext& cmd);
    };
}
