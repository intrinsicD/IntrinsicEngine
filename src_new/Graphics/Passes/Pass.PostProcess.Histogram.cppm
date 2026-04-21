module;

export module Extrinsic.Graphics.Pass.PostProcess.Histogram;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
	export class HistogramPass
	{
	public:
		explicit HistogramPass(PostProcessSystem& postProcess) : m_PostProcessSystem(postProcess) {}

		HistogramPass(const HistogramPass&)            = delete;
		HistogramPass& operator=(const HistogramPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		PostProcessSystem& m_PostProcessSystem;
	};
}

