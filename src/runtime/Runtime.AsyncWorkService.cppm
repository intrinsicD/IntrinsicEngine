module;

#include <cstdint>
#include <memory>

export module Extrinsic.Runtime.AsyncWorkService;

export import Extrinsic.Runtime.DerivedJobGraph;
export import Extrinsic.Runtime.StreamingExecutor;

namespace Extrinsic::Runtime
{
    export class AsyncWorkService
    {
    public:
        AsyncWorkService();
        ~AsyncWorkService();

        AsyncWorkService(const AsyncWorkService&) = delete;
        AsyncWorkService& operator=(const AsyncWorkService&) = delete;

        void Initialize();
        void ShutdownAndDrain();
        void Reset() noexcept;

        [[nodiscard]] StreamingExecutor* Streaming() noexcept;
        [[nodiscard]] const StreamingExecutor* Streaming() const noexcept;
        [[nodiscard]] DerivedJobRegistry* DerivedJobs() noexcept;
        [[nodiscard]] const DerivedJobRegistry* DerivedJobs() const noexcept;

        void DrainCompletions();
        [[nodiscard]] std::uint32_t ApplyMainThreadResults(
            std::uint32_t maxApplyCount);
        void PumpBackground(std::uint32_t maxLaunches);

        [[nodiscard]] DerivedJobHandle SubmitDerivedJob(DerivedJobDesc desc);
        void CancelDerivedJob(DerivedJobHandle handle);
        [[nodiscard]] DerivedJobQueueSnapshot SnapshotDerivedJobs() const;

    private:
        std::unique_ptr<StreamingExecutor> m_StreamingExecutor{};
        std::unique_ptr<DerivedJobRegistry> m_DerivedJobRegistry{};
    };
}
