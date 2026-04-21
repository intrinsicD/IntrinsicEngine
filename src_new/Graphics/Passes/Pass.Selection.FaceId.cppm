module;

export module Extrinsic.Graphics.Pass.Selection.FaceId;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
	export class FaceIdPass
	{
	public:
		explicit FaceIdPass(SelectionSystem& selection) : m_SelectionSystem(selection) {}

		FaceIdPass(const FaceIdPass&)            = delete;
		FaceIdPass& operator=(const FaceIdPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		SelectionSystem& m_SelectionSystem;
	};
}

