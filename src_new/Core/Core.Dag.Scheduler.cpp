module;

#include <algorithm>
#include <functional>
#include <memory>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
                std::string debugNameStorage{};
                std::vector<TaskId> dependsOn{};
                std::vector<ResourceAccess> resources{};
            };

            void InitializeCachedTask(CachedTask& cached, const PendingTaskDesc& pending)
            {
                cached.desc = pending;
                cached.debugNameStorage = std::string(pending.debugName);
                cached.dependsOn.assign(pending.dependsOn.begin(), pending.dependsOn.end());
                cached.resources.assign(pending.resources.begin(), pending.resources.end());
                cached.desc.debugName = cached.debugNameStorage;
                cached.desc.dependsOn = std::span<const TaskId>(cached.dependsOn.data(), cached.dependsOn.size());
                cached.desc.resources = std::span<const ResourceAccess>(cached.resources.data(), cached.resources.size());
            }

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
            TaskList& tasks,
            const BuildConfig& config,
            ScheduleStats& outStats)
        {
            outStats.lastDiagnostic.clear();
            outStats.taskCount = static_cast<uint32_t>(tasks.size());
            if (tasks.empty())
                return std::vector<PlanTask>{};

            for (auto& task : tasks)
            {
                task.desc.debugName = task.debugNameStorage;
                task.desc.dependsOn = std::span<const TaskId>(task.dependsOn.data(), task.dependsOn.size());
                task.desc.resources = std::span<const ResourceAccess>(task.resources.data(), task.resources.size());
            }

            const auto N = tasks.size();

            // Pass 1: index and adjacency.
            std::unordered_map<TaskId, std::size_t, StrongHandleHash<TaskTag>> idToIndex;
            idToIndex.reserve(N);
            const auto taskLabel = [&](std::size_t index) -> std::string
            {
                const auto& task = tasks[index];
                if (!task.desc.debugName.empty())
                    return std::string(task.desc.debugName);
                std::ostringstream oss;
                oss << "Task(" << task.desc.id.Index << ":" << task.desc.id.Generation << ")";
                return oss.str();
            };
            for (std::size_t i = 0; i < N; ++i)
            {
                const auto [_, inserted] = idToIndex.emplace(tasks[i].desc.id, i);
                if (!inserted)
                {
                    outStats.lastDiagnostic = "Duplicate TaskId detected at " + taskLabel(i);
                    return Err<std::vector<PlanTask>>(ErrorCode::InvalidArgument);
                }
            }

            std::vector<uint32_t> inDegree(N, 0);
            std::vector<std::vector<std::size_t>> successors(N);
            std::vector<std::vector<std::size_t>> predecessors(N);
            enum class EdgeReason : uint8_t
            {
                ExplicitDependency = 0,
                HazardRaw,
                HazardWaw,
                HazardWar,
            };
            std::unordered_set<uint64_t> seenEdges{};
            seenEdges.reserve(N * 2);
            std::unordered_map<uint64_t, EdgeReason> edgeReasons{};
            edgeReasons.reserve(N * 2);
            const auto encodeEdge = [](std::size_t from, std::size_t to) noexcept -> uint64_t
            {
                return (static_cast<uint64_t>(from) << 32) | static_cast<uint64_t>(to);
            };
            const auto addEdge = [&](std::size_t from, std::size_t to, const EdgeReason reason)
            {
                if (from == to)
                    return;
                const uint64_t key = encodeEdge(from, to);
                if (!seenEdges.insert(key).second)
                    return;
                successors[from].push_back(to);
                predecessors[to].push_back(from);
                inDegree[to] += 1;
                outStats.edgeCount += 1;
                if (reason == EdgeReason::ExplicitDependency)
                    outStats.explicitEdgeCount += 1;
                else
                    outStats.hazardEdgeCount += 1;
                edgeReasons.emplace(key, reason);
            };
            for (std::size_t i = 0; i < N; ++i)
            {
                for (const auto dep : tasks[i].dependsOn)
                {
                    const auto depIt = idToIndex.find(dep);
                    if (depIt == idToIndex.end())
                    {
                        std::ostringstream oss;
                        oss << "Missing dependency for " << taskLabel(i)
                            << " -> Task(" << dep.Index << ":" << dep.Generation << ")";
                        outStats.lastDiagnostic = oss.str();
                        return Err<std::vector<PlanTask>>(ErrorCode::InvalidArgument);
                    }
                    addEdge(depIt->second, i, EdgeReason::ExplicitDependency);
                }
            }

            std::vector<PendingTaskDesc> hazardTaskView{};
            hazardTaskView.reserve(N);
            for (const auto& task : tasks)
                hazardTaskView.push_back(task.desc);

            const ResourceHazardBuilder hazardBuilder{};
            const auto hazardEdges = hazardBuilder.Build(hazardTaskView);
            for (const auto& edge : hazardEdges)
            {
                const auto reason = [&]() -> EdgeReason
                {
                    switch (edge.kind)
                    {
                    case HazardKind::Raw: return EdgeReason::HazardRaw;
                    case HazardKind::Waw: return EdgeReason::HazardWaw;
                    case HazardKind::War: return EdgeReason::HazardWar;
                    case HazardKind::None: return EdgeReason::HazardRaw;
                    }
                    return EdgeReason::HazardRaw;
                }();
                addEdge(edge.from, edge.to, reason);
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
                {
                    std::vector<uint8_t> color(N, 0);
                    std::vector<std::size_t> stack{};
                    std::vector<std::size_t> cycle{};
                    std::function<bool(std::size_t)> dfs = [&](std::size_t node) -> bool
                    {
                        color[node] = 1;
                        stack.push_back(node);
                        for (const auto next : successors[node])
                        {
                            if (color[next] == 0)
                            {
                                if (dfs(next))
                                    return true;
                            }
                            else if (color[next] == 1)
                            {
                                const auto it = std::find(stack.begin(), stack.end(), next);
                                if (it != stack.end())
                                {
                                    cycle.assign(it, stack.end());
                                    cycle.push_back(next);
                                }
                                return true;
                            }
                        }
                        stack.pop_back();
                        color[node] = 2;
                        return false;
                    };
                    for (std::size_t i = 0; i < N; ++i)
                    {
                        if (color[i] == 0 && dfs(i))
                            break;
                    }

                    std::ostringstream oss;
                    oss << "Cycle detected";
                    if (!cycle.empty())
                    {
                        oss << ": ";
                        for (std::size_t i = 0; i + 1 < cycle.size(); ++i)
                        {
                            const auto from = cycle[i];
                            const auto to = cycle[i + 1];
                            oss << taskLabel(from);
                            const auto key = encodeEdge(from, to);
                            if (const auto reasonIt = edgeReasons.find(key); reasonIt != edgeReasons.end())
                            {
                                oss << " --";
                                switch (reasonIt->second)
                                {
                                case EdgeReason::ExplicitDependency: oss << "explicit"; break;
                                case EdgeReason::HazardRaw: oss << "RAW"; break;
                                case EdgeReason::HazardWaw: oss << "WAW"; break;
                                case EdgeReason::HazardWar: oss << "WAR"; break;
                                }
                                oss << "--> ";
                            }
                            else
                            {
                                oss << " --> ";
                            }
                        }
                        oss << taskLabel(cycle.back());
                    }
                    outStats.lastDiagnostic = oss.str();
                    return Err<std::vector<PlanTask>>(ErrorCode::InvalidState);
                }
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


            std::vector<uint32_t> topoLayer(N, 0);
            uint32_t maxLayer = 0;
            for (const auto nodeIndex : topo)
            {
                uint32_t nodeLayer = 0;
                for (const auto predIndex : predecessors[nodeIndex])
                    nodeLayer = std::max(nodeLayer, topoLayer[predIndex] + 1);
                topoLayer[nodeIndex] = nodeLayer;
                maxLayer = std::max(maxLayer, nodeLayer);
            }
            outStats.layerCount = maxLayer + 1;

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
                    .batch = topoLayer[entry.index],
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

                m_CachedTasks.emplace_back();
                InitializeCachedTask(m_CachedTasks.back(), pending);
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

                m_Tasks.emplace_back();
                InitializeCachedTask(m_Tasks.back(), task);
                return Ok();
            }

            Expected<std::vector<PlanTask>> BuildPlan(const BuildConfig& config) override
            {
                m_LastStats = {};
                return BuildPlanFromTasks(m_Tasks, config, m_LastStats);
            }

            ScheduleStats GetLastStats() const override { return m_LastStats; }

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
