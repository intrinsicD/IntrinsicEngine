module;

export module Extrinsic.Graphics.Pass.Forward.Surface;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.ForwardSystem;

namespace Extrinsic::Graphics
{
	export class SurfacePass
	{
	public:
		explicit SurfacePass(ForwardSystem& forward) : m_ForwardSystem(forward) {}

		SurfacePass(const SurfacePass&)            = delete;
		SurfacePass& operator=(const SurfacePass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		ForwardSystem& m_ForwardSystem;
	};
}

