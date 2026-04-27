module;

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cassert>
#include <mutex>
#include <cstdint>
#include <expected>
#include <limits>
#include <sstream>
#include <memory>
#include <functional>
#include <queue>
#include <string>
#include <string_view>
#include <span>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

module Extrinsic.Core.Dag.TaskGraph;

import Extrinsic.Core.Logging;
import Extrinsic.Core.Tasks;
import Extrinsic.Core.Tasks.CounterEvent;

namespace Extrinsic::Core::Dag
{
    namespace
    {
        struct ResourceState
        {
            std::int32_t LastWriter = -1;
            std::vector<std::uint32_t> CurrentReaders{};
        };

        struct CompiledPlanState
        {
            std::vector<std::uint32_t> ExecutionOrder{};
            std::vector<std::uint32_t> PassBatch{};
            std::vector<std::vector<std::uint32_t>> Layers{};
            std::vector<std::vector<std::uint32_t>> Successors{};
            std::vector<std::uint32_t> InitialInDegree{};
            ScheduleStats Stats{};
        };

        [[nodiscard]] constexpr std::uint64_t PackResource(const ResourceId resource) noexcept
        {
            return (static_cast<std::uint64_t>(resource.Generation) << 32) |
                static_cast<std::uint64_t>(resource.Index);
        }

        [[nodiscard]] bool IsWriteMode(const ResourceAccessMode mode) noexcept
        {
            return mode == ResourceAccessMode::Write || mode == ResourceAccessMode::ReadWrite;
        }

        [[nodiscard]] bool IsReadMode(const ResourceAccessMode mode) noexcept
        {
            return mode == ResourceAccessMode::Read || mode == ResourceAccessMode::ReadWrite;
        }

        enum class EdgeReason : uint8_t
        {
            ExplicitDependency = 0,
            HazardRaw,
            HazardWaw,
            HazardWar,
            LabelDependency,
        };

        struct EdgeReasonInfo
        {
            EdgeReason Kind = EdgeReason::ExplicitDependency;
            std::uint32_t LabelValue = 0;
        };

        void AppendCycleDiagnostic(
            const std::vector<std::uint32_t>& cycle,
            const std::vector<std::string>& passNames,
            const std::vector<std::uint32_t>& taskIds,
            const std::unordered_map<std::uint64_t, EdgeReasonInfo>& edgeReasons,
            std::string* outDiagnostic)
        {
            if (!outDiagnostic)
                return;

            auto taskLabel = [&](const std::uint32_t index) -> std::string
            {
                if (index < passNames.size())
                    return passNames[index];
                return std::to_string(taskIds[index]);
            };

            std::ostringstream oss;
            oss << "Cycle detected: ";
            for (std::size_t i = 0; i < cycle.size(); ++i)
            {
                const auto from = cycle[i];
                const auto next = cycle[(i + 1u) % cycle.size()];
                oss << taskLabel(from);
                const auto key = (static_cast<std::uint64_t>(from) << 32) | static_cast<std::uint64_t>(next);
                if (const auto reasonIt = edgeReasons.find(key); reasonIt != edgeReasons.end())
                {
                    const auto& reason = reasonIt->second;
                    oss << " --";
                    switch (reason.Kind)
                    {
                    case EdgeReason::ExplicitDependency: oss << "explicit"; break;
                    case EdgeReason::HazardRaw: oss << "RAW"; break;
                    case EdgeReason::HazardWaw: oss << "WAW"; break;
                    case EdgeReason::HazardWar: oss << "WAR"; break;
                    case EdgeReason::LabelDependency: oss << "label(" << reason.LabelValue << ")"; break;
                    }
                    oss << "--> ";
                }
                else
                {
                    oss << " --> ";
                }
            }
            if (!cycle.empty())
            {
                oss << taskLabel(cycle.front());
            }
            *outDiagnostic = oss.str();
        }

        template <typename PassContainer>
        [[nodiscard]] Core::Expected<CompiledPlanState> CompileGraph(
            const PassContainer& passes,
            const std::uint32_t nodeCount,
            std::string* outDiagnostic = nullptr,
            std::span<const std::uint32_t> labelValues = {})
        {
            CompiledPlanState compiled{};
            compiled.Stats.lastDiagnostic.clear();
            if (outDiagnostic)
                outDiagnostic->clear();
            compiled.Stats.taskCount = nodeCount;
            compiled.PassBatch.assign(nodeCount, 0u);
            compiled.InitialInDegree.assign(nodeCount, 0u);
            compiled.Layers.clear();

            if (nodeCount == 0)
                return compiled;

            if (nodeCount == 1)
            {
                compiled.ExecutionOrder.push_back(0u);
                compiled.PassBatch[0] = 0u;
                compiled.Layers.push_back({0u});
                compiled.Successors.resize(1);
                compiled.Stats.hazardEdgeCount = 0;
                compiled.Stats.explicitEdgeCount = 0;
                compiled.Stats.edgeCount = 0;
                compiled.Stats.layerCount = 1u;
                compiled.Stats.criticalPathCost = passes[0].Options.EstimatedCost;
                return compiled;
            }

            auto addEdge = [&](const std::uint32_t from,
                               const std::uint32_t to,
                               const EdgeReason reason,
                               const std::uint32_t labelValue,
                               auto& seenEdges,
                               auto& successors,
                               auto& predecessors,
                               auto& inDegree,
                               auto& edgeReasons,
                               ScheduleStats& stats) -> void
            {
                if (from == to)
                    return;

                const std::uint64_t key = (static_cast<std::uint64_t>(from) << 32) | to;
                const auto [_, inserted] = seenEdges.insert(key);
                if (!inserted)
                    return;

                successors[from].push_back(to);
                predecessors[to].push_back(from);
                ++inDegree[to];
                ++stats.edgeCount;
                if (reason == EdgeReason::ExplicitDependency || reason == EdgeReason::LabelDependency)
                    ++stats.explicitEdgeCount;
                else
                    ++stats.hazardEdgeCount;
                edgeReasons.emplace(key, EdgeReasonInfo{reason, labelValue});
            };

            std::vector<std::vector<std::uint32_t>> successors(nodeCount);
            std::vector<std::vector<std::uint32_t>> predecessors(nodeCount);
            std::vector<std::uint32_t> inDegree(nodeCount, 0u);
            compiled.InitialInDegree = inDegree;
            std::unordered_set<std::uint64_t> seenEdges{};
            seenEdges.reserve(nodeCount * 4u + 16u);
            std::unordered_map<std::uint64_t, EdgeReasonInfo> edgeReasons{};
            edgeReasons.reserve(nodeCount * 4u + 16u);

            std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> labelSignals{};
            labelSignals.reserve(nodeCount * 2u + 16u);

            std::unordered_map<std::uint64_t, ResourceState> resourceStates{};
            resourceStates.reserve(nodeCount * 2u + 16u);

            for (std::uint32_t i = 0; i < nodeCount; ++i)
            {
                const auto& pass = passes[i];
                for (const auto dependency : pass.Dependencies)
                {
                    if (dependency >= nodeCount)
                    {
                        const auto msg = "Invalid explicit dependency in pass '" + std::string(passes[i].Name) +
                            "': predecessor index " + std::to_string(dependency) + " is out of range";
                        compiled.Stats.lastDiagnostic = msg;
                        if (outDiagnostic)
                            *outDiagnostic = msg;
                        return Err<CompiledPlanState>(ErrorCode::InvalidState);
                    }
                    addEdge(
                        dependency,
                        i,
                        EdgeReason::ExplicitDependency,
                        0u,
                        seenEdges,
                        successors,
                        predecessors,
                        inDegree,
                        edgeReasons,
                        compiled.Stats);
                }

                for (const auto& access : pass.Resources)
                {
                    auto& state = resourceStates[PackResource(access.resource)];

                    const bool isWriter = IsWriteMode(access.mode);
                    if (state.LastWriter != -1)
                    {
                        const auto reason = isWriter ? EdgeReason::HazardWaw : EdgeReason::HazardRaw;
                        addEdge(
                            static_cast<std::uint32_t>(state.LastWriter),
                            i,
                            reason,
                            0u,
                            seenEdges,
                            successors,
                            predecessors,
                            inDegree,
                            edgeReasons,
                            compiled.Stats);
                    }

                    if (isWriter)
                    {
                        for (const auto reader : state.CurrentReaders)
                        {
                            addEdge(
                                reader,
                                i,
                                EdgeReason::HazardWar,
                                0u,
                                seenEdges,
                                successors,
                                predecessors,
                                inDegree,
                                edgeReasons,
                                compiled.Stats);
                        }

                        state.CurrentReaders.clear();
                        state.LastWriter = static_cast<std::int32_t>(i);
                    }
                    else if (IsReadMode(access.mode))
                    {
                        state.CurrentReaders.push_back(i);
                    }
                }
            }

            for (std::uint32_t i = 0; i < nodeCount; ++i)
            {
                const auto& pass = passes[i];
                for (const auto& waitLabel : pass.WaitLabels)
                {
                    auto signalIt = labelSignals.find(waitLabel);
                    if (signalIt == labelSignals.end())
                    {
                        const auto labelValue = (waitLabel < labelValues.size()) ? labelValues[waitLabel] : 0u;
                        compiled.Stats.lastDiagnostic =
                            "No prior signaler for wait label (" + std::to_string(labelValue) +
                            ") in pass '" + std::string(pass.Name) + "'";
                        if (outDiagnostic)
                            *outDiagnostic = compiled.Stats.lastDiagnostic;
                        return Err<CompiledPlanState>(ErrorCode::InvalidState);
                    }

                    for (const auto signaler : signalIt->second)
                    {
                        const auto labelValue = (waitLabel < labelValues.size()) ? labelValues[waitLabel] : 0u;
                        addEdge(signaler, i, EdgeReason::LabelDependency, labelValue, seenEdges,
                                successors, predecessors, inDegree, edgeReasons, compiled.Stats);
                    }
                }

                for (const auto& signalLabel : pass.SignalLabels)
                {
                    auto& signalers = labelSignals[signalLabel];
                    if (signalers.empty() || signalers.back() != i)
                        signalers.push_back(i);
                }
            }

            compiled.InitialInDegree = inDegree;

            // Topological sort with queue for cycle detection.
            std::vector<std::uint32_t> topo{};
            topo.reserve(nodeCount);
            {
                std::vector<std::uint32_t> inDegreeForKahn = inDegree;
                std::queue<std::uint32_t> q{};
                for (std::uint32_t i = 0; i < nodeCount; ++i)
                {
                    if (inDegreeForKahn[i] == 0u)
                        q.push(i);
                }

                while (!q.empty())
                {
                    const auto node = q.front();
                    q.pop();
                    topo.push_back(node);
                    for (const auto successor : successors[node])
                    {
                        if (--inDegreeForKahn[successor] == 0u)
                            q.push(successor);
                    }
                }

                if (topo.size() != nodeCount)
                {
                    std::vector<std::uint8_t> color(nodeCount, 0);
                    std::vector<std::uint32_t> cycle{};
                    std::vector<std::uint32_t> stack{};

                    std::vector<std::string> passNames{};
                    passNames.reserve(nodeCount);
                    std::vector<std::uint32_t> taskIds{};
                    taskIds.reserve(nodeCount);
                    for (std::uint32_t i = 0; i < nodeCount; ++i)
                    {
                        passNames.push_back(passes[i].Name);
                        taskIds.push_back(i);
                    }

                    std::function<bool(std::uint32_t)> dfs = [&](const std::uint32_t node) -> bool
                    {
                        if (color[node] == 1u)
                            return false;

                        color[node] = 1u;
                        stack.push_back(node);
                        for (const auto next : successors[node])
                        {
                            if (color[next] == 0u)
                            {
                                if (dfs(next))
                                    return true;
                            }
                            else if (color[next] == 1u)
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
                        color[node] = 2u;
                        return false;
                    };

                    for (std::uint32_t i = 0; i < nodeCount; ++i)
                    {
                        if (color[i] == 0u && dfs(i))
                            break;
                    }

                    if (!cycle.empty())
                    {
                        AppendCycleDiagnostic(cycle, passNames, taskIds, edgeReasons, &compiled.Stats.lastDiagnostic);
                    }
                    else
                    {
                        std::string unresolved;
                        for (std::uint32_t i = 0; i < nodeCount; ++i)
                        {
                            if (std::find(topo.begin(), topo.end(), i) != topo.end())
                                continue;
                            unresolved += passes[i].Name;
                            unresolved += ", ";
                        }
                        compiled.Stats.lastDiagnostic = "Cycle detected in task graph. Unresolved nodes: " + unresolved;
                    }

                    if (outDiagnostic)
                        *outDiagnostic = compiled.Stats.lastDiagnostic;
                    return Err<CompiledPlanState>(ErrorCode::InvalidState);
                }
            }

            // longest remaining path ("critical cost") on reversed topo.
            std::vector<std::uint32_t> level(nodeCount, 0u);
            for (auto it = topo.rbegin(); it != topo.rend(); ++it)
            {
                const auto u = *it;
                std::uint32_t maxSucc = 0u;
                for (const auto v : successors[u])
                    maxSucc = std::max(maxSucc, level[v]);
                const auto estimatedCost = std::max<std::uint32_t>(1u, passes[u].Options.EstimatedCost);
                level[u] = estimatedCost + maxSucc;
            }

            // Layer assignment by longest predecessor path.
            std::vector<std::uint32_t> topoLayer(nodeCount, 0u);
            std::uint32_t maxLayer = 0u;
            for (const auto node : topo)
            {
                std::uint32_t nodeLayer = 0u;
                for (const auto pred : predecessors[node])
                    nodeLayer = std::max(nodeLayer, topoLayer[pred] + 1u);
                topoLayer[node] = nodeLayer;
                maxLayer = std::max(maxLayer, nodeLayer);
            }
            compiled.Stats.layerCount = maxLayer + 1u;

            compiled.Stats.criticalPathCost = 0u;
            for (std::uint32_t i = 0; i < nodeCount; ++i)
            {
                if (compiled.InitialInDegree[i] == 0u)
                    compiled.Stats.criticalPathCost = std::max(compiled.Stats.criticalPathCost, level[i]);
            }

            struct ReadyEntry
            {
                std::uint8_t priority{};
                std::uint32_t level{};
                std::uint32_t insertion{};
                std::uint32_t index{};
            };

            const auto readyCmp = [](const ReadyEntry& a, const ReadyEntry& b) noexcept
            {
                if (a.priority != b.priority) return a.priority > b.priority;
                if (a.level != b.level) return a.level < b.level;
                return a.insertion > b.insertion;
            };

            std::priority_queue<ReadyEntry, std::vector<ReadyEntry>, decltype(readyCmp)> ready(readyCmp);
            std::uint32_t insertionCounter = 0u;
            auto enqueue = [&](const std::uint32_t node)
            {
                ready.push(ReadyEntry{
                    .priority = static_cast<std::uint8_t>(passes[node].Options.Priority),
                    .level = level[node],
                    .insertion = insertionCounter++,
                    .index = node,
                });
            };

            std::vector<std::uint32_t> mutableInDegree = inDegree;
            for (std::uint32_t i = 0; i < nodeCount; ++i)
            {
                if (mutableInDegree[i] == 0u)
                    enqueue(i);
            }

            compiled.ExecutionOrder.reserve(nodeCount);
            while (!ready.empty())
            {
                const auto entry = ready.top();
                ready.pop();

                compiled.Stats.maxReadyQueueDepth = std::max<std::uint32_t>(
                    compiled.Stats.maxReadyQueueDepth,
                    static_cast<std::uint32_t>(ready.size() + 1u));

                const auto node = entry.index;
                compiled.ExecutionOrder.push_back(node);
                compiled.PassBatch[node] = topoLayer[node];

                const auto batch = topoLayer[node];
                if (compiled.Layers.size() <= batch)
                    compiled.Layers.resize(batch + 1u);
                compiled.Layers[batch].push_back(node);
                for (const auto successor : successors[node])
                {
                    if (mutableInDegree[successor] == 0u)
                    {
                        Log::Warn("[TaskGraph] Invalid indegree during compile; graph may have duplicate edges");
                        compiled.Stats.lastDiagnostic =
                            "Topological emission encountered invalid indegree transition";
                        if (outDiagnostic)
                            *outDiagnostic = compiled.Stats.lastDiagnostic;
                        return Err<CompiledPlanState>(ErrorCode::InvalidState);
                    }
                    --mutableInDegree[successor];
                    if (mutableInDegree[successor] == 0u)
                        ready.push(ReadyEntry{
                            .priority = static_cast<std::uint8_t>(passes[successor].Options.Priority),
                            .level = level[successor],
                            .insertion = insertionCounter++,
                            .index = successor,
                        });
                }
            }

            if (compiled.ExecutionOrder.size() != nodeCount)
            {
                compiled.Stats.lastDiagnostic = "Topological emission failed to emit all nodes";
                if (outDiagnostic)
                    *outDiagnostic = compiled.Stats.lastDiagnostic;
                return Err<CompiledPlanState>(ErrorCode::InvalidState);
            }

            compiled.Successors = std::move(successors);
            return compiled;
        }

        struct ExecutionState
        {
            std::vector<std::atomic<uint32_t>> RemainingDeps{};
            std::vector<std::atomic<uint8_t>> Dispatched{};
            std::vector<std::uint32_t> MainThreadQueue{};
            std::mutex MainThreadQueueMutex{};
            Core::Tasks::CounterEvent Done{};

            explicit ExecutionState(std::uint32_t taskCount)
                : RemainingDeps(taskCount),
                  Dispatched(taskCount),
                  Done()
            {
            }
        };
    }

    struct TaskGraph::Impl
    {
        QueueDomain Domain = QueueDomain::Cpu;

        struct PassNode
        {
            std::string Name;
            GraphExecuteCallback Execute;
            std::vector<ResourceAccess> Resources{};
            std::vector<std::uint32_t> Dependencies{};
            std::vector<std::uint32_t> WaitLabels{};
            std::vector<std::uint32_t> SignalLabels{};
            TaskGraphPassOptions Options{};
        };

        std::vector<PassNode> Passes{};

        // Compiled topology metadata.
        std::vector<std::uint32_t> ExecutionOrder{};
        std::vector<std::uint32_t> PassBatch{};
        std::vector<std::vector<std::uint32_t>> Layers{};
        std::vector<std::vector<std::uint32_t>> Successors{};
        std::vector<std::uint32_t> InitialInDegree{};
        bool Compiled = false;
        bool Executing = false;

        // TypeToken → ResourceId stable mapping (reset each epoch).
        std::unordered_map<std::size_t, std::uint32_t> TokenMap{};
        std::uint32_t NextResourceIdx = 0u;

        // StringID → ResourceId stable mapping (reset each epoch).
        std::unordered_map<std::uint32_t, std::uint32_t> StringResourceMap{};

        // Label map keeps signal labels separate from normal resources.
        std::unordered_map<std::uint64_t, std::uint32_t> LabelMap{};
        std::vector<std::uint32_t> LabelValues{};
        std::uint32_t NextLabelIdx = 0u;

        // Schedule stats and timings.
        uint64_t LastCompileNs = 0u;
        uint64_t LastExecuteNs = 0u;
        uint64_t LastCriticalPathNs = 0u;
        ScheduleStats LastStats{};

        // The compiled state should not be read/modified while Execute is active.
        // Keep the raw task payload alive for the life of execution.
        bool CanUsePlan() const noexcept { return Compiled && !ExecutionOrder.empty(); }
    };

    // -----------------------------------------------------------------------
    // TaskGraph public implementation
    // -----------------------------------------------------------------------
    TaskGraph::TaskGraph(QueueDomain domain)
        : m_Impl(std::make_unique<Impl>())
    {
        m_Impl->Domain = domain;
    }

    TaskGraph::~TaskGraph() = default;
    TaskGraph::TaskGraph(TaskGraph&&) noexcept = default;
    TaskGraph& TaskGraph::operator=(TaskGraph&&) noexcept = default;

    QueueDomain TaskGraph::Domain() const noexcept { return m_Impl->Domain; }

uint32_t TaskGraph::AddPassInternal(std::string_view name,
                                        const TaskGraphPassOptions& options,
                                        GraphExecuteCallback execute)
    {
        const auto idx = static_cast<std::uint32_t>(m_Impl->Passes.size());
        auto& pass = m_Impl->Passes.emplace_back();
        pass.Name = std::string(name);
        pass.Execute = std::move(execute);
        pass.Options = options;
        NormalizeOptions(pass.Options);
        m_Impl->Compiled = false;
        return idx;
    }

    void TaskGraph::NormalizeOptions(TaskGraphPassOptions& options) const
    {
        options.EstimatedCost = std::max<std::uint32_t>(1u, options.EstimatedCost);
    }

    ResourceId TaskGraph::TokenToResource(std::size_t token)
    {
        auto it = m_Impl->TokenMap.find(token);
        if (it != m_Impl->TokenMap.end())
            return ResourceId{it->second, 1u};

        const auto idx = m_Impl->NextResourceIdx++;
        m_Impl->TokenMap.emplace(token, idx);
        return ResourceId{idx, 1u};
    }

    ResourceId TaskGraph::StringIdToResource(Hash::StringID stringId)
    {
        const auto key = stringId.Value;
        auto it = m_Impl->StringResourceMap.find(key);
        if (it != m_Impl->StringResourceMap.end())
            return ResourceId{it->second, 1u};

        const auto idx = m_Impl->NextResourceIdx++;
        m_Impl->StringResourceMap.emplace(key, idx);
        return ResourceId{idx, 1u};
    }

    Core::Result TaskGraph::Compile()
    {
        if (m_Impl->Executing)
        {
            Log::Error("[TaskGraph] Compile() called while execution is active");
            return Err(ErrorCode::InvalidState);
        }

        const auto t0 = std::chrono::high_resolution_clock::now();
        const auto passCount = static_cast<std::uint32_t>(m_Impl->Passes.size());
        std::string diagnostic;

        auto compiled = CompileGraph(m_Impl->Passes, passCount, &diagnostic, m_Impl->LabelValues);
        if (!compiled.has_value())
        {
            m_Impl->Compiled = false;
            m_Impl->LastStats = ScheduleStats{};
            m_Impl->LastStats.lastDiagnostic = diagnostic;
            return Err(compiled.error());
        }

        auto graph = std::move(*compiled);
        m_Impl->ExecutionOrder = std::move(graph.ExecutionOrder);
        m_Impl->PassBatch     = std::move(graph.PassBatch);
        m_Impl->Layers        = std::move(graph.Layers);
        m_Impl->Successors    = std::move(graph.Successors);
        m_Impl->InitialInDegree= std::move(graph.InitialInDegree);
        m_Impl->LastStats     = graph.Stats;
        m_Impl->Compiled      = true;

        const auto t1 = std::chrono::high_resolution_clock::now();
        m_Impl->LastCompileNs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        m_Impl->LastCriticalPathNs = m_Impl->LastStats.criticalPathCost;
        return Ok();
    }

    std::uint32_t ResolveLane(const QueueDomain domain,
                              const uint32_t index,
                              [[maybe_unused]] const uint32_t order,
                              const uint32_t cpuBudget,
                              const uint32_t gpuBudget,
                              const uint32_t streamBudget)
    {
        switch (domain)
        {
        case QueueDomain::Cpu:
            return index % std::max<uint32_t>(1u, cpuBudget);
        case QueueDomain::Gpu:
            return index % std::max<uint32_t>(1u, gpuBudget);
        case QueueDomain::Streaming:
            return index % std::max<uint32_t>(1u, streamBudget);
        }
        return 0u;
    }

    Core::Expected<std::vector<PlanTask>> TaskGraph::BuildPlan(const BuildConfig& config)
    {
        if (!m_Impl->CanUsePlan())
        {
            if (auto r = Compile(); !r.has_value())
                return std::unexpected(r.error());
        }

        const auto passCount = m_Impl->ExecutionOrder.size();
        std::vector<PlanTask> plan{};
        plan.reserve(passCount);

        uint32_t topoOrder = 0u;
        uint32_t cpuLane = 0u;
        uint32_t gpuLane = 0u;
        uint32_t streamLane = 0u;
        const auto cpuBudget = std::max<uint32_t>(config.queueBudgetCpu, 1u);
        const auto gpuBudget = std::max<uint32_t>(config.queueBudgetGpu, 1u);
        const auto streamBudget = std::max<uint32_t>(config.queueBudgetStreaming, 1u);

        for (const auto passIndex : m_Impl->ExecutionOrder)
        {
            const auto lane = ResolveLane(
                m_Impl->Domain,
                (m_Impl->Domain == QueueDomain::Cpu ? cpuLane++ :
                 m_Impl->Domain == QueueDomain::Gpu ? gpuLane++ : streamLane++),
                topoOrder,
                cpuBudget,
                gpuBudget,
                streamBudget);

            plan.push_back(PlanTask{
                .id = TaskId{passIndex, 1u},
                .domain = m_Impl->Domain,
                .lane = lane,
                .topoOrder = topoOrder++,
                .batch = m_Impl->PassBatch[passIndex],
            });
        }
        return plan;
    }

    Core::Result TaskGraph::Execute()
    {
        if (m_Impl->Domain != QueueDomain::Cpu)
        {
            Log::Error("[TaskGraph] Execute() called on non-CPU domain graph '{}'",
                       static_cast<int>(m_Impl->Domain));
            return Err(ErrorCode::InvalidState);
        }

        if (!m_Impl->CanUsePlan())
        {
            if (auto compileResult = Compile(); !compileResult.has_value())
                return compileResult;
        }

        const bool canDispatch = Tasks::Scheduler::IsInitialized();
        const auto workerCount = static_cast<std::uint32_t>(Tasks::Scheduler::GetStats().WorkerLocalDepths.size());
        const bool canUseWorkers = canDispatch && workerCount > 1u;
        const auto t0 = std::chrono::high_resolution_clock::now();
        m_Impl->Executing = true;

        auto executeSequential = [&]()
        {
            for (const auto passIndex : m_Impl->ExecutionOrder)
                ExecutePass(passIndex);
        };

        if (!canUseWorkers || m_Impl->ExecutionOrder.size() <= 1u)
        {
            executeSequential();
        }
        else
        {
            ExecutionState state(static_cast<std::uint32_t>(m_Impl->Passes.size()));
            for (std::uint32_t i = 0; i < m_Impl->Passes.size(); ++i)
            {
                state.RemainingDeps[i].store(m_Impl->InitialInDegree[i], std::memory_order_release);
                state.Dispatched[i].store(0u, std::memory_order_release);
            }
            std::function<void(std::uint32_t)> onTaskFinished;
            std::function<void(std::uint32_t)> scheduleTask;

            onTaskFinished = [&](const std::uint32_t passIndex)
            {
                if (passIndex >= m_Impl->Passes.size())
                {
                    state.Done.Signal();
                    return;
                }

                const auto& successors = m_Impl->Successors[passIndex];
                for (const auto successor : successors)
                {
                    if (state.RemainingDeps[successor].fetch_sub(1u, std::memory_order_acq_rel) == 1u)
                        scheduleTask(successor);
                }

                state.Done.Signal();
            };

            scheduleTask = [&](const std::uint32_t passIndex)
            {
                if (passIndex >= m_Impl->Passes.size())
                    return;

                if (state.Dispatched[passIndex].exchange(1u, std::memory_order_acq_rel) == 1u)
                    return;

                state.Done.Add();

                const auto& options = m_Impl->Passes[passIndex].Options;
                const bool canRunOnWorker = options.AllowParallel && !options.MainThreadOnly && canUseWorkers;
                if (canRunOnWorker)
                {
                    Tasks::Scheduler::Dispatch([this, passIndex, &onTaskFinished]()
                    {
                        ExecutePass(passIndex);
                        onTaskFinished(passIndex);
                    });
                }
                else
                {
                    {
                        std::scoped_lock lock(state.MainThreadQueueMutex);
                        state.MainThreadQueue.push_back(passIndex);
                    }
                }
            };

            for (std::uint32_t i = 0; i < m_Impl->Passes.size(); ++i)
            {
                if (m_Impl->InitialInDegree[i] == 0u)
                    scheduleTask(i);
            }

            while (!state.Done.IsReady())
            {
                std::uint32_t passToRun = std::numeric_limits<std::uint32_t>::max();
                {
                    std::scoped_lock lock(state.MainThreadQueueMutex);
                    if (!state.MainThreadQueue.empty())
                    {
                        passToRun = state.MainThreadQueue.back();
                        state.MainThreadQueue.pop_back();
                    }
                }

                if (passToRun != std::numeric_limits<std::uint32_t>::max())
                {
                    ExecutePass(passToRun);
                    onTaskFinished(passToRun);
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        }

        const auto t1 = std::chrono::high_resolution_clock::now();
        m_Impl->LastExecuteNs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        m_Impl->Executing = false;
        return Ok();
    }

    void TaskGraph::ExecutePass(uint32_t passIndex)
    {
        if (!m_Impl->Compiled)
        {
            Log::Warn("[TaskGraph] ExecutePass requires a compiled graph");
            return;
        }

        if (passIndex >= static_cast<std::uint32_t>(m_Impl->Passes.size()))
        {
            Log::Warn("[TaskGraph] ExecutePass out-of-range index {}", passIndex);
            return;
        }

        if (!m_Impl->Passes[passIndex].Execute)
        {
            Log::Warn("[TaskGraph] Pass '{}' has no execute callback", m_Impl->Passes[passIndex].Name);
            return;
        }

        m_Impl->Passes[passIndex].Execute();
    }

GraphExecuteCallback TaskGraph::TakePassExecute(uint32_t passIndex)
    {
        if (!m_Impl->Compiled)
        {
            Log::Warn("[TaskGraph] TakePassExecute requires a compiled graph");
            return {};
        }

        if (passIndex >= static_cast<std::uint32_t>(m_Impl->Passes.size()))
            return {};

        auto closure = std::move(m_Impl->Passes[passIndex].Execute);
        m_Impl->Passes[passIndex].Execute = {};
        return closure;
    }

    void TaskGraph::Reset()
    {
        if (m_Impl->Executing)
        {
            assert(!m_Impl->Executing && "TaskGraph::Reset() called while Execute is active");
            return;
        }

        m_Impl->Passes.clear();
        m_Impl->ExecutionOrder.clear();
        m_Impl->PassBatch.clear();
        m_Impl->Layers.clear();
        m_Impl->Successors.clear();
        m_Impl->InitialInDegree.clear();
        m_Impl->Compiled = false;
        m_Impl->TokenMap.clear();
        m_Impl->StringResourceMap.clear();
        m_Impl->LabelMap.clear();
        m_Impl->LabelValues.clear();
        m_Impl->NextResourceIdx = 0u;
        m_Impl->NextLabelIdx = 0u;
        m_Impl->LastCompileNs = 0u;
        m_Impl->LastExecuteNs = 0u;
        m_Impl->LastStats = ScheduleStats{};
    }

    std::uint32_t TaskGraph::PassCount() const noexcept
    {
        return static_cast<std::uint32_t>(m_Impl->Passes.size());
    }

    std::string_view TaskGraph::PassName(std::uint32_t index) const noexcept
    {
        if (index < m_Impl->Passes.size())
            return m_Impl->Passes[index].Name;
        return {};
    }

    const std::vector<std::vector<std::uint32_t>>& TaskGraph::GetExecutionLayers() const noexcept
    {
        return m_Impl->Layers;
    }

    std::uint64_t TaskGraph::LastCompileTimeNs()      const noexcept { return m_Impl->LastCompileNs; }
    std::uint64_t TaskGraph::LastExecuteTimeNs()      const noexcept { return m_Impl->LastExecuteNs; }
    std::uint64_t TaskGraph::LastCriticalPathTimeNs() const noexcept { return m_Impl->LastCriticalPathNs; }

    ScheduleStats TaskGraph::GetScheduleStats() const noexcept
    {
        return m_Impl->LastStats;
    }

    std::unique_ptr<TaskGraph> CreateTaskGraph(QueueDomain domain)
    {
        return std::make_unique<TaskGraph>(domain);
    }

    // -----------------------------------------------------------------------
    // TaskGraphBuilder — implementation
    // -----------------------------------------------------------------------
    void TaskGraphBuilder::ReadResource(ResourceId resource)
    {
        m_Graph.m_Impl->Passes[m_PassIndex].Resources.push_back(
            ResourceAccess{resource, ResourceAccessMode::Read});
    }

    void TaskGraphBuilder::WriteResource(ResourceId resource)
    {
        m_Graph.m_Impl->Passes[m_PassIndex].Resources.push_back(
            ResourceAccess{resource, ResourceAccessMode::Write});
    }

    void TaskGraphBuilder::ReadResource(Hash::StringID label)
    {
        m_Graph.m_Impl->Passes[m_PassIndex].Resources.push_back(
            ResourceAccess{m_Graph.StringIdToResource(label), ResourceAccessMode::Read});
    }

    void TaskGraphBuilder::WriteResource(Hash::StringID label)
    {
        m_Graph.m_Impl->Passes[m_PassIndex].Resources.push_back(
            ResourceAccess{m_Graph.StringIdToResource(label), ResourceAccessMode::Write});
    }

    void TaskGraphBuilder::WaitFor(Hash::StringID label)
    {
        auto& lm = m_Graph.m_Impl->LabelMap;
        auto it = lm.find(label.Value);
        std::uint32_t idx = 0u;
        if (it != lm.end())
            idx = static_cast<std::uint32_t>(it->second);
        else
        {
            idx = m_Graph.m_Impl->NextLabelIdx++;
            lm.emplace(static_cast<std::uint64_t>(label.Value), idx);
            m_Graph.m_Impl->LabelValues.push_back(label.Value);
        }

        m_Graph.m_Impl->Passes[m_PassIndex].WaitLabels.push_back(
            idx);
    }

    void TaskGraphBuilder::Signal(Hash::StringID label)
    {
        auto& lm = m_Graph.m_Impl->LabelMap;
        auto it = lm.find(label.Value);
        std::uint32_t idx = 0u;
        if (it != lm.end())
            idx = static_cast<std::uint32_t>(it->second);
        else
        {
            idx = m_Graph.m_Impl->NextLabelIdx++;
            lm.emplace(static_cast<std::uint64_t>(label.Value), idx);
            m_Graph.m_Impl->LabelValues.push_back(label.Value);
        }

        m_Graph.m_Impl->Passes[m_PassIndex].SignalLabels.push_back(
            idx);
    }

    void TaskGraphBuilder::DependsOn(std::uint32_t predecessorPassIndex)
    {
        m_Graph.m_Impl->Passes[m_PassIndex].Dependencies.push_back(predecessorPassIndex);
    }
}
