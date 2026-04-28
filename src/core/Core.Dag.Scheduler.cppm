module;

#include <string_view>
#include <memory>
#include <vector>

export module Extrinsic.Core.Dag.Scheduler;

import Extrinsic.Core.Error;

export import :Types;
export import :Hazards;
export import :Policy;
export import :Compiler;
export import :DomainGraph;

export namespace Extrinsic::Core::Dag
{
    struct ProducerInfo
    {
        std::string_view name{};
        uint32_t subsystemId = 0;
        QueueDomain preferredDomain = QueueDomain::Cpu;
    };

    using EmitPendingTaskFn = bool(*)(void* emitCtx, const PendingTaskDesc&);
    using QueryPendingTasksFn = Result(*)(void* producerCtx, void* emitCtx, EmitPendingTaskFn emit);

    class DagScheduler
    {
    public:
        DagScheduler() = default;
        DagScheduler(const DagScheduler&) = delete;
        DagScheduler& operator=(const DagScheduler&) = delete;

        [[nodiscard]] virtual Expected<ProducerId> RegisterProducer(
            ProducerInfo info,
            void* producerCtx,
            QueryPendingTasksFn queryFn) = 0;

        virtual Result UnregisterProducer(ProducerId producer) = 0;
        virtual Result QueryAllPending() = 0;
        [[nodiscard]] virtual Expected<std::vector<PlanTask>> BuildSchedule(const BuildConfig& config) = 0;
        [[nodiscard]] virtual ScheduleStats GetLastStats() const = 0;
        virtual void ResetEpoch() = 0;
        virtual ~DagScheduler() = default;
    };

    [[nodiscard]] std::unique_ptr<DagScheduler> CreateDagScheduler();
}
