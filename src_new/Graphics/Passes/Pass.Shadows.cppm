module;

export module Extrinsic.Graphics.Pass.Shadows;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.ShadowSystem;

namespace Extrinsic::Graphics
{
	export class ShadowPass
	{
	public:
		explicit ShadowPass(ShadowSystem& shadows) : m_ShadowSystem(shadows) {}

		ShadowPass(const ShadowPass&)            = delete;
		ShadowPass& operator=(const ShadowPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		ShadowSystem& m_ShadowSystem;
	};
}

