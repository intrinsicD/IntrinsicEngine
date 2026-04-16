module;

module Extrinsic.Asset.LoadPipeline;

namespace Extrinsic::Assets
{
    Core::Result AssetLoadPipeline::EnqueueIO(const LoadRequest& req)
    {
        // Streaming graph stage
        // 1) CAS state Unloaded -> QueuedIO
        // 2) Dispatch IO/decode task on Scheduler
        // 3) Publish CPU payload slot
        // 4) If GPU upload required: LoadedCPU -> QueuedGPU else -> Ready
        return Core::Ok();
    }

    Core::Result AssetLoadPipeline::OnCpuDecoded(AssetId id)
    {
    }

    Core::Result AssetLoadPipeline::OnGpuUploaded(AssetId id)
    {
        // Frame graph completion callback
        // CAS QueuedGPU -> Ready
        // enqueue event bus notification
        return Core::Ok();

    }
}
