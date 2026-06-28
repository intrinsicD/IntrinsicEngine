module;

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry.Graph.ShortestPath;

import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Extrinsic.Core.IndexedHeap;

namespace Geometry::ShortestPath
{
    namespace
    {
        template <class Domain>
        concept GraphLike = requires(Domain& d, const Domain& cd, VertexHandle v, HalfedgeHandle h)
        {
            { cd.VerticesSize() } -> std::convertible_to<std::size_t>;
            { cd.HalfedgesSize() } -> std::convertible_to<std::size_t>;
            { cd.IsValid(v) } -> std::same_as<bool>;
            { cd.IsDeleted(v) } -> std::same_as<bool>;
            { cd.IsIsolated(v) } -> std::same_as<bool>;
            d.HalfedgesAroundVertex(v);
            { cd.ToVertex(h) } -> std::same_as<VertexHandle>;
            d.VertexProperties();
        };

        [[nodiscard]] glm::vec3 VertexPosition(const HalfedgeMesh::Mesh& mesh, VertexHandle v)
        {
            return mesh.Position(v);
        }

        [[nodiscard]] glm::vec3 VertexPosition(const Graph::Graph& graph, VertexHandle v)
        {
            return graph.VertexPosition(v);
        }

        template <class Domain>
        [[nodiscard]] double EdgeLength(const Domain& domain, VertexHandle from, VertexHandle to)
        {
            const glm::vec3 delta = VertexPosition(domain, to) - VertexPosition(domain, from);
            const auto lengthSquared = static_cast<double>(glm::dot(delta, delta));
            if (!(lengthSquared >= 0.0) || !std::isfinite(lengthSquared))
            {
                return std::numeric_limits<double>::infinity();
            }
            return std::sqrt(lengthSquared);
        }

        template <class Domain>
        [[nodiscard]] bool IsUsableVertex(const Domain& domain, VertexHandle v)
        {
            return v.IsValid() && domain.IsValid(v) && !domain.IsDeleted(v) && !domain.IsIsolated(v);
        }

        struct FrontierPriority
        {
            double Distance{0.0};
            PropertyIndex VertexIndex{0};
        };

        [[nodiscard]] bool FrontierPriorityBefore(FrontierPriority a, FrontierPriority b)
        {
            if (a.Distance != b.Distance) return a.Distance < b.Distance;
            return a.VertexIndex < b.VertexIndex;
        }

        [[nodiscard]] bool LegacyLazyFrontierWouldHavePendingEntry(
            std::span<const FrontierPriority> supersededEntries,
            std::optional<FrontierPriority> lastSettledPriority)
        {
            if (!lastSettledPriority) return false;
            for (const FrontierPriority entry : supersededEntries)
            {
                if (!FrontierPriorityBefore(entry, *lastSettledPriority))
                {
                    return true;
                }
            }
            return false;
        }

        template <class Domain>
        [[nodiscard]] std::optional<Graph::Graph> ExtractPathGraphCommon(
            const Domain& domain,
            const ShortestPathResult& result,
            std::span<const VertexHandle> startVertices,
            std::span<const VertexHandle> endVertices)
        {
            if (domain.VerticesSize() == 0) return std::nullopt;
            if (startVertices.empty() && endVertices.empty()) return std::nullopt;
            if (!result.Distances || !result.Predecessors) return std::nullopt;

            const std::size_t vertexCount = domain.VerticesSize();
            const double kInfinity = std::numeric_limits<double>::infinity();
            std::vector<std::uint8_t> included(vertexCount, 0);

            const bool traceTargets = !startVertices.empty() && !endVertices.empty();
            if (traceTargets)
            {
                for (const VertexHandle goal : endVertices)
                {
                    if (!IsUsableVertex(domain, goal)) continue;
                    std::size_t safety = 0;
                    VertexHandle current = goal;
                    while (current.IsValid() && domain.IsValid(current) && !domain.IsDeleted(current))
                    {
                        if (!std::isfinite(result.Distances[current])) break;
                        if (included[current.Index] != 0) break;
                        included[current.Index] = 1;

                        const VertexHandle predecessor = result.Predecessors[current];
                        if (!predecessor.IsValid()) break;
                        current = predecessor;
                        if (++safety > vertexCount) break;
                    }
                }
            }
            else
            {
                for (std::size_t i = 0; i < vertexCount; ++i)
                {
                    const VertexHandle v{static_cast<PropertyIndex>(i)};
                    if (!IsUsableVertex(domain, v)) continue;
                    if (std::isfinite(result.Distances[v])) included[i] = 1;
                }
            }

            std::size_t includedCount = 0;
            std::size_t edgeCount = 0;
            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                if (included[i] == 0) continue;
                ++includedCount;
                const VertexHandle v{static_cast<PropertyIndex>(i)};
                const VertexHandle predecessor = result.Predecessors[v];
                if (predecessor.IsValid() && predecessor.Index < vertexCount && included[predecessor.Index] != 0)
                {
                    ++edgeCount;
                }
            }

            if (includedCount == 0) return std::nullopt;

            Graph::Graph pathGraph;
            pathGraph.Reserve(includedCount, edgeCount);

            auto originalVertexProperty = pathGraph.GetOrAddVertexProperty<VertexHandle>("v:original_vertex", VertexHandle{});
            auto distanceProperty = pathGraph.GetOrAddVertexProperty<double>("v:shortest_path_distance", kInfinity);

            std::vector<VertexHandle> remap(vertexCount, VertexHandle{});
            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                if (included[i] == 0) continue;
                const VertexHandle original{static_cast<PropertyIndex>(i)};
                const VertexHandle mapped = pathGraph.AddVertex(VertexPosition(domain, original));
                remap[i] = mapped;
                originalVertexProperty[mapped] = original;
                distanceProperty[mapped] = result.Distances[original];
            }

            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                if (included[i] == 0) continue;
                const VertexHandle v{static_cast<PropertyIndex>(i)};
                const VertexHandle predecessor = result.Predecessors[v];
                if (!predecessor.IsValid() || predecessor.Index >= vertexCount) continue;
                if (included[predecessor.Index] == 0) continue;
                (void)pathGraph.AddEdge(remap[predecessor.Index], remap[i]);
            }

            return pathGraph;
        }

        template <GraphLike Domain>
        [[nodiscard]] std::optional<ShortestPathResult> DijkstraCommon(
            Domain& graph,
            std::span<const VertexHandle> seedVertices,
            std::span<const VertexHandle> goalVertices,
            const DijkstraParams& params)
        {
            if (graph.VerticesSize() == 0) return std::nullopt;
            if (seedVertices.empty() && goalVertices.empty()) return std::nullopt;

            const std::size_t vertexCount = graph.VerticesSize();
            const double kInfinity = std::numeric_limits<double>::infinity();

            ShortestPathResult result{};
            result.Distances = VertexProperty<double>(graph.VertexProperties().template GetOrAdd<double>("v:shortest_path_distance", kInfinity));
            result.Predecessors = VertexProperty<VertexHandle>(graph.VertexProperties().template GetOrAdd<VertexHandle>("v:shortest_path_predecessor", VertexHandle{}));

            result.Distances.Vector().assign(vertexCount, kInfinity);
            result.Predecessors.Vector().assign(vertexCount, VertexHandle{});

            std::vector<std::uint8_t> goalMask(vertexCount, 0);
            std::size_t validGoalCount = 0;
            for (const VertexHandle target : goalVertices)
            {
                if (!IsUsableVertex(graph, target)) continue;
                if (goalMask[target.Index] != 0) continue;
                goalMask[target.Index] = 1;
                ++validGoalCount;
            }

            // True decrease-key frontier. QueuePushCount intentionally counts
            // each successful historical frontier update (including
            // DecreaseKey) to preserve the old lazy-priority-queue diagnostics.
            Extrinsic::Core::IndexedHeap<double, PropertyIndex> queue;
            std::vector<double> frontierDistances(vertexCount, kInfinity);
            std::vector<FrontierPriority> supersededFrontierEntries;
            std::size_t validStartCount = 0;
            for (const VertexHandle source : seedVertices)
            {
                if (!IsUsableVertex(graph, source)) continue;
                if (result.Distances[source] > 0.0) result.Distances[source] = 0.0;
                result.Predecessors[source] = VertexHandle{};
                if (queue.Contains(source.Index))
                {
                    supersededFrontierEntries.push_back(FrontierPriority{frontierDistances[source.Index], source.Index});
                    (void)queue.DecreaseKey(source.Index, 0.0);
                }
                else
                {
                    (void)queue.Push(0.0, source.Index);
                }
                frontierDistances[source.Index] = 0.0;
                ++result.QueuePushCount;
                ++validStartCount;
            }

            if (validStartCount == 0) return std::nullopt;
            if (!goalVertices.empty() && validGoalCount == 0) return std::nullopt;

            std::vector<std::uint8_t> settled(vertexCount, 0);
            const std::size_t settleBudget = params.MaxSettledVertices > 0 ? params.MaxSettledVertices : vertexCount;
            const bool trackGoals = !goalVertices.empty();
            bool reachedGoal = goalVertices.empty();
            std::optional<FrontierPriority> lastSettledPriority;

            while (!queue.Empty() && result.SettledVertexCount < settleBudget)
            {
                std::pair<double, PropertyIndex> front;
                if (!queue.TryPop(front)) break;

                const VertexHandle current{front.second};
                const double currentDistance = front.first;
                if (settled[current.Index] != 0) continue;
                settled[current.Index] = 1;
                frontierDistances[current.Index] = kInfinity;
                lastSettledPriority = FrontierPriority{currentDistance, current.Index};
                ++result.SettledVertexCount;

                if (goalMask[current.Index] != 0)
                {
                    ++result.ReachedGoalCount;
                    reachedGoal = true;
                    if (params.StopWhenAllTargetsSettled && result.ReachedGoalCount >= validGoalCount)
                    {
                        result.Converged = true;
                        result.EarlyTerminated = true;
                        break;
                    }
                }

                std::size_t safety = 0;
                const std::size_t safetyLimit = graph.HalfedgesSize();
                for (const HalfedgeHandle h : graph.HalfedgesAroundVertex(current))
                {
                    if (++safety > safetyLimit) break;

                    const VertexHandle next = graph.ToVertex(h);
                    if (!IsUsableVertex(graph, next)) continue;

                    const double stepLength = EdgeLength(graph, current, next);
                    if (!std::isfinite(stepLength)) continue;

                    const double newDistance = currentDistance + stepLength;
                    ++result.RelaxedEdgeCount;
                    if (newDistance < result.Distances[next])
                    {
                        result.Distances[next] = newDistance;
                        result.Predecessors[next] = current;
                        if (queue.Contains(next.Index))
                        {
                            supersededFrontierEntries.push_back(FrontierPriority{frontierDistances[next.Index], next.Index});
                            (void)queue.DecreaseKey(next.Index, newDistance);
                        }
                        else
                        {
                            (void)queue.Push(newDistance, next.Index);
                        }
                        frontierDistances[next.Index] = newDistance;
                        ++result.QueuePushCount;
                    }
                }
            }

            const bool legacyLazyFrontierWouldHavePendingEntries =
                LegacyLazyFrontierWouldHavePendingEntry(supersededFrontierEntries, lastSettledPriority);
            if (result.SettledVertexCount >= settleBudget &&
                (!queue.Empty() || legacyLazyFrontierWouldHavePendingEntries))
            {
                result.Converged = false;
            }
            else if (!trackGoals)
            {
                result.Converged = true;
            }
            else if (result.ReachedGoalCount >= validGoalCount)
            {
                result.Converged = true;
            }

            if (!reachedGoal) return std::nullopt;
            return result;
        }
    } // namespace

    std::optional<ShortestPathResult> Dijkstra(
        HalfedgeMesh::Mesh& mesh,
        std::span<const VertexHandle> startVertices,
        std::span<const VertexHandle> endVertices,
        const DijkstraParams& params)
    {
        if (startVertices.empty() && endVertices.empty()) return std::nullopt;

        const auto seedVertices = startVertices.empty() ? endVertices : startVertices;
        const auto goalVertices = startVertices.empty() ? std::span<const VertexHandle>{} : endVertices;
        return DijkstraCommon(mesh, seedVertices, goalVertices, params);
    }

    std::optional<ShortestPathResult> Dijkstra(
        Graph::Graph& graph,
        std::span<const VertexHandle> startVertices,
        std::span<const VertexHandle> endVertices,
        const DijkstraParams& params)
    {
        if (startVertices.empty() && endVertices.empty()) return std::nullopt;

        const auto seedVertices = startVertices.empty() ? endVertices : startVertices;
        const auto goalVertices = startVertices.empty() ? std::span<const VertexHandle>{} : endVertices;
        return DijkstraCommon(graph, seedVertices, goalVertices, params);
    }

    std::optional<Graph::Graph> ExtractPathGraph(
        const HalfedgeMesh::Mesh& mesh,
        const ShortestPathResult& result,
        std::span<const VertexHandle> startVertices,
        std::span<const VertexHandle> endVertices)
    {
        return ExtractPathGraphCommon(mesh, result, startVertices, endVertices);
    }

    std::optional<Graph::Graph> ExtractPathGraph(
        const Graph::Graph& graph,
        const ShortestPathResult& result,
        std::span<const VertexHandle> startVertices,
        std::span<const VertexHandle> endVertices)
    {
        return ExtractPathGraphCommon(graph, result, startVertices, endVertices);
    }
}
