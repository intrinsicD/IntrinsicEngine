module;

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

export module Extrinsic.Runtime.JobService;

import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    export struct JobTokenTag;
    export using JobToken = Core::StrongHandle<JobTokenTag>;

    export struct GpuQueueParticipantTag;
    export using GpuQueueParticipantHandle =
        Core::StrongHandle<GpuQueueParticipantTag>;

    export using JobResultTypeKey = std::size_t;

    export template <typename TResult>
    [[nodiscard]] consteval std::string_view JobResultTypeNameOf() noexcept
    {
#if defined(__clang__) || defined(__GNUC__)
        return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
        return __FUNCSIG__;
#else
        return "JobResultTypeNameOf<unknown>";
#endif
    }

    export enum class JobTarget : std::uint8_t
    {
        CpuPool,
        GpuQueue,
    };

    export enum class JobState : std::uint8_t
    {
        Invalid,
        Queued,
        Running,
        AwaitingGate,
        Published,
        Dropped,
        Cancelled,
        Rejected,
    };

    export class JobCancellation
    {
    public:
        JobCancellation() = default;

        [[nodiscard]] bool IsCancelled() const noexcept;

    private:
        friend class JobService;
        explicit JobCancellation(std::shared_ptr<std::atomic<bool>> flag)
            : m_Flag(std::move(flag))
        {
        }

        std::shared_ptr<std::atomic<bool>> m_Flag{};
    };

    export class JobResultEnvelope
    {
    public:
        JobResultEnvelope() = default;

        template <typename TResult>
        [[nodiscard]] static JobResultEnvelope Make(TResult payload)
        {
            return JobResultEnvelope(Core::TypeToken<TResult>(),
                                     std::make_shared<const TResult>(
                                         std::move(payload)),
                                     JobResultTypeNameOf<TResult>());
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return static_cast<bool>(m_Payload);
        }
        [[nodiscard]] std::string_view TypeName() const noexcept
        {
            return m_TypeName;
        }

        template <typename TResult>
        [[nodiscard]] const TResult* TryGet() const noexcept
        {
            if (m_Type != Core::TypeToken<TResult>())
                return nullptr;
            return static_cast<const TResult*>(m_Payload.get());
        }

    private:
        friend class JobService;

        JobResultEnvelope(JobResultTypeKey type,
                          std::shared_ptr<const void> payload,
                          std::string_view typeName)
            : m_Type(type), m_Payload(std::move(payload)), m_TypeName(typeName)
        {
        }

        JobResultTypeKey           m_Type{0};
        std::shared_ptr<const void> m_Payload{};
        std::string_view           m_TypeName{};
    };

    export struct JobDesc
    {
        std::string DebugName{};
        JobTarget Target{JobTarget::CpuPool};
        WorldHandle Scope{DefaultWorldHandle};
        std::move_only_function<JobResultEnvelope(const JobCancellation&)>
            Work{};
        std::move_only_function<bool(KernelEventBus&, const JobResultEnvelope&)>
            PublishCompletion{};
    };

    export struct GpuQueueParticipantDesc
    {
        std::string DebugName{};
        WorldHandle Scope{DefaultWorldHandle};
        std::function<void(RHI::ICommandContext&)> RecordFrameCommands{};
        std::function<void()> DrainCompletedTransfers{};
        std::function<bool()> HasInFlightWork{};
        std::function<void()> ShutdownAfterDeviceIdle{};
    };

    export template <typename TResult, typename TWork, typename TEventFactory>
    [[nodiscard]] JobDesc MakeCpuJobDesc(std::string debugName,
                                         WorldHandle scope,
                                         TWork work,
                                         TEventFactory makeEvent)
    {
        JobDesc desc{};
        desc.DebugName = std::move(debugName);
        desc.Target = JobTarget::CpuPool;
        desc.Scope = scope.IsValid() ? scope : DefaultWorldHandle;
        desc.Work = [work = std::move(work)](const JobCancellation& cancellation) mutable
            -> JobResultEnvelope
        {
            return JobResultEnvelope::Make<TResult>(work(cancellation));
        };
        desc.PublishCompletion =
            [makeEvent = std::move(makeEvent)](
                KernelEventBus& events,
                const JobResultEnvelope& result) mutable -> bool
        {
            const TResult* payload = result.TryGet<TResult>();
            if (!payload)
                return false;
            events.Publish(makeEvent(*payload));
            return true;
        };
        return desc;
    }

    export struct JobServiceStats
    {
        std::uint64_t SubmittedJobs{0};
        std::uint64_t RejectedJobs{0};
        std::uint64_t InFlightJobs{0};
        std::uint64_t QueuedJobs{0};
        std::uint64_t RunningJobs{0};
        std::uint64_t AwaitingGateJobs{0};
        std::uint64_t CompletedJobs{0};
        std::uint64_t CancelledJobs{0};
        std::uint64_t PublishedCompletions{0};
        std::uint64_t DroppedCompletions{0};
        std::uint64_t CompletionDrains{0};
        std::uint64_t LastDrainFinished{0};
        std::uint64_t LastDrainPublished{0};
        std::uint64_t LastDrainDropped{0};
        std::uint64_t ReapedJobs{0};
    };

    export class JobService
    {
    public:
        JobService();
        ~JobService();

        JobService(const JobService&) = delete;
        JobService& operator=(const JobService&) = delete;

        [[nodiscard]] JobToken Submit(JobDesc desc);
        [[nodiscard]] bool Cancel(JobToken token);
        [[nodiscard]] std::uint64_t CancelAllForWorld(WorldHandle world);
        [[nodiscard]] std::uint64_t CancelAll();

        [[nodiscard]] bool IsComplete(JobToken token) const;
        [[nodiscard]] JobState GetState(JobToken token) const;

        [[nodiscard]] std::uint64_t DrainCompletions(KernelEventBus& events);
        [[nodiscard]] std::uint64_t ReapCompleted();

        [[nodiscard]] JobServiceStats Stats() const;

        [[nodiscard]] GpuQueueParticipantHandle RegisterGpuQueueParticipant(
            GpuQueueParticipantDesc desc);
        void UnregisterGpuQueueParticipant(
            GpuQueueParticipantHandle handle,
            std::function<void()> waitForGpuIdle = {});
        void RecordGpuQueueFrameCommands(RHI::ICommandContext& commandContext);
        [[nodiscard]] std::uint64_t DrainGpuQueueCompletedTransfers();
        [[nodiscard]] bool HasGpuQueueWork() const;
        [[nodiscard]] std::uint64_t ShutdownGpuQueueParticipants(
            std::function<void()> waitForGpuIdle = {});

    private:
        struct SharedState;
        std::shared_ptr<SharedState> m_State;
    };
}
