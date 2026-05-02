module;

export module Extrinsic.Graphics.Pass.Selection.FaceId;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
	export class SelectionFaceIdPass
	{
	public:
		explicit SelectionFaceIdPass(SelectionSystem& selection) : m_SelectionSystem(selection) {}

		SelectionFaceIdPass(const SelectionFaceIdPass&)            = delete;
		SelectionFaceIdPass& operator=(const SelectionFaceIdPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		SelectionSystem& m_SelectionSystem;
	};
}

