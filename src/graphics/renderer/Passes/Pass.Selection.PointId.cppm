module;

export module Extrinsic.Graphics.Pass.Selection.PointId;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
    export class SelectionPointIdPass
    {
    public:
        explicit SelectionPointIdPass(SelectionSystem& selection) : m_SelectionSystem(selection) {}

        SelectionPointIdPass(const SelectionPointIdPass&)            = delete;
        SelectionPointIdPass& operator=(const SelectionPointIdPass&) = delete;

        void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

    private:
        SelectionSystem& m_SelectionSystem;
    };
}

