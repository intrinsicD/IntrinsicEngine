module;

export module Extrinsic.Graphics.Pass.PostProcess.FXAA;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	export class PostProcessFXAAPass
	{
	public:
		explicit PostProcessFXAAPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		PostProcessFXAAPass(const PostProcessFXAAPass&)            = delete;
		PostProcessFXAAPass& operator=(const PostProcessFXAAPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
	};
}

