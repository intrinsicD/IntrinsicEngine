module;

export module Extrinsic.Graphics.Pass.PostProcess.Bloom;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	export class BloomPass
	{
	public:
		explicit BloomPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		BloomPass(const BloomPass&)            = delete;
		BloomPass& operator=(const BloomPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
	};
}

