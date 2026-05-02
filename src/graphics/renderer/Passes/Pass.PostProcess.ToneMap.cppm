module;

export module Extrinsic.Graphics.Pass.PostProcess.ToneMap;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	export class PostProcessToneMapPass
	{
	public:
		explicit PostProcessToneMapPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		PostProcessToneMapPass(const PostProcessToneMapPass&)            = delete;
		PostProcessToneMapPass& operator=(const PostProcessToneMapPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
	};
}

