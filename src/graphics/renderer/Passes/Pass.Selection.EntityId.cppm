module;

export module Extrinsic.Graphics.Pass.Selection.EntityId;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
	export class EntityIdPass
	{
	public:
		explicit EntityIdPass(SelectionSystem& selection) : m_SelectionSystem(selection) {}

		EntityIdPass(const EntityIdPass&)            = delete;
		EntityIdPass& operator=(const EntityIdPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		SelectionSystem& m_SelectionSystem;
	};
}

