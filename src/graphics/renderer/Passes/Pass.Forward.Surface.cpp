module;

#include <cstdint>

module Extrinsic.Graphics.Pass.Forward.Surface;

namespace Extrinsic::Graphics
{
	void ForwardSurfacePass::SetPipeline(const RHI::PipelineHandle pipeline) noexcept
	{
		m_Pipeline = pipeline;
	}

	void ForwardSurfacePass::Execute(RHI::ICommandContext& cmd,
									 const RHI::CameraUBO& camera,
									 const GpuWorld&       gpuWorld,
									 const CullingSystem&  culling,
									 const std::uint32_t   frameIndex)
	{
		(void)camera;
		if (!m_ForwardSystem.IsInitialized() || !m_Pipeline.IsValid())
		{
			return;
		}

		RecordOpaqueSurfaceBucket(
		    cmd, m_Pipeline, gpuWorld,
		    culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque),
		    frameIndex);
	}
}

