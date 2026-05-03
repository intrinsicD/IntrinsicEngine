module;

export module Extrinsic.Graphics.Pass.Deferred.Lighting;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.DeferredSystem;
import Extrinsic.Graphics.GpuWorld;

namespace Extrinsic::Graphics
{
	export class DeferredLightingPass
	{
	public:
		explicit DeferredLightingPass(DeferredSystem& deferred) : m_DeferredSystem(deferred) {}

		DeferredLightingPass(const DeferredLightingPass&)            = delete;
		DeferredLightingPass& operator=(const DeferredLightingPass&) = delete;

		void SetPipeline(RHI::PipelineHandle pipeline) noexcept;
		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera, const GpuWorld& gpuWorld);

	private:
		DeferredSystem& m_DeferredSystem;
		RHI::PipelineHandle m_Pipeline{};
	};
}
