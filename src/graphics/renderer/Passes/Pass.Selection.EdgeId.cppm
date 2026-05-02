module;

export module Extrinsic.Graphics.Pass.Selection.EdgeId;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
	export class SelectionEdgeIdPass
	{
	public:
		explicit SelectionEdgeIdPass(SelectionSystem& selection) : m_SelectionSystem(selection) {}

		SelectionEdgeIdPass(const SelectionEdgeIdPass&)            = delete;
		SelectionEdgeIdPass& operator=(const SelectionEdgeIdPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		SelectionSystem& m_SelectionSystem;
	};
}

