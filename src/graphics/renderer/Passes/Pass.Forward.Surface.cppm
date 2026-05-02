module;

export module Extrinsic.Graphics.Pass.Forward.Surface;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.ForwardSystem;

namespace Extrinsic::Graphics
{
	export class ForwardSurfacePass
	{
	public:
		explicit ForwardSurfacePass(ForwardSystem& forward) : m_ForwardSystem(forward) {}

		ForwardSurfacePass(const ForwardSurfacePass&)            = delete;
		ForwardSurfacePass& operator=(const ForwardSurfacePass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		ForwardSystem& m_ForwardSystem;
	};
}

