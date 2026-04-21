module;

#include <algorithm>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>
#include <span>

module Extrinsic.Core.Dag.Scheduler;

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

        // HLFET (Highest Levels First with Estimated Times): among all
        // ready tasks, emit the one with the most urgent priority, breaking
        // ties on longest remaining path to a sink (a.k.a. the node's "level").
        // This approximates makespan-minimizing critical-path scheduling and
        // respects TaskPriority as a hard override.
        //
        // Passes:
        //   1. Build index + adjacency; reject missing deps / duplicate ids.
        //   2. Kahn topological pre-pass → topo order (and cycle detection).
        //   3. Reverse DP over the topo order → level[i] = cost[i] + max(level[succ]).
        //   4. Priority-aware Kahn emission: ready set is a priority queue
        //      keyed by (priority, -level, insertion) to guarantee a stable
        //      total ordering.
        [[nodiscard]] Expected<std::vector<PlanTask>> BuildPlanFromTasks(
            const TaskList& tasks,
            const BuildConfig& config,
            ScheduleStats& outStats)
        {
            outStats.taskCount = static_cast<uint32_t>(tasks.size());
            if (tasks.empty())
                return std::vector<PlanTask>{};

            const auto N = tasks.size();

            // Pass 1: index and adjacency.
            std::unordered_map<TaskId, std::size_t, StrongHandleHash<TaskTag>> idToIndex;
            idToIndex.reserve(N);
            for (std::size_t i = 0; i < N; ++i)
            {
                const auto [_, inserted] = idToIndex.emplace(tasks[i].desc.id, i);
                if (!inserted)
                    return Err<std::vector<PlanTask>>(ErrorCode::InvalidArgument);
            }

            std::vector<uint32_t> inDegree(N, 0);
            std::vector<std::vector<std::size_t>> successors(N);
            for (std::size_t i = 0; i < N; ++i)
            {
                for (const auto dep : tasks[i].dependsOn)
                {
                    const auto depIt = idToIndex.find(dep);
                    if (depIt == idToIndex.end())
                        return Err<std::vector<PlanTask>>(ErrorCode::InvalidArgument);
                    successors[depIt->second].push_back(i);
                    inDegree[i] += 1;
                    outStats.edgeCount += 1;
                }
            }

            // Pass 2: topological pre-pass. Also detects cycles.
            std::vector<std::size_t> topo;
            topo.reserve(N);
            {
                std::vector<uint32_t> inDeg = inDegree;
                std::queue<std::size_t> q;
                for (std::size_t i = 0; i < N; ++i)
                    if (inDeg[i] == 0)
                        q.push(i);
                while (!q.empty())
                {
                    const auto u = q.front();
                    q.pop();
                    topo.push_back(u);
                    for (const auto v : successors[u])
                        if (--inDeg[v] == 0)
                            q.push(v);
                }
                if (topo.size() != N)
                    return Err<std::vector<PlanTask>>(ErrorCode::InvalidState);
            }

            // Pass 3: reverse DP for the longest remaining path (a.k.a. level).
            std::vector<uint32_t> level(N, 0);
            for (auto it = topo.rbegin(); it != topo.rend(); ++it)
            {
                const auto u = *it;
                uint32_t maxSucc = 0;
                for (const auto v : successors[u])
                    maxSucc = std::max(maxSucc, level[v]);
                level[u] = tasks[u].desc.estimatedCost + maxSucc;
            }

            outStats.criticalPathCost = 0;
            for (std::size_t i = 0; i < N; ++i)
                if (inDegree[i] == 0)
                    outStats.criticalPathCost = std::max(outStats.criticalPathCost, level[i]);

            // Pass 4: priority-aware Kahn emission.
            struct ReadyEntry
            {
                uint8_t priority;     // TaskPriority rank; lower = more urgent
                uint32_t level;       // longest remaining path including self
                uint32_t insertion;   // stable tie-breaker
                std::size_t index;
            };
            // std::priority_queue is a max-heap; order so the "best" entry
            // comes out on top. "Best" = lowest priority, then highest level,
            // then lowest insertion.
            const auto cmp = [](const ReadyEntry& a, const ReadyEntry& b) noexcept
            {
                if (a.priority != b.priority) return a.priority > b.priority;
                if (a.level != b.level) return a.level < b.level;
                return a.insertion > b.insertion;
            };

            std::priority_queue<ReadyEntry, std::vector<ReadyEntry>, decltype(cmp)> ready(cmp);
            uint32_t nextInsertion = 0;
            const auto enqueue = [&](const std::size_t i)
            {
                ready.push(ReadyEntry{
                    .priority = static_cast<uint8_t>(tasks[i].desc.priority),
                    .level = level[i],
                    .insertion = nextInsertion++,
                    .index = i,
                });
            };
            for (std::size_t i = 0; i < N; ++i)
                if (inDegree[i] == 0)
                    enqueue(i);

            std::vector<uint32_t> inDeg = inDegree;
            const auto cpuBudget = std::max<uint32_t>(config.queueBudgetCpu, 1u);
            const auto gpuBudget = std::max<uint32_t>(config.queueBudgetGpu, 1u);
            const auto streamBudget = std::max<uint32_t>(config.queueBudgetStreaming, 1u);
            uint32_t cpuLane = 0, gpuLane = 0, streamLane = 0;
            uint32_t order = 0;

            std::vector<PlanTask> plan;
            plan.reserve(N);

            while (!ready.empty())
            {
                outStats.maxReadyQueueDepth = std::max<uint32_t>(
                    outStats.maxReadyQueueDepth, static_cast<uint32_t>(ready.size()));
                const auto entry = ready.top();
                ready.pop();

                const auto& task = tasks[entry.index].desc;
                uint32_t lane = 0;
                switch (task.domain)
                {
                case QueueDomain::Cpu:       lane = cpuLane++ % cpuBudget; break;
                case QueueDomain::Gpu:       lane = gpuLane++ % gpuBudget; break;
                case QueueDomain::Streaming: lane = streamLane++ % streamBudget; break;
                }

                plan.push_back(PlanTask{
                    .id = task.id,
                    .domain = task.domain,
                    .lane = lane,
                    .topoOrder = order++,
                    .batch = static_cast<uint32_t>(task.priority),
                });

                for (const auto v : successors[entry.index])
                    if (--inDeg[v] == 0)
                        enqueue(v);
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

            Expected<std::vector<PlanTask>> BuildSchedule(const BuildConfig& config) override
            {
                m_LastStats = {};
                m_LastStats.producerCount = static_cast<uint32_t>(m_Producers.size());

                return BuildPlanFromTasks(m_CachedTasks, config, m_LastStats);
            }

            ScheduleStats GetLastStats() const override
            {
                return m_LastStats;
            }

            void ResetEpoch() override
            {
                m_CachedTasks.clear();
                m_LastStats = {};
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
            ScheduleStats m_LastStats{};
        };

    }

    std::unique_ptr<DagScheduler> CreateDagScheduler()
    {
        return std::make_unique<DagSchedulerImpl>();
    }

    // -----------------------------------------------------------------------
    // DomainTaskGraph — raw PendingTaskDesc-based submit/plan graph
    // -----------------------------------------------------------------------
    namespace
    {
        class DomainTaskGraphImpl final : public DomainTaskGraph
        {
        public:
            explicit DomainTaskGraphImpl(QueueDomain domain) : m_Domain(domain) {}

            Result Submit(const PendingTaskDesc& task) override
            {
                if (!task.id.IsValid() || task.domain != m_Domain)
                    return Err(ErrorCode::InvalidArgument);
                CachedTask cached{};
                cached.desc = task;
                cached.dependsOn.assign(task.dependsOn.begin(), task.dependsOn.end());
                cached.resources.assign(task.resources.begin(), task.resources.end());
                cached.desc.dependsOn = std::span<const TaskId>(
                    cached.dependsOn.data(), cached.dependsOn.size());
                cached.desc.resources = std::span<const ResourceAccess>(
                    cached.resources.data(), cached.resources.size());
                m_Tasks.push_back(std::move(cached));
                return Ok();
            }

            Expected<std::vector<PlanTask>> BuildPlan(const BuildConfig& config) override
            {
                m_LastStats = {};
                return BuildPlanFromTasks(m_Tasks, config, m_LastStats);
            }

            QueueDomain Domain() const noexcept override { return m_Domain; }

            void Reset() override { m_Tasks.clear(); m_LastStats = {}; }

        private:
            QueueDomain   m_Domain;
            TaskList      m_Tasks{};
            ScheduleStats m_LastStats{};
        };
    }

    std::unique_ptr<DomainTaskGraph> CreateDomainTaskGraph(QueueDomain domain)
    {
        return std::make_unique<DomainTaskGraphImpl>(domain);
    }
}
