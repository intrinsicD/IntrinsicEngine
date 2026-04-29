module;

export module Extrinsic.Graphics.Pass.PostProcess.FXAA;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	export class FXAAPass
	{
	public:
		explicit FXAAPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		FXAAPass(const FXAAPass&)            = delete;
		FXAAPass& operator=(const FXAAPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
	};
}

