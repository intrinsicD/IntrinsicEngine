module;

#include <cstdint>

export module Extrinsic.Graphics.Pass.Selection.Outline;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.SelectionSystem;

namespace Extrinsic::Graphics
{
	export class SelectionOutlinePass
	{
	public:
		explicit SelectionOutlinePass(SelectionSystem& selection) : m_SelectionSystem(selection) {}

		SelectionOutlinePass(const SelectionOutlinePass&)            = delete;
		SelectionOutlinePass& operator=(const SelectionOutlinePass&) = delete;

		void SetPipeline(RHI::PipelineHandle pipeline) noexcept;
		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);
		void Execute(RHI::ICommandContext& cmd,
		             const RHI::CameraUBO& camera,
		             std::uint32_t         frameIndex);

	private:
		SelectionSystem&    m_SelectionSystem;
		RHI::PipelineHandle m_Pipeline{};
	};
}
