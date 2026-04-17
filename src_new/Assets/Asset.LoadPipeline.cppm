module;

#include <cstdint>
#include <mutex>
#include <string>
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
        std::string path{};
        bool needsGpuUpload = false;
    };

    class AssetLoadPipeline
    {
    public:
        AssetLoadPipeline() = default;
        AssetLoadPipeline(const AssetLoadPipeline&) = delete;
        AssetLoadPipeline& operator=(const AssetLoadPipeline&) = delete;

        void BindRegistry(AssetRegistry* registry);
        void BindEventBus(AssetEventBus* eventBus);

        Core::Result EnqueueIO(LoadRequest req);
        Core::Result OnCpuDecoded(AssetId id);
        Core::Result OnGpuUploaded(AssetId id);

        Core::Result MarkFailed(AssetId id);
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
