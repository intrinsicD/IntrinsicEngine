module;

export module Extrinsic.Graphics.Pass.Selection.EdgeId;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
	export class EdgeIdPass
	{
	public:
		explicit EdgeIdPass(SelectionSystem& selection) : m_SelectionSystem(selection) {}

		EdgeIdPass(const EdgeIdPass&)            = delete;
		EdgeIdPass& operator=(const EdgeIdPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		SelectionSystem& m_SelectionSystem;
	};
}

