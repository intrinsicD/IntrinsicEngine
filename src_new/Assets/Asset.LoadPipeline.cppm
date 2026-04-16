module;

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

export module Extrinsic.Asset.LoadPipeline;

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.EventBus;
import Extrinsic.Core.Error;

export namespace Extrinsic::Assets
{
    struct LoadRequest
    {
        AssetId id{};
        uint32_t typeId{};
        std::string path{};        // owning storage - survives async dispatch
        bool needsGpuUpload = false;
    };

    class AssetLoadPipeline
    {
    public:
        AssetLoadPipeline() = default;
        AssetLoadPipeline(const AssetLoadPipeline&) = delete;
        AssetLoadPipeline& operator=(const AssetLoadPipeline&) = delete;

        // Binds the registry used to gate state transitions. Must be called
        // before any Enqueue/OnCpuDecoded/OnGpuUploaded invocation.
        void BindRegistry(AssetRegistry* registry);

        // Optional. If bound, the pipeline will publish Ready/Failed events
        // at the appropriate state transitions.
        void BindEventBus(AssetEventBus* eventBus);

        // Transition Unloaded -> QueuedIO and dispatch the decode step.
        // If the task scheduler is initialized, decode runs asynchronously;
        // otherwise it runs inline on the caller's thread.
        Core::Result EnqueueIO(LoadRequest req);

        // Explicit hooks for custom pipelines that want to drive the state
        // machine manually (e.g. unit tests, custom schedulers).
        Core::Result OnCpuDecoded(AssetId id);
        Core::Result OnGpuUploaded(AssetId id);

        // Mark the asset as failed from any pre-Ready state. Publishes a
        // Failed event on the bound event bus (if any). Idempotent - a no-op
        // if the asset is already Failed.
        Core::Result MarkFailed(AssetId id);

        // Drop any in-flight bookkeeping for an asset without touching the
        // registry. Call from AssetService::Destroy so dangling LoadRequests
        // do not accumulate when an asset is destroyed mid-upload.
        void Cancel(AssetId id);

        [[nodiscard]] std::size_t InFlightCount() const;
        [[nodiscard]] bool IsInFlight(AssetId id) const;

    private:
        mutable std::mutex m_Mutex{};
        AssetRegistry* m_Registry = nullptr;
        AssetEventBus* m_EventBus = nullptr;
        std::unordered_map<AssetId, LoadRequest> m_AssetsInFlight{};
    };
}
