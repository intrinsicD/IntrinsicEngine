module;

export module Extrinsic.Graphics.Pass.PostProcess.ToneMap;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	export class ToneMapPass
	{
	public:
		explicit ToneMapPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		ToneMapPass(const ToneMapPass&)            = delete;
		ToneMapPass& operator=(const ToneMapPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
	};
}

