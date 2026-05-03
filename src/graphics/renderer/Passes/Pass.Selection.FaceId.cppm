module;

#include <cstdint>

export module Extrinsic.Graphics.Pass.Selection.FaceId;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
	export class FaceIdPass
	{
	public:
		explicit FaceIdPass(SelectionSystem& selection) : m_SelectionSystem(selection) {}

		FaceIdPass(const FaceIdPass&)            = delete;
		FaceIdPass& operator=(const FaceIdPass&) = delete;

		void SetPipeline(RHI::PipelineHandle pipeline) noexcept;
		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);
		void Execute(RHI::ICommandContext& cmd,
		             const RHI::CameraUBO& camera,
		             const GpuWorld&       gpuWorld,
		             const CullingSystem&  culling,
		             std::uint32_t         frameIndex);

	private:
		SelectionSystem&    m_SelectionSystem;
		RHI::PipelineHandle m_Pipeline{};
	};

	export using SelectionFaceIdPass = FaceIdPass;
}
