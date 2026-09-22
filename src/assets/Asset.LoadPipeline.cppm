// Coordinates asset state transitions and readiness events across CPU decode
// and optional externally completed upload stages.
module;

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <chrono>
#include <vector>
#include <deque>
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
        uint32_t typeId{};
        std::string path{};
        bool needsGpuUpload = false;
        bool publishQueuedEvent = false;
        AssetEvent queuedEvent = AssetEvent::Ready;
    };

    struct AssetLoadPipelineTestHooks
    {
        // Completion-thread pause seams; callbacks must not re-enter the pipeline.
        std::function<void(AssetId)> AfterCpuDecodeClaim{};
        std::function<void(AssetId)> BeforeCpuReadyEvent{};
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

        explicit AssetLoadPipeline(AssetLoadPipelineTestHooks testHooks = {});
        ~AssetLoadPipeline();
        AssetLoadPipeline(const AssetLoadPipeline&) = delete;
        AssetLoadPipeline& operator=(const AssetLoadPipeline&) = delete;

        void BindRegistry(AssetRegistry* registry);
        void BindEventBus(AssetEventBus* eventBus);

        Core::Result EnqueueIO(LoadRequest req);
        Core::Result OnCpuDecoded(AssetId id);
        // Advances or joins CPU completion; success requires Ready after publication.
        // A GPU request can advance to QueuedGPU and still report InvalidState.
        Core::Result CompleteCpuLoad(AssetId id);
        Core::Result OnGpuUploaded(AssetId id);
        Core::Result ArmGpuFence(AssetId id, uint64_t fenceValue);
        uint32_t CompleteGpuFence(uint64_t fenceValue);

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
        struct AsyncState;

        static void AppendStageStamp(InFlightEntry& entry, Stage stage);
        void ArchiveTrailUnlocked(AssetId id);
        Core::Result OnCpuDecodedUnlocked(AssetId id);
        void Shutdown();

        AssetLoadPipelineTestHooks m_TestHooks{};
        mutable std::mutex m_Mutex{};
        static constexpr std::size_t kCompletedTrailCapacity = 256;
        bool m_Accepting{true};
        std::shared_ptr<AsyncState> m_AsyncState{};
        AssetRegistry* m_Registry = nullptr;
        AssetEventBus* m_EventBus = nullptr;
        std::unordered_map<AssetId, InFlightEntry, AssetIdHash> m_AssetsInFlight{};
        std::unordered_map<AssetId, std::vector<StageStamp>, AssetIdHash> m_CompletedStageTrails{};
        std::deque<AssetId> m_CompletedTrailOrder{};
        std::unordered_map<uint64_t, std::vector<AssetId>> m_FenceWaiters{};
    };
}
