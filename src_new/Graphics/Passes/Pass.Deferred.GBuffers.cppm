module;

export module Extrinsic.Graphics.Pass.Deferred.GBuffers;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.DeferredSystem;

namespace Extrinsic::Graphics
{
	export class GBufferPass
	{
	public:
		explicit GBufferPass(DeferredSystem& deferred) : m_DeferredSystem(deferred) {}

		GBufferPass(const GBufferPass&)            = delete;
		GBufferPass& operator=(const GBufferPass&) = delete;

		void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

	private:
		DeferredSystem& m_DeferredSystem;
	};
}

