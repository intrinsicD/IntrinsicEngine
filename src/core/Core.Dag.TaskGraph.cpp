module;

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iterator>
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
        [[nodiscard]] constexpr Tasks::DispatchPriority ToDispatchPriority(
            const TaskPriority priority) noexcept
        {
            switch (priority)
            {
            case TaskPriority::Critical:
            case TaskPriority::High:
                return Tasks::DispatchPriority::High;
            case TaskPriority::Low:
            case TaskPriority::Background:
                return Tasks::DispatchPriority::Low;
            case TaskPriority::Normal:
            default:
                return Tasks::DispatchPriority::Normal;
            }
        }

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

        enum class PassDependencyKind : uint8_t
        {
            Default = 0,
            Named,
        };

        struct PassDependency
        {
            std::uint32_t Predecessor = 0u;
            PassDependencyKind Kind = PassDependencyKind::Default;
            std::string Reason{};
        };

        enum class EdgeReason : uint8_t
        {
            ExplicitDependency = 0,
            HazardRaw,
            HazardWaw,
            HazardWar,
            LabelDependency,
            NamedDependency,
        };

        struct EdgeReasonInfo
        {
            EdgeReason Kind = EdgeReason::ExplicitDependency;
            std::string Reason{};
            std::uint32_t LabelValue = 0;
        };

        constexpr std::size_t kMaxCycleDiagnosticNodes = 32u;

        template <typename LabelFn>
        void AppendCycleDiagnostic(
            const std::vector<std::uint32_t>& cycle,
            const bool truncated,
            LabelFn&& taskLabel,
            const std::unordered_map<std::uint64_t, EdgeReasonInfo>& edgeReasons,
            std::string* outDiagnostic)
        {
            if (!outDiagnostic)
                return;

            std::ostringstream oss;
            oss << "Cycle detected: ";
            const std::size_t edgeCount = (cycle.size() > 1u) ? (cycle.size() - 1u) : 0u;
            for (std::size_t i = 0; i < edgeCount; ++i)
            {
                const auto from = cycle[i];
                const auto next = cycle[i + 1u];
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
                    case EdgeReason::NamedDependency:
                        if (!reason.Reason.empty())
                            oss << "reason(" << reason.Reason << ")";
                        else
                            oss << "named";
                        break;
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
                oss << taskLabel(cycle.back());
            }
            if (truncated)
            {
                oss << " ... (truncated)";
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
                               std::string_view namedReason,
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
                if (reason == EdgeReason::ExplicitDependency ||
                    reason == EdgeReason::LabelDependency ||
                    reason == EdgeReason::NamedDependency)
                    ++stats.explicitEdgeCount;
                else
                    ++stats.hazardEdgeCount;
                edgeReasons.emplace(
                    key,
                    EdgeReasonInfo{
                        .Kind = reason,
                        .Reason = std::string(namedReason),
                        .LabelValue = labelValue,
                    });
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
                for (const auto& dependency : pass.Dependencies)
                {
                    if (dependency.Predecessor >= nodeCount)
                    {
                        const auto msg = "Invalid explicit dependency in pass '" + std::string(passes[i].Name) +
                            "': predecessor index " + std::to_string(dependency.Predecessor) + " is out of range";
                        compiled.Stats.lastDiagnostic = msg;
                        if (outDiagnostic)
                            *outDiagnostic = msg;
                        return Err<CompiledPlanState>(ErrorCode::InvalidState);
                    }
                    const bool hasNamedReason = dependency.Kind != PassDependencyKind::Default;
                    addEdge(
                        dependency.Predecessor,
                        i,
                        hasNamedReason ? EdgeReason::NamedDependency : EdgeReason::ExplicitDependency,
                        dependency.Reason,
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
                            {},
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
                                {},
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
                        addEdge(signaler,
                                i,
                                EdgeReason::LabelDependency,
                                {},
                                labelValue,
                                seenEdges,
                                successors,
                                predecessors,
                                inDegree,
                                edgeReasons,
                                compiled.Stats);
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
                    bool cycleTruncated = false;

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
                                    const auto cycleStart = static_cast<std::size_t>(std::distance(stack.begin(), it));
                                    const auto stackNodeCount = stack.size() - cycleStart;
                                    const auto cycleNodeCount = stackNodeCount + 1u;
                                    if (cycleNodeCount <= kMaxCycleDiagnosticNodes)
                                    {
                                        cycle.reserve(cycleNodeCount);
                                        for (std::size_t idx = cycleStart; idx < stack.size(); ++idx)
                                            cycle.push_back(stack[idx]);
                                        cycle.push_back(next);
                                    }
                                    else
                                    {
                                        cycle.reserve(kMaxCycleDiagnosticNodes);
                                        const auto emitCount = std::min<std::size_t>(stackNodeCount, kMaxCycleDiagnosticNodes);
                                        for (std::size_t idx = 0; idx < emitCount; ++idx)
                                            cycle.push_back(stack[cycleStart + idx]);
                                        cycleTruncated = true;
                                    }
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
                        auto labelFor = [&](const std::uint32_t index) -> std::string
                        {
                            if (index < passes.size() && !passes[index].Name.empty())
                                return std::string(passes[index].Name);
                            return std::to_string(index);
                        };
                        AppendCycleDiagnostic(cycle, cycleTruncated, labelFor, edgeReasons, &compiled.Stats.lastDiagnostic);
                    }
                    else
                    {
                        constexpr std::size_t kMaxUnresolvedNodes = 16u;
                        std::vector<bool> inTopo(nodeCount, false);
                        for (const auto node : topo)
                            inTopo[node] = true;

                        std::size_t unresolvedCount = 0;
                        for (std::uint32_t i = 0; i < nodeCount; ++i)
                        {
                            if (!inTopo[i])
                                ++unresolvedCount;
                        }

                        std::ostringstream unresolved;
                        unresolved << "Cycle detected in task graph. Unresolved nodes: ";
                        std::size_t emitted = 0;
                        for (std::uint32_t i = 0; i < nodeCount && emitted < kMaxUnresolvedNodes; ++i)
                        {
                            if (inTopo[i])
                                continue;

                            if (emitted > 0u)
                                unresolved << ", ";
                            if (!passes[i].Name.empty())
                                unresolved << passes[i].Name;
                            else
                                unresolved << i;
                            ++emitted;
                        }
                        if (unresolvedCount > emitted)
                            unresolved << " ... (" << (unresolvedCount - emitted) << " more)";
                        compiled.Stats.lastDiagnostic = unresolved.str();
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
            struct MainThreadReadyEntry
            {
                std::uint8_t Priority = 0u;
                std::uint32_t EstimatedCost = 1u;
                std::uint32_t InsertionOrder = 0u;
                std::uint32_t PassIndex = 0u;
            };

            struct MainThreadReadyCompare
            {
                [[nodiscard]] bool operator()(const MainThreadReadyEntry& a,
                                              const MainThreadReadyEntry& b) const noexcept
                {
                    if (a.Priority != b.Priority)
                        return a.Priority > b.Priority;
                    if (a.EstimatedCost != b.EstimatedCost)
                        return a.EstimatedCost < b.EstimatedCost;
                    return a.InsertionOrder > b.InsertionOrder;
                }
            };

            std::priority_queue<MainThreadReadyEntry,
                                std::vector<MainThreadReadyEntry>,
                                MainThreadReadyCompare> MainThreadQueue{};
            std::mutex MainThreadQueueMutex{};
            std::atomic<std::uint32_t> NextInsertionOrder{0u};
            std::atomic<std::uint32_t> RemainingTasks{0u};
            Core::Tasks::CounterEvent Done;
            std::thread::id OwnerThread{};
            std::uint64_t SchedulerInstance = 0u;
            std::chrono::steady_clock::time_point StartedAt{};
            std::function<void(const std::shared_ptr<ExecutionState>&, std::uint32_t)> OnTaskFinished{};
            std::function<void(const std::shared_ptr<ExecutionState>&,
                               const std::vector<std::uint32_t>&)> ScheduleReadyBatch{};
            std::function<void(const std::shared_ptr<ExecutionState>&, std::uint32_t)> ExecuteAndFinish{};

            ExecutionState(std::uint32_t taskCount, std::uint64_t schedulerInstance)
                : RemainingDeps(taskCount),
                  Dispatched(taskCount),
                  RemainingTasks(taskCount),
                  Done(taskCount),
                  OwnerThread(std::this_thread::get_id()),
                  SchedulerInstance(schedulerInstance),
                  StartedAt(std::chrono::steady_clock::now())
            {
            }
        };
    }

    struct TaskGraphCompletion::Impl
    {
        std::shared_ptr<ExecutionState> State{};

        explicit Impl(std::shared_ptr<ExecutionState> state) noexcept
            : State(std::move(state))
        {
        }
    };

    struct TaskGraph::Impl
    {
        TaskGraphExecutionMode Mode = TaskGraphExecutionMode::ExecuteCallbacks;

        struct PassNode
        {
            std::string Name;
            GraphExecuteCallback Execute;
            std::vector<ResourceAccess> Resources{};
            std::vector<PassDependency> Dependencies{};
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
        std::atomic<bool> Executing{false};
        mutable std::mutex ActiveExecutionMutex{};
        std::weak_ptr<ExecutionState> ActiveExecution{};

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
        std::atomic<uint64_t> LastExecuteNs{0u};
        uint64_t LastCriticalPathNs = 0u;
        ScheduleStats LastStats{};

        // The compiled state should not be read/modified while Execute is active.
        // Keep the raw task payload alive for the life of execution.
        bool CanUsePlan() const noexcept { return Compiled && !ExecutionOrder.empty(); }

        bool HasLiveExecution() const
        {
            std::scoped_lock lock(ActiveExecutionMutex);
            const auto active = ActiveExecution.lock();
            return active && !active->Done.IsReady();
        }

        void TrackExecution(const std::shared_ptr<ExecutionState>& state)
        {
            std::scoped_lock lock(ActiveExecutionMutex);
            ActiveExecution = state;
        }

        void ClearCompiledState() noexcept
        {
            ExecutionOrder.clear();
            PassBatch.clear();
            Layers.clear();
            Successors.clear();
            InitialInDegree.clear();
            Compiled = false;
        }
    };

    // -----------------------------------------------------------------------
    // TaskGraphCompletion public implementation
    // -----------------------------------------------------------------------
    TaskGraphCompletion::TaskGraphCompletion() noexcept = default;
    TaskGraphCompletion::~TaskGraphCompletion() = default;
    TaskGraphCompletion::TaskGraphCompletion(const TaskGraphCompletion&) noexcept = default;
    TaskGraphCompletion& TaskGraphCompletion::operator=(const TaskGraphCompletion&) noexcept = default;
    TaskGraphCompletion::TaskGraphCompletion(TaskGraphCompletion&&) noexcept = default;
    TaskGraphCompletion& TaskGraphCompletion::operator=(TaskGraphCompletion&&) noexcept = default;

    TaskGraphCompletion::TaskGraphCompletion(std::shared_ptr<Impl> impl) noexcept
        : m_Impl(std::move(impl))
    {
    }

    bool TaskGraphCompletion::IsValid() const noexcept
    {
        return m_Impl && m_Impl->State;
    }

    bool TaskGraphCompletion::IsReady() const noexcept
    {
        return IsValid() && m_Impl->State->Done.IsReady();
    }

    Core::Expected<std::uint32_t> TaskGraphCompletion::PumpMainThreadPasses()
    {
        if (!IsValid())
            return Err<std::uint32_t>(ErrorCode::InvalidState);

        const auto state = m_Impl->State;
        if (std::this_thread::get_id() != state->OwnerThread)
            return Err<std::uint32_t>(ErrorCode::ThreadViolation);
        if (!state->Done.IsReady() && state->SchedulerInstance != 0u &&
            state->SchedulerInstance != Tasks::Scheduler::CurrentInstanceId())
        {
            return Err<std::uint32_t>(ErrorCode::InvalidState);
        }

        std::uint32_t executed = 0u;
        for (;;)
        {
            std::uint32_t passToRun = std::numeric_limits<std::uint32_t>::max();
            {
                std::scoped_lock lock(state->MainThreadQueueMutex);
                if (!state->MainThreadQueue.empty())
                {
                    passToRun = state->MainThreadQueue.top().PassIndex;
                    state->MainThreadQueue.pop();
                }
            }

            if (passToRun == std::numeric_limits<std::uint32_t>::max())
                break;

            state->ExecuteAndFinish(state, passToRun);
            ++executed;
        }

        return executed;
    }

    Core::Result TaskGraphCompletion::Wait()
    {
        if (!IsValid())
            return Err(ErrorCode::InvalidState);

        const auto state = m_Impl->State;
        if (std::this_thread::get_id() != state->OwnerThread)
            return Err(ErrorCode::ThreadViolation);

        while (!state->Done.IsReady())
        {
            if (state->SchedulerInstance != 0u &&
                state->SchedulerInstance != Tasks::Scheduler::CurrentInstanceId())
            {
                return Err(ErrorCode::InvalidState);
            }

            const auto schedulerProgress =
                state->SchedulerInstance != 0u
                    ? Tasks::Scheduler::ObserveWorkProgress()
                    : Tasks::Scheduler::WorkProgressToken{};
            if (state->SchedulerInstance != 0u &&
                schedulerProgress.SchedulerInstance != state->SchedulerInstance)
            {
                return Err(ErrorCode::InvalidState);
            }

            const auto observedPending = state->Done.PendingCount();
            if (observedPending == 0u)
                break;

            auto pumped = PumpMainThreadPasses();
            if (!pumped.has_value())
                return Err(pumped.error());
            if (*pumped != 0u)
                continue;

            if (Tasks::Scheduler::TryRunOne())
                continue;

            if (state->SchedulerInstance != 0u)
            {
                if (!Tasks::Scheduler::WaitForWorkProgress(schedulerProgress))
                    return Err(ErrorCode::InvalidState);
            }
            else
            {
                state->Done.WaitForProgress(observedPending);
            }
        }

        return Ok();
    }

    // -----------------------------------------------------------------------
    // TaskGraph public implementation
    // -----------------------------------------------------------------------
    TaskGraph::TaskGraph(const TaskGraphExecutionMode mode)
        : m_Impl(std::make_shared<Impl>())
    {
        m_Impl->Mode = mode;
    }

    TaskGraph::~TaskGraph() = default;
    TaskGraph::TaskGraph(TaskGraph&&) noexcept = default;
    TaskGraph& TaskGraph::operator=(TaskGraph&&) noexcept = default;

    uint32_t TaskGraph::AddPassInternal(std::string_view name,
                                        const TaskGraphPassOptions& options,
                                        GraphExecuteCallback execute)
    {
        if (m_Impl->Executing.load(std::memory_order_acquire) || m_Impl->HasLiveExecution())
        {
            Log::Error("[TaskGraph] AddPass() called while execution is active");
            return std::numeric_limits<std::uint32_t>::max();
        }

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
        if (m_Impl->Executing.load(std::memory_order_acquire) || m_Impl->HasLiveExecution())
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
            m_Impl->ClearCompiledState();
            m_Impl->LastStats = ScheduleStats{};
            m_Impl->LastStats.lastDiagnostic = diagnostic;
            m_Impl->LastCriticalPathNs = 0u;
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

    Core::Expected<std::vector<PlanTask>> TaskGraph::BuildPlan()
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
        for (const auto passIndex : m_Impl->ExecutionOrder)
        {
            plan.push_back(PlanTask{
                .id = TaskId{passIndex, 1u},
                .topoOrder = topoOrder++,
                .batch = m_Impl->PassBatch[passIndex],
            });
        }
        return plan;
    }

    Core::Expected<TaskGraphCompletion> TaskGraph::Submit()
    {
        if (m_Impl->Mode != TaskGraphExecutionMode::ExecuteCallbacks)
        {
            Log::Error("[TaskGraph] Submit() called on a plan-only graph");
            return Err<TaskGraphCompletion>(ErrorCode::InvalidState);
        }

        if (m_Impl->Executing.load(std::memory_order_acquire) || m_Impl->HasLiveExecution())
        {
            Log::Error("[TaskGraph] Submit() called while execution is active");
            return Err<TaskGraphCompletion>(ErrorCode::InvalidState);
        }

        if (!m_Impl->CanUsePlan())
        {
            if (auto compileResult = Compile(); !compileResult.has_value())
                return Err<TaskGraphCompletion>(compileResult.error());
        }

        bool expectedIdle = false;
        if (!m_Impl->Executing.compare_exchange_strong(expectedIdle, true,
                                                       std::memory_order_acq_rel,
                                                       std::memory_order_acquire))
        {
            return Err<TaskGraphCompletion>(ErrorCode::InvalidState);
        }

        const auto impl = m_Impl;
        const auto schedulerInstance = Tasks::Scheduler::CurrentInstanceId();
        const bool hasWorkerEligiblePass = std::ranges::any_of(
            impl->Passes,
            [](const Impl::PassNode& pass)
            {
                return pass.Options.AllowParallel && !pass.Options.MainThreadOnly;
            });
        const bool canUseWorkers = schedulerInstance != 0u && hasWorkerEligiblePass;
        auto state = std::make_shared<ExecutionState>(
            static_cast<std::uint32_t>(impl->Passes.size()),
            canUseWorkers ? schedulerInstance : 0u);
        impl->TrackExecution(state);
        for (std::uint32_t i = 0; i < impl->Passes.size(); ++i)
        {
            state->RemainingDeps[i].store(impl->InitialInDegree[i], std::memory_order_release);
            state->Dispatched[i].store(0u, std::memory_order_release);
        }

        state->ScheduleReadyBatch = [impl, canUseWorkers](const std::shared_ptr<ExecutionState>& state,
                                                          const std::vector<std::uint32_t>& passIndices)
        {
            std::vector<std::uint32_t> workerPasses{};
            std::vector<ExecutionState::MainThreadReadyEntry> mainThreadPasses{};
            workerPasses.reserve(passIndices.size());
            mainThreadPasses.reserve(passIndices.size());

            for (const auto passIndex : passIndices)
            {
                if (passIndex >= impl->Passes.size())
                    continue;

                if (state->Dispatched[passIndex].exchange(1u, std::memory_order_acq_rel) == 1u)
                    continue;

                const auto& options = impl->Passes[passIndex].Options;
                const bool canRunOnWorker = options.AllowParallel &&
                    !options.MainThreadOnly && canUseWorkers;
                if (canRunOnWorker)
                {
                    workerPasses.push_back(passIndex);
                }
                else
                {
                    mainThreadPasses.push_back(ExecutionState::MainThreadReadyEntry{
                        .Priority = static_cast<std::uint8_t>(options.Priority),
                        .EstimatedCost = options.EstimatedCost,
                        .InsertionOrder = state->NextInsertionOrder.fetch_add(1u, std::memory_order_relaxed),
                        .PassIndex = passIndex,
                    });
                }
            }

            if (!mainThreadPasses.empty())
            {
                std::scoped_lock lock(state->MainThreadQueueMutex);
                for (const auto& entry : mainThreadPasses)
                    state->MainThreadQueue.push(entry);
            }

            for (const auto passIndex : workerPasses)
            {
                const auto priority =
                    ToDispatchPriority(impl->Passes[passIndex].Options.Priority);
                Tasks::Scheduler::Dispatch(
                    priority,
                    [passIndex, state]()
                    {
                        state->ExecuteAndFinish(state, passIndex);
                    });
            }
        };

        state->OnTaskFinished = [impl](const std::shared_ptr<ExecutionState>& state,
                                       const std::uint32_t passIndex)
        {
            if (passIndex < impl->Passes.size())
            {
                std::vector<std::uint32_t> readySuccessors{};
                readySuccessors.reserve(impl->Successors[passIndex].size());
                for (const auto successor : impl->Successors[passIndex])
                {
                    if (state->RemainingDeps[successor].fetch_sub(1u, std::memory_order_acq_rel) == 1u)
                        readySuccessors.push_back(successor);
                }

                state->ScheduleReadyBatch(state, readySuccessors);
            }

            const auto remaining = state->RemainingTasks.fetch_sub(1u, std::memory_order_acq_rel);
            if (remaining == 1u)
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - state->StartedAt).count();
                impl->LastExecuteNs.store(static_cast<std::uint64_t>(elapsed), std::memory_order_release);
                impl->Executing.store(false, std::memory_order_release);
            }

            state->Done.Signal();
        };

        state->ExecuteAndFinish = [impl](const std::shared_ptr<ExecutionState>& state,
                                         const std::uint32_t passIndex)
        {
            if (passIndex < impl->Passes.size() && impl->Passes[passIndex].Execute)
            {
                impl->Passes[passIndex].Execute();
            }
            else
            {
                Log::Warn("[TaskGraph] Submitted pass {} has no execute callback", passIndex);
            }

            state->OnTaskFinished(state, passIndex);
        };

        if (impl->Passes.empty())
        {
            impl->LastExecuteNs.store(0u, std::memory_order_release);
            impl->Executing.store(false, std::memory_order_release);
        }
        else
        {
            std::vector<std::uint32_t> initialReady{};
            initialReady.reserve(impl->Passes.size());
            for (std::uint32_t i = 0; i < impl->Passes.size(); ++i)
            {
                if (impl->InitialInDegree[i] == 0u)
                    initialReady.push_back(i);
            }
            state->ScheduleReadyBatch(state, initialReady);
        }

        return TaskGraphCompletion(std::make_shared<TaskGraphCompletion::Impl>(std::move(state)));
    }

    Core::Result TaskGraph::Execute()
    {
        auto completion = Submit();
        if (!completion.has_value())
            return Err(completion.error());
        return completion->Wait();
    }

    void TaskGraph::ExecutePass(uint32_t passIndex)
    {
        if (m_Impl->Executing.load(std::memory_order_acquire) || m_Impl->HasLiveExecution())
        {
            Log::Warn("[TaskGraph] ExecutePass cannot run during a submitted execution");
            return;
        }

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
        if (m_Impl->Executing.load(std::memory_order_acquire) || m_Impl->HasLiveExecution())
        {
            Log::Warn("[TaskGraph] TakePassExecute cannot run during a submitted execution");
            return {};
        }

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

    Core::Result TaskGraph::Reset()
    {
        if (m_Impl->Executing.load(std::memory_order_acquire) || m_Impl->HasLiveExecution())
        {
            Log::Error("[TaskGraph] Reset() called while execution is active");
            return Err(ErrorCode::InvalidState);
        }

        m_Impl->Passes.clear();
        m_Impl->ClearCompiledState();
        m_Impl->TokenMap.clear();
        m_Impl->StringResourceMap.clear();
        m_Impl->LabelMap.clear();
        m_Impl->LabelValues.clear();
        m_Impl->NextResourceIdx = 0u;
        m_Impl->NextLabelIdx = 0u;
        m_Impl->LastCompileNs = 0u;
        m_Impl->LastExecuteNs.store(0u, std::memory_order_release);
        m_Impl->LastCriticalPathNs = 0u;
        m_Impl->LastStats = ScheduleStats{};
        return Ok();
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
    std::uint64_t TaskGraph::LastExecuteTimeNs()      const noexcept
    {
        return m_Impl->LastExecuteNs.load(std::memory_order_acquire);
    }
    std::uint64_t TaskGraph::LastCriticalPathTimeNs() const noexcept { return m_Impl->LastCriticalPathNs; }

    ScheduleStats TaskGraph::GetScheduleStats() const noexcept
    {
        return m_Impl->LastStats;
    }

    std::unique_ptr<TaskGraph> CreateTaskGraph(const TaskGraphExecutionMode mode)
    {
        return std::make_unique<TaskGraph>(mode);
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
        m_Graph.m_Impl->Passes[m_PassIndex].Dependencies.push_back(
            PassDependency{
                .Predecessor = predecessorPassIndex,
                .Kind = PassDependencyKind::Default,
            });
    }

    void TaskGraphBuilder::DependsOn(std::uint32_t predecessorPassIndex, std::string_view reason)
    {
        m_Graph.m_Impl->Passes[m_PassIndex].Dependencies.push_back(
            PassDependency{
                .Predecessor = predecessorPassIndex,
                .Kind = PassDependencyKind::Named,
                .Reason = std::string(reason),
            });
    }
}
