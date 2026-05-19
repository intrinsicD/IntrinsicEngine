module;

export module Extrinsic.Graphics.Pass.Deferred.Lighting;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.DeferredSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.ShadowSystem;

namespace Extrinsic::Graphics
{
	export class DeferredLightingPass
	{
	public:
		// GRAPHICS-072 Slice C — the lighting pass owns the deferred composition
		// body and is the consumer of the `ShadowSystem`-owned shadow atlas.
		// The system reference is required so `Execute(...)` can publish the
		// atlas's bindless slot through push constants for the fragment shader
		// to sample.
		DeferredLightingPass(DeferredSystem& deferred, ShadowSystem& shadows)
			: m_DeferredSystem(deferred), m_ShadowSystem(shadows) {}

		DeferredLightingPass(const DeferredLightingPass&)            = delete;
		DeferredLightingPass& operator=(const DeferredLightingPass&) = delete;

		void SetPipeline(RHI::PipelineHandle pipeline) noexcept;
		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera, const GpuWorld& gpuWorld);

	private:
		DeferredSystem&     m_DeferredSystem;
		ShadowSystem&       m_ShadowSystem;
		RHI::PipelineHandle m_Pipeline{};
	};
}
