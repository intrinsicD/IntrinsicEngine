//
// Created by alex on 22.04.26.
//

module;

export module Extrinsic.Graphics.Pass.DepthPrepass;

import Extrinsic.RHI.CommandContext;

namespace Extrinsic::Graphics
{
    export class DepthPrePass
    {
    public:
        DepthPrePass() = default;

        DepthPrePass(const DepthPrePass&)            = delete;
        DepthPrePass& operator=(const DepthPrePass&) = delete;

        void Execute(RHI::ICommandContext& cmd);
    };
}
