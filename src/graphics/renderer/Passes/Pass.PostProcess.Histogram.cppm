module;

export module Extrinsic.Graphics.Pass.PostProcess.Histogram;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	export class PostProcessHistogramPass
	{
	public:
		explicit PostProcessHistogramPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		PostProcessHistogramPass(const PostProcessHistogramPass&)            = delete;
		PostProcessHistogramPass& operator=(const PostProcessHistogramPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
	};
}

