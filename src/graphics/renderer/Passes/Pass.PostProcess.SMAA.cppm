module;

export module Extrinsic.Graphics.Pass.PostProcess.SMAA;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	export class SMAAPass
	{
	public:
		explicit SMAAPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		SMAAPass(const SMAAPass&)            = delete;
		SMAAPass& operator=(const SMAAPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
	};
}

