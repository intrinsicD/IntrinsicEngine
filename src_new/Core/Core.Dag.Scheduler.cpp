module;

#include <algorithm>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>
#include <span>

module Extrinsic.Core.DagScheduler;

namespace Extrinsic::Core::Dag
{
    namespace
    {
        struct CachedTask
        {
            PendingTaskDesc desc{};
            std::vector<TaskId> dependsOn{};
            std::vector<ResourceAccess> resources{};
        };

        using TaskList = std::vector<CachedTask>;

        [[nodiscard]] Expected<std::vector<PlanTask>> BuildPlanFromTasks(
            const TaskList& tasks,
            const BuildConfig& config,
            ScheduleStats& outStats)
        {
            outStats.taskCount = static_cast<uint32_t>(tasks.size());
            if (tasks.empty())
                return std::vector<PlanTask>{};

            std::unordered_map<TaskId, std::size_t, StrongHandleHash<TaskTag>> idToIndex;
            idToIndex.reserve(tasks.size());
            for (std::size_t i = 0; i < tasks.size(); ++i)
            {
                const auto [_, inserted] = idToIndex.emplace(tasks[i].desc.id, i);
                if (!inserted)
                {
                    return Err<std::vector<PlanTask>>(ErrorCode::InvalidArgument);
                }
            }

            std::vector<uint32_t> inDegree(tasks.size(), 0);
            std::vector<std::vector<std::size_t>> graph(tasks.size());

            for (std::size_t i = 0; i < tasks.size(); ++i)
            {
                for (const auto dep : tasks[i].dependsOn)
                {
                    const auto depIt = idToIndex.find(dep);
                    if (depIt == idToIndex.end())
                    {
                        return Err<std::vector<PlanTask>>(ErrorCode::InvalidArgument);
                    }
                    graph[depIt->second].push_back(i);
                    inDegree[i] += 1;
                    outStats.edgeCount += 1;
                }
            }

            std::queue<std::size_t> ready;
            for (std::size_t i = 0; i < inDegree.size(); ++i)
            {
                if (inDegree[i] == 0)
                    ready.push(i);
            }

            uint32_t order = 0;
            uint32_t cpuLane = 0;
            uint32_t gpuLane = 0;
            uint32_t streamLane = 0;
            const auto cpuBudget = std::max<uint32_t>(config.queueBudgetCpu, 1u);
            const auto gpuBudget = std::max<uint32_t>(config.queueBudgetGpu, 1u);
            const auto streamBudget = std::max<uint32_t>(config.queueBudgetStreaming, 1u);

            std::vector<PlanTask> plan{};
            plan.reserve(tasks.size());
            std::vector<uint32_t> longestPath(tasks.size(), 0);

            while (!ready.empty())
            {
                outStats.maxReadyQueueDepth = std::max<uint32_t>(outStats.maxReadyQueueDepth,
                                                                 static_cast<uint32_t>(ready.size()));
                const auto idx = ready.front();
                ready.pop();

                const auto& task = tasks[idx].desc;
                uint32_t lane = 0;
                switch (task.domain)
                {
                case QueueDomain::Cpu:
                    lane = cpuLane++ % cpuBudget;
                    break;
                case QueueDomain::Gpu:
                    lane = gpuLane++ % gpuBudget;
                    break;
                case QueueDomain::Streaming:
                    lane = streamLane++ % streamBudget;
                    break;
                }

                plan.push_back(PlanTask{
                    .id = task.id,
                    .domain = task.domain,
                    .lane = lane,
                    .topoOrder = order,
                    .batch = static_cast<uint32_t>(task.priority),
                });
                ++order;

                for (const auto next : graph[idx])
                {
                    longestPath[next] = std::max(longestPath[next], longestPath[idx] + tasks[idx].desc.estimatedCost);
                    if (--inDegree[next] == 0)
                        ready.push(next);
                }
            }

            if (plan.size() != tasks.size())
                return Err<std::vector<PlanTask>>(ErrorCode::InvalidState);

            outStats.criticalPathCost = 0;
            for (std::size_t i = 0; i < tasks.size(); ++i)
            {
                outStats.criticalPathCost = std::max(outStats.criticalPathCost,
                                                     longestPath[i] + tasks[i].desc.estimatedCost);
            }
            return plan;
        }

        struct ProducerEntry
        {
            ProducerId id{};
            ProducerInfo info{};
            void* producerCtx = nullptr;
            QueryPendingTasksFn queryFn = nullptr;
        };

        class DagSchedulerImpl final : public DagScheduler
        {
        public:
            Expected<ProducerId> RegisterProducer(
                ProducerInfo info,
                void* producerCtx,
                QueryPendingTasksFn queryFn) override
            {
                if (queryFn == nullptr)
                    return Err<ProducerId>(ErrorCode::InvalidArgument);

                ProducerId id{m_NextProducerIndex++, 1};
                m_Producers.push_back(ProducerEntry{
                    .id = id,
                    .info = info,
                    .producerCtx = producerCtx,
                    .queryFn = queryFn,
                });
                return id;
            }

            Result UnregisterProducer(ProducerId producer) override
            {
                const auto it = std::find_if(m_Producers.begin(), m_Producers.end(), [producer](const ProducerEntry& p)
                {
                    return p.id == producer;
                });
                if (it == m_Producers.end())
                    return Err(ErrorCode::ResourceNotFound);

                m_Producers.erase(it);
                return Ok();
            }

            Result QueryAllPending() override
            {
                m_CachedTasks.clear();

                for (const auto& producer : m_Producers)
                {
                    auto emit = [](void* emitCtx, const PendingTaskDesc& pending) -> bool
                    {
                        auto* self = static_cast<DagSchedulerImpl*>(emitCtx);
                        return self->OnEmitTask(pending);
                    };

                    const auto r = producer.queryFn(producer.producerCtx, this, emit);
                    if (!r.has_value())
                        return r;
                }

                return Ok();
            }

            Expected<SchedulePlanView> BuildSchedule(const BuildConfig& config) override
            {
                m_LastPlan.clear();
                m_LastStats = {};
                m_LastStats.producerCount = static_cast<uint32_t>(m_Producers.size());

                auto plan = BuildPlanFromTasks(m_CachedTasks, config, m_LastStats);
                if (!plan.has_value())
                    return Err<SchedulePlanView>(plan.error());

                m_LastPlan = std::move(*plan);
                m_LastView = {.orderedTasks = std::span<const PlanTask>(m_LastPlan.data(), m_LastPlan.size())};
                return m_LastView;
            }

            ScheduleStats GetLastStats() const override
            {
                return m_LastStats;
            }

            void ResetEpoch() override
            {
                m_CachedTasks.clear();
                m_LastPlan.clear();
                m_LastStats = {};
                m_LastView = {};
            }

        private:
            bool OnEmitTask(const PendingTaskDesc& pending)
            {
                if (!pending.id.IsValid())
                    return false;

                CachedTask cached{};
                cached.desc = pending;
                cached.dependsOn.assign(pending.dependsOn.begin(), pending.dependsOn.end());
                cached.resources.assign(pending.resources.begin(), pending.resources.end());
                cached.desc.dependsOn = std::span<const TaskId>(cached.dependsOn.data(), cached.dependsOn.size());
                cached.desc.resources = std::span<const ResourceAccess>(cached.resources.data(), cached.resources.size());
                m_CachedTasks.push_back(std::move(cached));
                return true;
            }

            uint32_t m_NextProducerIndex = 0;
            std::vector<ProducerEntry> m_Producers{};
            TaskList m_CachedTasks{};
            std::vector<PlanTask> m_LastPlan{};
            ScheduleStats m_LastStats{};
            SchedulePlanView m_LastView{};
        };

        class DomainGraphBase
        {
        public:
            explicit DomainGraphBase(const QueueDomain domain) : m_Domain(domain) {}

            Result Submit(const PendingTaskDesc& task)
            {
                if (!task.id.IsValid() || task.domain != m_Domain)
                    return Err(ErrorCode::InvalidArgument);

                CachedTask cached{};
                cached.desc = task;
                cached.dependsOn.assign(task.dependsOn.begin(), task.dependsOn.end());
                cached.resources.assign(task.resources.begin(), task.resources.end());
                cached.desc.dependsOn = std::span<const TaskId>(cached.dependsOn.data(), cached.dependsOn.size());
                cached.desc.resources = std::span<const ResourceAccess>(cached.resources.data(), cached.resources.size());
                m_Tasks.push_back(std::move(cached));
                return Ok();
            }

            Expected<SchedulePlanView> BuildPlan(const BuildConfig& config)
            {
                m_LastStats = {};
                auto plan = BuildPlanFromTasks(m_Tasks, config, m_LastStats);
                if (!plan.has_value())
                    return Err<SchedulePlanView>(plan.error());

                m_LastPlan = std::move(*plan);
                m_LastView = {.orderedTasks = std::span<const PlanTask>(m_LastPlan.data(), m_LastPlan.size())};
                return m_LastView;
            }

            void Reset()
            {
                m_Tasks.clear();
                m_LastPlan.clear();
                m_LastStats = {};
                m_LastView = {};
            }

        private:
            QueueDomain m_Domain;
            TaskList m_Tasks{};
            std::vector<PlanTask> m_LastPlan{};
            ScheduleStats m_LastStats{};
            SchedulePlanView m_LastView{};
        };

        class CpuTaskGraphImpl final : public CpuTaskGraph
        {
        public:
            Result Submit(const PendingTaskDesc& task) override { return m_Base.Submit(task); }
            Expected<SchedulePlanView> BuildPlan(const BuildConfig& config) override { return m_Base.BuildPlan(config); }
            void Reset() override { m_Base.Reset(); }

        private:
            DomainGraphBase m_Base{QueueDomain::Cpu};
        };

        class GpuFrameGraphImpl final : public GpuFrameGraph
        {
        public:
            Result Submit(const PendingTaskDesc& task) override { return m_Base.Submit(task); }
            Expected<SchedulePlanView> BuildPlan(const BuildConfig& config) override { return m_Base.BuildPlan(config); }
            void Reset() override { m_Base.Reset(); }

        private:
            DomainGraphBase m_Base{QueueDomain::Gpu};
        };

        class AsyncStreamingGraphImpl final : public AsyncStreamingGraph
        {
        public:
            Result Submit(const PendingTaskDesc& task) override { return m_Base.Submit(task); }
            Expected<SchedulePlanView> BuildPlan(const BuildConfig& config) override { return m_Base.BuildPlan(config); }
            void Reset() override { m_Base.Reset(); }

        private:
            DomainGraphBase m_Base{QueueDomain::Streaming};
        };
    }

    std::unique_ptr<DagScheduler> CreateDagScheduler()
    {
        return std::make_unique<DagSchedulerImpl>();
    }

    std::unique_ptr<CpuTaskGraph> CreateCpuTaskGraph()
    {
        return std::make_unique<CpuTaskGraphImpl>();
    }

    std::unique_ptr<GpuFrameGraph> CreateGpuFrameGraph()
    {
        return std::make_unique<GpuFrameGraphImpl>();
    }

    std::unique_ptr<AsyncStreamingGraph> CreateAsyncStreamingGraph()
    {
        return std::make_unique<AsyncStreamingGraphImpl>();
    }
}
