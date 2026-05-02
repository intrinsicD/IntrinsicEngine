module;

export module Extrinsic.Graphics.Pass.Selection.EntityId;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
	export class SelectionEntityIdPass
	{
	public:
		explicit SelectionEntityIdPass(SelectionSystem& selection) : m_SelectionSystem(selection) {}

		SelectionEntityIdPass(const SelectionEntityIdPass&)            = delete;
		SelectionEntityIdPass& operator=(const SelectionEntityIdPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		SelectionSystem& m_SelectionSystem;
	};
}

