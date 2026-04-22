module;

#include <cassert>
#include <chrono>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <functional>

module Extrinsic.Core.Dag.TaskGraph;

import Extrinsic.Core.Logging;

namespace Extrinsic::Core::Dag
{
    namespace
    {
        constexpr std::size_t kLabelTag = std::size_t{1} << (sizeof(std::size_t) * 8 - 1);

        [[nodiscard]] constexpr uint64_t PackResourceKey(ResourceId resource) noexcept
        {
            return (static_cast<uint64_t>(resource.Generation) << 32) | resource.Index;
        }
    }

    // -----------------------------------------------------------------------
    // Impl — internal state for one TaskGraph instance
    // -----------------------------------------------------------------------
    struct TaskGraph::Impl
    {
        QueueDomain Domain = QueueDomain::Cpu;

        struct PassNode
        {
            std::string Name;
            std::move_only_function<void()> Execute;
            std::vector<ResourceAccess> Resources;
            std::vector<ResourceId>     WaitLabels;
            std::vector<ResourceId>     SignalLabels;
        };

        std::vector<PassNode> Passes;

        // Compiled topo layers (set by Compile())
        std::vector<std::vector<uint32_t>> Layers;
        bool Compiled = false;

        // TypeToken → ResourceId stable mapping (reset each epoch)
        std::unordered_map<std::size_t, uint32_t> TokenMap;
        uint32_t NextResourceIdx = 0;

        // Label → pseudo-ResourceId (MSB-tagged)
        std::unordered_map<uint64_t, uint32_t> LabelMap;
        uint32_t NextLabelIdx = 0;

        // Stats
        uint64_t LastCompileNs     = 0;
        uint64_t LastExecuteNs     = 0;
        uint64_t LastCriticalPathNs = 0;

        // Simple topological sort via Kahn's algorithm.
        // Returns Err if a cycle is detected.
        Core::Result BuildLayers()
        {
            const uint32_t N = static_cast<uint32_t>(Passes.size());
            if (N == 0) return Core::Ok();

            // Build adjacency from resource hazards.
            // For each resource, we walk conflicting accesses in registration
            // order and emit a single forward edge per hazardous pair.
            std::vector<std::vector<uint32_t>> adj(N);
            std::vector<uint32_t> inDegree(N, 0);

            struct AccessEntry
            {
                uint32_t PassIndex = 0;
                ResourceAccessMode Mode = ResourceAccessMode::Read;
            };

            std::unordered_map<uint64_t, std::vector<AccessEntry>> accesses;

            for (uint32_t i = 0; i < N; ++i)
            {
                for (const auto& ra : Passes[i].Resources)
                {
                    accesses[PackResourceKey(ra.resource)].push_back(AccessEntry{
                        .PassIndex = i,
                        .Mode = ra.mode,
                    });
                }
            }

            auto AddEdge = [&](uint32_t from, uint32_t to)
            {
                if (from == to) return;
                adj[from].push_back(to);
                ++inDegree[to];
            };

            for (const auto& [resourceKey, entries] : accesses)
            {
                (void)resourceKey;
                for (std::size_t a = 0; a < entries.size(); ++a)
                {
                    for (std::size_t b = a + 1; b < entries.size(); ++b)
                    {
                        const bool aWrites = entries[a].Mode != ResourceAccessMode::Read;
                        const bool bWrites = entries[b].Mode != ResourceAccessMode::Read;
                        if (aWrites || bWrites)
                            AddEdge(entries[a].PassIndex, entries[b].PassIndex);
                    }
                }
            }

            // Label deps: WaitLabel pseudo-resource RAW edges
            for (uint32_t i = 0; i < N; ++i)
            {
                for (const ResourceId& waitLabel : Passes[i].WaitLabels)
                {
                    // Find all passes that signal this label
                    for (uint32_t j = 0; j < N; ++j)
                    {
                        for (const ResourceId& sig : Passes[j].SignalLabels)
                        {
                            if (sig.Index == waitLabel.Index)
                                AddEdge(j, i);
                        }
                    }
                }
            }

            // Kahn's topological sort → layers
            Layers.clear();
            std::vector<uint32_t> queue;
            queue.reserve(N);
            for (uint32_t i = 0; i < N; ++i)
                if (inDegree[i] == 0) queue.push_back(i);

            uint32_t processed = 0;
            while (!queue.empty())
            {
                Layers.push_back(std::move(queue));
                queue = {};
                for (uint32_t node : Layers.back())
                {
                    ++processed;
                    for (uint32_t next : adj[node])
                    {
                        if (--inDegree[next] == 0)
                            queue.push_back(next);
                    }
                }
            }

            if (processed != N)
                return std::unexpected(Core::ErrorCode::InvalidState); // cycle
            return Core::Ok();
        }
    };

    // -----------------------------------------------------------------------
    // TaskGraph — public implementation
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
                                        std::move_only_function<void()> execute)
    {
        const auto idx = static_cast<uint32_t>(m_Impl->Passes.size());
        auto& pass = m_Impl->Passes.emplace_back();
        pass.Name    = std::string(name);
        pass.Execute = std::move(execute);
        m_Impl->Compiled = false;
        return idx;
    }

    ResourceId TaskGraph::TokenToResource(std::size_t token)
    {
        auto it = m_Impl->TokenMap.find(token);
        if (it != m_Impl->TokenMap.end())
            return ResourceId{it->second, 1};
        const uint32_t idx = m_Impl->NextResourceIdx++;
        m_Impl->TokenMap.emplace(token, idx);
        return ResourceId{idx, 1};
    }

    Core::Result TaskGraph::Compile()
    {
        const auto t0 = std::chrono::high_resolution_clock::now();
        auto result = m_Impl->BuildLayers();
        const auto t1 = std::chrono::high_resolution_clock::now();
        m_Impl->LastCompileNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        if (result.has_value())
            m_Impl->Compiled = true;
        return result;
    }

    Core::Result TaskGraph::Execute()
    {
        if (m_Impl->Domain != QueueDomain::Cpu)
        {
            Log::Error("[TaskGraph] Execute() called on non-CPU domain graph '{}'",
                       static_cast<int>(m_Impl->Domain));
            return std::unexpected(Core::ErrorCode::InvalidState);
        }
        if (!m_Impl->Compiled)
        {
            auto r = Compile();
            if (!r) return r;
        }

        const auto t0 = std::chrono::high_resolution_clock::now();
        for (const auto& layer : m_Impl->Layers)
        {
            // TODO: dispatch layer in parallel via Tasks::Scheduler::Dispatch
            // when fiber integration is available. For now: sequential.
            for (const uint32_t idx : layer)
            {
                if (m_Impl->Passes[idx].Execute)
                    m_Impl->Passes[idx].Execute();
            }
        }
        const auto t1 = std::chrono::high_resolution_clock::now();
        m_Impl->LastExecuteNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        return Core::Ok();
    }

    Core::Expected<std::vector<PlanTask>> TaskGraph::BuildPlan(const BuildConfig& config)
    {
        if (!m_Impl->Compiled)
        {
            auto r = Compile();
            if (!r) return std::unexpected(r.error());
        }

        std::vector<PlanTask> plan;
        plan.reserve(m_Impl->Passes.size());
        uint32_t topoOrder = 0;
        for (uint32_t batch = 0; batch < static_cast<uint32_t>(m_Impl->Layers.size()); ++batch)
        {
            for (const uint32_t idx : m_Impl->Layers[batch])
            {
                plan.push_back(PlanTask{
                    .id        = TaskId{idx, 1},
                    .domain    = m_Impl->Domain,
                    .lane      = 0,
                    .topoOrder = topoOrder++,
                    .batch     = batch,
                });
            }
        }
        (void)config;
        return plan;
    }

    void TaskGraph::Reset()
    {
        m_Impl->Passes.clear();
        m_Impl->Layers.clear();
        m_Impl->Compiled = false;
        m_Impl->TokenMap.clear();
        m_Impl->LabelMap.clear();
        m_Impl->NextResourceIdx = 0;
        m_Impl->NextLabelIdx    = 0;
        m_Impl->LastCompileNs   = 0;
        m_Impl->LastExecuteNs   = 0;
    }

    uint32_t TaskGraph::PassCount() const noexcept
    {
        return static_cast<uint32_t>(m_Impl->Passes.size());
    }

    std::string_view TaskGraph::PassName(uint32_t index) const noexcept
    {
        if (index < m_Impl->Passes.size())
            return m_Impl->Passes[index].Name;
        return {};
    }

    uint64_t TaskGraph::LastCompileTimeNs()      const noexcept { return m_Impl->LastCompileNs; }
    uint64_t TaskGraph::LastExecuteTimeNs()      const noexcept { return m_Impl->LastExecuteNs; }
    uint64_t TaskGraph::LastCriticalPathTimeNs() const noexcept { return m_Impl->LastCriticalPathNs; }

    ScheduleStats TaskGraph::GetScheduleStats() const noexcept
    {
        ScheduleStats s{};
        s.taskCount = static_cast<uint32_t>(m_Impl->Passes.size());
        return s;
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

    void TaskGraphBuilder::WaitFor(Hash::StringID label)
    {
        auto& lm  = m_Graph.m_Impl->LabelMap;
        auto  it  = lm.find(label.Value);
        uint32_t idx;
        if (it != lm.end()) { idx = it->second; }
        else
        {
            idx = m_Graph.m_Impl->NextLabelIdx++;
            lm.emplace(label.Value, idx);
        }
        m_Graph.m_Impl->Passes[m_PassIndex].WaitLabels.push_back(
            ResourceId{static_cast<uint32_t>(idx | kLabelTag), 0});
    }

    void TaskGraphBuilder::Signal(Hash::StringID label)
    {
        auto& lm  = m_Graph.m_Impl->LabelMap;
        auto  it  = lm.find(label.Value);
        uint32_t idx;
        if (it != lm.end()) { idx = it->second; }
        else
        {
            idx = m_Graph.m_Impl->NextLabelIdx++;
            lm.emplace(label.Value, idx);
        }
        m_Graph.m_Impl->Passes[m_PassIndex].SignalLabels.push_back(
            ResourceId{static_cast<uint32_t>(idx | kLabelTag), 0});
    }
}

