module;

export module Extrinsic.Graphics.Pass.PostProcess.Bloom;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	export class PostProcessBloomPass
	{
	public:
		explicit PostProcessBloomPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		PostProcessBloomPass(const PostProcessBloomPass&)            = delete;
		PostProcessBloomPass& operator=(const PostProcessBloomPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
	};
}

