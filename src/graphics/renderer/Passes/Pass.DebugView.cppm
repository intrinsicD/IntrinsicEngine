module;

export module Extrinsic.Graphics.Pass.DebugView;

import Extrinsic.Graphics.DebugViewSystem;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    export class DebugViewPass
    {
    public:
        explicit DebugViewPass(DebugViewSystem& debugView) : m_DebugViewSystem(debugView) {}

        DebugViewPass(const DebugViewPass&) = delete;
        DebugViewPass& operator=(const DebugViewPass&) = delete;

        void SetPipeline(RHI::PipelineHandle pipeline) noexcept;
        void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

    private:
        DebugViewSystem& m_DebugViewSystem;
        RHI::PipelineHandle m_Pipeline{};
    };
}

