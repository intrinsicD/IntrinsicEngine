// Implements dense DAG mutation, adjacency queries, deterministic compilation,
// and concrete cycle diagnostics for the property-backed Core.Dag surface.
module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <span>
#include <utility>
#include <vector>

module Core.Dag;

namespace Extrinsic::Core
{
    namespace
    {
        [[nodiscard]] DagDiagnostic MakeDiagnostic(
            const DagErrorCode code, const NodeHandle node = {})
        {
            return DagDiagnostic{
                .Code = code,
                .Node = node,
            };
        }

        [[nodiscard]] constexpr std::size_t MaximumElementCount() noexcept
        {
            return static_cast<std::size_t>(
                std::numeric_limits<PropertyIndex>::max());
        }

        template <typename T>
        void ReserveForAppend(std::vector<T>& values)
        {
            if (values.size() < values.capacity())
                return;

            const std::size_t maximum = values.max_size();
            const std::size_t capacity = values.capacity();
            const std::size_t doubled =
                capacity > maximum / 2u ? maximum : capacity * 2u;
            values.reserve(std::max(values.size() + 1u, doubled));
        }
    } // namespace

    DagResult<NodeHandle> Dag::AddNode()
    {
        const std::size_t index = NodeCount();
        if (index >= MaximumElementCount())
        {
            return std::unexpected(
                MakeDiagnostic(DagErrorCode::CapacityExceeded));
        }

        // Pre-reserved capacity lets aligned adjacency slots publish without
        // allocation after property mutation.
        ReserveForAppend(m_IncomingEdges);
        ReserveForAppend(m_OutgoingEdges);

        m_Nodes.PushBack();
        m_IncomingEdges.emplace_back();
        m_OutgoingEdges.emplace_back();
        ++m_TopologyRevision;

        return NodeHandle{
            static_cast<PropertyIndex>(index),
            m_HandleEpoch,
        };
    }

    DagResult<EdgeHandle> Dag::AddEdge(
        const NodeHandle before, const NodeHandle after)
    {
        if (!IsValid(before))
        {
            return std::unexpected(
                MakeDiagnostic(DagErrorCode::InvalidNode, before));
        }
        if (!IsValid(after))
        {
            return std::unexpected(
                MakeDiagnostic(DagErrorCode::InvalidNode, after));
        }
        if (before == after)
        {
            return std::unexpected(
                MakeDiagnostic(DagErrorCode::SelfEdge, before));
        }

        const std::size_t index = EdgeCount();
        if (index >= MaximumElementCount())
        {
            return std::unexpected(
                MakeDiagnostic(DagErrorCode::CapacityExceeded));
        }

        std::vector<EdgeHandle>& outgoing = m_OutgoingEdges[before.Index];
        std::vector<EdgeHandle>& incoming = m_IncomingEdges[after.Index];

        ReserveForAppend(m_Endpoints);
        ReserveForAppend(outgoing);
        ReserveForAppend(incoming);

        const EdgeHandle edge{
            static_cast<PropertyIndex>(index),
            m_HandleEpoch,
        };
        m_Edges.PushBack();
        m_Endpoints.push_back(EdgeEndpoints{
            .Before = before,
            .After = after,
        });
        outgoing.push_back(edge);
        incoming.push_back(edge);
        ++m_TopologyRevision;

        return edge;
    }

    void Dag::ReserveNodes(const std::size_t count)
    {
        m_Nodes.ReserveElements(count);
        m_IncomingEdges.reserve(count);
        m_OutgoingEdges.reserve(count);
    }

    void Dag::ReserveEdges(const std::size_t count)
    {
        m_Edges.ReserveElements(count);
        m_Endpoints.reserve(count);
    }

    void Dag::Clear()
    {
        const bool topologyChanged = NodeCount() != 0u || EdgeCount() != 0u;

        m_Nodes.Clear();
        m_Edges.Clear();
        m_Endpoints.clear();
        m_IncomingEdges.clear();
        m_OutgoingEdges.clear();

        if (topologyChanged)
        {
            ++m_HandleEpoch;
            if (m_HandleEpoch == 0u)
                ++m_HandleEpoch;
            ++m_TopologyRevision;
        }
    }

    std::size_t Dag::NodeCount() const noexcept
    {
        return m_Nodes.Size();
    }

    std::size_t Dag::EdgeCount() const noexcept
    {
        return m_Edges.Size();
    }

    DagRevision Dag::TopologyRevision() const noexcept
    {
        return m_TopologyRevision;
    }

    bool Dag::IsValid(const NodeHandle node) const noexcept
    {
        return node.Epoch == m_HandleEpoch &&
            static_cast<std::size_t>(node.Index) < NodeCount();
    }

    bool Dag::IsValid(const EdgeHandle edge) const noexcept
    {
        return edge.Epoch == m_HandleEpoch &&
            static_cast<std::size_t>(edge.Index) < EdgeCount();
    }

    std::optional<EdgeEndpoints> Dag::Endpoints(
        const EdgeHandle edge) const noexcept
    {
        if (!IsValid(edge))
            return std::nullopt;

        return m_Endpoints[edge.Index];
    }

    std::optional<std::span<const EdgeHandle>> Dag::IncomingEdges(
        const NodeHandle node) const noexcept
    {
        if (!IsValid(node))
            return std::nullopt;

        return std::span<const EdgeHandle>{m_IncomingEdges[node.Index]};
    }

    std::optional<std::span<const EdgeHandle>> Dag::OutgoingEdges(
        const NodeHandle node) const noexcept
    {
        if (!IsValid(node))
            return std::nullopt;

        return std::span<const EdgeHandle>{m_OutgoingEdges[node.Index]};
    }

    DagResult<CompiledDag> Dag::Compile() const
    {
        const std::size_t nodeCount = NodeCount();
        std::vector<std::uint32_t> remainingPredecessors(nodeCount);
        for (std::size_t index = 0u; index < nodeCount; ++index)
        {
            remainingPredecessors[index] = static_cast<std::uint32_t>(
                m_IncomingEdges[index].size());
        }

        std::priority_queue<
            PropertyIndex,
            std::vector<PropertyIndex>,
            std::greater<PropertyIndex>> ready;
        for (PropertyIndex index = 0u;
             static_cast<std::size_t>(index) < nodeCount;
             ++index)
        {
            if (remainingPredecessors[index] == 0u)
                ready.push(index);
        }

        CompiledDag compiled{
            .SourceRevision = m_TopologyRevision,
            .LayerByNode = std::vector<std::uint32_t>(nodeCount),
        };
        compiled.TopologicalOrder.reserve(nodeCount);

        while (!ready.empty())
        {
            const PropertyIndex index = ready.top();
            ready.pop();
            compiled.TopologicalOrder.push_back(NodeHandle{index, m_HandleEpoch});

            for (const EdgeHandle edge : m_OutgoingEdges[index])
            {
                const PropertyIndex successor = m_Endpoints[edge.Index].After.Index;
                compiled.LayerByNode[successor] = std::max(
                    compiled.LayerByNode[successor],
                    compiled.LayerByNode[index] + 1u);

                std::uint32_t& predecessorCount =
                    remainingPredecessors[successor];
                --predecessorCount;
                if (predecessorCount == 0u)
                    ready.push(successor);
            }
        }

        if (compiled.TopologicalOrder.size() == nodeCount)
        {
            if (!compiled.LayerByNode.empty())
            {
                compiled.LayerCount =
                    *std::max_element(
                        compiled.LayerByNode.begin(),
                        compiled.LayerByNode.end()) +
                    1u;
            }
            return compiled;
        }

        enum class VisitState : std::uint8_t
        {
            Unvisited,
            Active,
            Complete,
        };

        struct SearchFrame
        {
            PropertyIndex Node{};
            EdgeHandle IncomingEdge{};
            std::size_t NextEdge{};
        };

        std::vector<VisitState> visitStates(nodeCount, VisitState::Unvisited);
        std::vector<SearchFrame> search;
        search.reserve(nodeCount);

        for (PropertyIndex start = 0u;
             static_cast<std::size_t>(start) < nodeCount;
             ++start)
        {
            if (remainingPredecessors[start] == 0u ||
                visitStates[start] != VisitState::Unvisited)
            {
                continue;
            }

            visitStates[start] = VisitState::Active;
            search.push_back(SearchFrame{.Node = start});

            while (!search.empty())
            {
                SearchFrame& frame = search.back();
                const std::vector<EdgeHandle>& outgoing =
                    m_OutgoingEdges[frame.Node];

                if (frame.NextEdge == outgoing.size())
                {
                    visitStates[frame.Node] = VisitState::Complete;
                    search.pop_back();
                    continue;
                }

                const EdgeHandle edge = outgoing[frame.NextEdge++];
                const PropertyIndex successor =
                    m_Endpoints[edge.Index].After.Index;
                if (remainingPredecessors[successor] == 0u)
                    continue;

                if (visitStates[successor] == VisitState::Unvisited)
                {
                    visitStates[successor] = VisitState::Active;
                    search.push_back(SearchFrame{
                        .Node = successor,
                        .IncomingEdge = edge,
                    });
                    continue;
                }

                if (visitStates[successor] != VisitState::Active)
                    continue;

                const auto cycleBegin = std::find_if(
                    search.begin(), search.end(),
                    [successor](const SearchFrame& candidate)
                    {
                        return candidate.Node == successor;
                    });

                DagDiagnostic diagnostic = MakeDiagnostic(
                    DagErrorCode::CycleDetected,
                    NodeHandle{successor, m_HandleEpoch});
                const std::size_t cycleEdgeCount =
                    static_cast<std::size_t>(search.end() - cycleBegin);
                diagnostic.Cycle.reserve(cycleEdgeCount + 1u);
                diagnostic.CycleEdges.reserve(cycleEdgeCount);
                for (auto current = cycleBegin; current != search.end(); ++current)
                {
                    diagnostic.Cycle.push_back(NodeHandle{
                        current->Node,
                        m_HandleEpoch,
                    });
                }
                for (auto current = cycleBegin + 1; current != search.end(); ++current)
                    diagnostic.CycleEdges.push_back(current->IncomingEdge);
                diagnostic.Cycle.push_back(NodeHandle{successor, m_HandleEpoch});
                diagnostic.CycleEdges.push_back(edge);
                return std::unexpected(std::move(diagnostic));
            }
        }

        const auto unresolved = std::find_if(
            remainingPredecessors.begin(), remainingPredecessors.end(),
            [](const std::uint32_t count)
            {
                return count != 0u;
            });
        const PropertyIndex node = static_cast<PropertyIndex>(
            unresolved - remainingPredecessors.begin());
        return std::unexpected(MakeDiagnostic(
            DagErrorCode::CycleDetected,
            NodeHandle{node, m_HandleEpoch}));
    }
}
