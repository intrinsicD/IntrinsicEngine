//
// Created by alex on 22.04.26.
//

module;

export module Extrinsic.Graphics.Pass.ImGui;

import Extrinsic.RHI.CommandContext;

namespace Extrinsic::Graphics
{
    export class ImGuiPass
    {
    public:
        ImGuiPass() = default;

        ImGuiPass(const ImGuiPass&)            = delete;
        ImGuiPass& operator=(const ImGuiPass&) = delete;

        void Execute(RHI::ICommandContext& cmd);
    };
}
