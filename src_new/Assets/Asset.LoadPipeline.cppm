module;

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <chrono>
#include <vector>

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
        enum class Stage : uint8_t
        {
            AssetIO = 0,
            AssetDecode,
            AssetUpload,
            Finalize,
        };

        struct StageStamp
        {
            Stage stage = Stage::AssetIO;
            std::chrono::steady_clock::time_point timestamp{};
        };

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
        [[nodiscard]] Core::Expected<std::vector<StageStamp>> GetStageTrail(AssetId id) const;

    private:
        struct InFlightEntry
        {
            LoadRequest request{};
            std::vector<StageStamp> stages{};
            bool decodeDone = false;
            bool uploadDone = false;
            bool finalized = false;
        };
        static void AppendStageStamp(InFlightEntry& entry, Stage stage);

        mutable std::mutex m_Mutex{};
        AssetRegistry* m_Registry = nullptr;
        AssetEventBus* m_EventBus = nullptr;
        std::unordered_map<AssetId, InFlightEntry> m_AssetsInFlight{};
    };
}
