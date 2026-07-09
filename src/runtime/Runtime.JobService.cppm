module;

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

export module Extrinsic.Runtime.JobService;

import Extrinsic.Core.StrongHandle;
import Extrinsic.Runtime.KernelEvents;

namespace Extrinsic::Runtime
{
    export struct RuntimeWorldTag;
    export using WorldHandle = Core::StrongHandle<RuntimeWorldTag>;

    export struct JobToken
    {
        std::uint32_t Index{Core::INVALID_HANDLE_INDEX};
        std::uint32_t Generation{0};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Index != Core::INVALID_HANDLE_INDEX && Generation != 0;
        }

        [[nodiscard]] friend bool operator==(JobToken, JobToken) noexcept = default;
    };

    export enum class JobTarget : std::uint8_t
    {
        CpuPool,
        GpuQueue,
    };

    export class JobCancellation
    {
    public:
        JobCancellation() = default;
        explicit JobCancellation(std::shared_ptr<std::atomic_bool> flag) noexcept;

        [[nodiscard]] bool IsCancellationRequested() const noexcept;

    private:
        std::shared_ptr<std::atomic_bool> m_Flag{};
    };

    export struct JobDesc
    {
        std::string DebugName{};
        JobTarget Target{JobTarget::CpuPool};
        WorldHandle Scope{};
        std::move_only_function<std::shared_ptr<void>(const JobCancellation&)>
            Work{};
        std::move_only_function<void(EventBus&, std::shared_ptr<void>)>
            PublishCompletion{};
    };

    export template <typename TResult>
    [[nodiscard]] JobDesc MakeJobDesc(
        std::string debugName,
        JobTarget target,
        WorldHandle scope,
        std::move_only_function<TResult(const JobCancellation&)> work,
        std::move_only_function<void(EventBus&, TResult&&)> publishCompletion)
    {
        static_assert(!std::is_void_v<TResult>,
                      "JobService completion payloads must be value types.");

        JobDesc desc{};
        desc.DebugName = std::move(debugName);
        desc.Target = target;
        desc.Scope = scope;
        desc.Work =
            [work = std::move(work)](const JobCancellation& cancellation) mutable
                -> std::shared_ptr<void>
            {
                return std::make_shared<TResult>(work(cancellation));
            };
        desc.PublishCompletion =
            [publishCompletion = std::move(publishCompletion)](
                EventBus& events,
                std::shared_ptr<void> payload) mutable
            {
                auto typed = std::static_pointer_cast<TResult>(std::move(payload));
                publishCompletion(events, std::move(*typed));
            };
        return desc;
    }

    export struct JobServiceStats
    {
        std::uint64_t Submitted{0};
        std::uint64_t Launched{0};
        std::uint64_t WorkerFinished{0};
        std::uint64_t PublishedCompletions{0};
        std::uint64_t CancelRequested{0};
        std::uint64_t DroppedCancelled{0};
        std::uint64_t DroppedWorldScope{0};
        std::uint64_t UnsupportedTarget{0};
        std::uint64_t Reaped{0};
        std::uint64_t InFlight{0};
        std::uint64_t PendingCompletions{0};
        std::uint64_t LastDrainPublished{0};
        std::uint64_t LastDrainDropped{0};
    };

    export class JobService
    {
    public:
        JobService();
        ~JobService();

        JobService(const JobService&) = delete;
        JobService& operator=(const JobService&) = delete;
        JobService(JobService&&) noexcept;
        JobService& operator=(JobService&&) noexcept;

        [[nodiscard]] JobToken Submit(JobDesc desc);
        void Cancel(JobToken token);
        [[nodiscard]] bool IsComplete(JobToken token) const;
        [[nodiscard]] std::uint32_t CancelAllForWorld(WorldHandle world);

        void DrainCompletions(EventBus& events);
        [[nodiscard]] std::uint32_t ReapCompleted();

        [[nodiscard]] JobServiceStats Stats() const;
        [[nodiscard]] std::size_t PendingCompletionCount() const;

    private:
        struct Impl;
        std::shared_ptr<Impl> m_Impl{};
    };
}
