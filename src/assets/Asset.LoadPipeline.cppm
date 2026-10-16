// Coordinates CPU asset completion, cancellation, and readiness events.
module;

#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <functional>

export module Extrinsic.Asset.LoadPipeline;

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.EventBus;
import Extrinsic.Core.Error;

export namespace Extrinsic::Assets
{
    struct LoadRequest
    {
        AssetId id{};
        bool publishQueuedEvent = false;
        AssetEvent queuedEvent = AssetEvent::Ready;
    };

    struct AssetLoadPipelineTestHooks
    {
        // Completion-thread pause seams run under the pipeline mutex. Callbacks must
        // not enter the pipeline or AssetService (service transactions lock first).
        std::function<void(AssetId)> AfterCpuDecodeClaim{};
        std::function<void(AssetId)> BeforeCpuReadyEvent{};
    };

    class AssetLoadPipeline
    {
    public:
        explicit AssetLoadPipeline(AssetLoadPipelineTestHooks testHooks = {});
        ~AssetLoadPipeline();
        AssetLoadPipeline(const AssetLoadPipeline&) = delete;
        AssetLoadPipeline& operator=(const AssetLoadPipeline&) = delete;

        void BindRegistry(AssetRegistry* registry);
        void BindEventBus(AssetEventBus* eventBus);

        Core::Result EnqueueIO(LoadRequest req);
        Core::Result OnCpuDecoded(AssetId id);
        // Advances or joins CPU completion; success requires Ready after publication.
        Core::Result CompleteCpuLoad(AssetId id);
        void Cancel(AssetId id);

        [[nodiscard]] std::size_t InFlightCount() const;
        [[nodiscard]] bool IsInFlight(AssetId id) const;

    private:
        struct AsyncState;

        Core::Result OnCpuDecodedUnlocked(AssetId id);
        void Shutdown();

        AssetLoadPipelineTestHooks m_TestHooks{};
        mutable std::mutex m_Mutex{};
        bool m_Accepting{true};
        std::shared_ptr<AsyncState> m_AsyncState{};
        AssetRegistry* m_Registry = nullptr;
        AssetEventBus* m_EventBus = nullptr;
        std::unordered_set<AssetId, AssetIdHash> m_AssetsInFlight{};
    };
}
