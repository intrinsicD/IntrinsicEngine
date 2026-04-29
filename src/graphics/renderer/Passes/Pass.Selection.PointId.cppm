module;

export module Extrinsic.Graphics.Pass.Selection.PointId;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
    export class PointIdPass
    {
    public:
        explicit PointIdPass(SelectionSystem& selection) : m_SelectionSystem(selection) {}

        PointIdPass(const PointIdPass&)            = delete;
        PointIdPass& operator=(const PointIdPass&) = delete;

        void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

    private:
        SelectionSystem& m_SelectionSystem;
    };
}

