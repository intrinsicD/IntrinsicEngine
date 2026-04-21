module;

export module Extrinsic.Graphics.Pass.Forward.Point;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.ForwardSystem;

namespace Extrinsic::Graphics
{
	export class PointPass
	{
	public:
		explicit PointPass(ForwardSystem& forward) : m_ForwardSystem(forward) {}

		PointPass(const PointPass&)            = delete;
		PointPass& operator=(const PointPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		ForwardSystem& m_ForwardSystem;
	};
}

