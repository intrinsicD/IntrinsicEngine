module;

export module Extrinsic.Graphics.Pass.PostProcess.SMAA;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	export class PostProcessSMAAPass
	{
	public:
		explicit PostProcessSMAAPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		PostProcessSMAAPass(const PostProcessSMAAPass&)            = delete;
		PostProcessSMAAPass& operator=(const PostProcessSMAAPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
	};
}

