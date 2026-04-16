module;

#include <cstdint>
#include <mutex>
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
        const char* path{};
        bool needsGpuUpload = false;
    };

    class AssetLoadPipeline
    {
    public:
        void BindRegistry(AssetRegistry* registry);
        void BindEventBus(AssetEventBus* eventBus);

        Core::Result EnqueueIO(const LoadRequest& req);
        Core::Result OnCpuDecoded(AssetId id);
        Core::Result OnGpuUploaded(AssetId id);

    private:
        mutable std::mutex m_Mutex{};
        AssetRegistry* m_Registry = nullptr;
        AssetEventBus* m_EventBus = nullptr;
        std::unordered_map<AssetId, LoadRequest> m_AssetsInFlight{};
    };
}
