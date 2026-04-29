module;

export module Extrinsic.Graphics.Pass.Selection.Outline;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
	export class SelectionOutlinePass
	{
	public:
		explicit SelectionOutlinePass(SelectionSystem& selection) : m_SelectionSystem(selection) {}

		SelectionOutlinePass(const SelectionOutlinePass&)            = delete;
		SelectionOutlinePass& operator=(const SelectionOutlinePass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		SelectionSystem& m_SelectionSystem;
	};
}

