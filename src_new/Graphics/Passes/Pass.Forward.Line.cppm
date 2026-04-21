module;

export module Extrinsic.Graphics.Pass.Forward.Line;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.ForwardSystem;

namespace Extrinsic::Graphics
{
	export class LinePass
	{
	public:
		explicit LinePass(ForwardSystem& forward) : m_ForwardSystem(forward) {}

		LinePass(const LinePass&)            = delete;
		LinePass& operator=(const LinePass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		ForwardSystem& m_ForwardSystem;
	};
}
