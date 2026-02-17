module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <utility>
#include <span>
#include <vector>
#include <glm/glm.hpp>

module Geometry:KDTree.Impl;
import :KDTree;

namespace Geometry
{
    namespace
    {
        struct BuildFrame
        {
            std::uint32_t NodeIndex{0};
            std::uint32_t Start{0};
            std::uint32_t Count{0};
            std::uint32_t Depth{0};
        };

        [[nodiscard]] float CentroidAxis(const AABB& box, const std::uint8_t axis)
        {
            return (box.Min[axis] + box.Max[axis]) * 0.5f;
        }

        [[nodiscard]] AABB ComputeBounds(const std::vector<AABB>& elementAabbs,
            const std::vector<KDTree::ElementIndex>& elementIndices, const std::uint32_t start,
            const std::uint32_t count)
        {
            AABB bounds{};
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const AABB& elementBox = elementAabbs[elementIndices[start + i]];
                bounds.Min = glm::min(bounds.Min, elementBox.Min);
                bounds.Max = glm::max(bounds.Max, elementBox.Max);
            }
            return bounds;
        }

        [[nodiscard]] float DistanceSquaredToNode(const glm::vec3& p, const KDTree::Node& node)
        {
            return static_cast<float>(SquaredDistance(node.Aabb, p));
        }
    }

    std::optional<KDTreeBuildResult> KDTree::Build(std::span<const AABB> elementAabbs, const KDTreeBuildParams& params)
    {
        m_ElementAabbs.assign(elementAabbs.begin(), elementAabbs.end());
        return BuildFromOwned(params);
    }

    std::optional<KDTreeBuildResult> KDTree::Build(std::vector<AABB>&& elementAabbs, const KDTreeBuildParams& params)
    {
        m_ElementAabbs = std::move(elementAabbs);
        return BuildFromOwned(params);
    }

    std::optional<KDTreeBuildResult> KDTree::BuildFromPoints(std::span<const glm::vec3> points,
        const KDTreeBuildParams& params)
    {
        std::vector<AABB> aabbs;
        aabbs.reserve(points.size());
        for (const glm::vec3& p : points)
        {
            aabbs.push_back(AABB{.Min = p, .Max = p});
        }
        return Build(std::move(aabbs), params);
    }

    std::optional<KDTreeBuildResult> KDTree::BuildFromOwned(const KDTreeBuildParams& params)
    {
        m_Nodes.clear();
        m_ElementIndices.clear();

        if (m_ElementAabbs.empty() || params.LeafSize == 0 || params.MaxDepth == 0 ||
            !std::isfinite(params.MinSplitExtent) || params.MinSplitExtent < 0.0f)
        {
            return std::nullopt;
        }

        m_ElementIndices.resize(m_ElementAabbs.size());
        for (ElementIndex i = 0; i < static_cast<ElementIndex>(m_ElementIndices.size()); ++i)
        {
            m_ElementIndices[i] = i;
        }

        m_Nodes.push_back(Node{});
        std::vector<BuildFrame> stack;
        stack.push_back(BuildFrame{0u, 0u, static_cast<std::uint32_t>(m_ElementIndices.size()), 0u});

        std::uint32_t maxDepthReached = 0;

        while (!stack.empty())
        {
            const BuildFrame frame = stack.back();
            stack.pop_back();

            Node& node = m_Nodes[frame.NodeIndex];
            node.FirstElement = frame.Start;
            node.NumElements = frame.Count;
            node.Aabb = ComputeBounds(m_ElementAabbs, m_ElementIndices, frame.Start, frame.Count);

            maxDepthReached = std::max(maxDepthReached, frame.Depth);

            if (frame.Count <= params.LeafSize || frame.Depth >= params.MaxDepth)
            {
                node.IsLeaf = true;
                continue;
            }

            const glm::vec3 extent = node.Aabb.Max - node.Aabb.Min;
            const std::array<float, 3> extents{extent.x, extent.y, extent.z};

            std::uint8_t axis = 0;
            if (extents[1] > extents[axis]) axis = 1;
            if (extents[2] > extents[axis]) axis = 2;

            if (extents[axis] <= params.MinSplitExtent)
            {
                node.IsLeaf = true;
                continue;
            }

            const std::uint32_t mid = frame.Start + frame.Count / 2;
            auto beginIt = m_ElementIndices.begin() + frame.Start;
            auto midIt = m_ElementIndices.begin() + mid;
            auto endIt = m_ElementIndices.begin() + frame.Start + frame.Count;

            std::nth_element(beginIt, midIt, endIt, [axis, this](const ElementIndex lhs, const ElementIndex rhs)
            {
                const float l = CentroidAxis(m_ElementAabbs[lhs], axis);
                const float r = CentroidAxis(m_ElementAabbs[rhs], axis);
                if (l != r)
                {
                    return l < r;
                }
                return lhs < rhs;
            });

            const std::uint32_t leftCount = mid - frame.Start;
            const std::uint32_t rightCount = frame.Count - leftCount;

            if (leftCount == 0 || rightCount == 0)
            {
                node.IsLeaf = true;
                continue;
            }

            node.SplitAxis = axis;
            node.SplitValue = CentroidAxis(m_ElementAabbs[m_ElementIndices[mid]], axis);
            node.IsLeaf = false;

            const NodeIndex leftIndex = static_cast<NodeIndex>(m_Nodes.size());
            m_Nodes.push_back(Node{});
            const NodeIndex rightIndex = static_cast<NodeIndex>(m_Nodes.size());
            m_Nodes.push_back(Node{});

            node.Left = leftIndex;
            node.Right = rightIndex;

            stack.push_back(BuildFrame{rightIndex, mid, rightCount, frame.Depth + 1});
            stack.push_back(BuildFrame{leftIndex, frame.Start, leftCount, frame.Depth + 1});
        }

        return KDTreeBuildResult{
            .ElementCount = m_ElementAabbs.size(),
            .NodeCount = m_Nodes.size(),
            .MaxDepthReached = maxDepthReached,
        };
    }

    std::optional<KDTreeKnnResult> KDTree::QueryKnn(const glm::vec3& query, const std::uint32_t k,
        std::vector<ElementIndex>& outElementIndices) const
    {
        outElementIndices.clear();
        if (m_Nodes.empty() || k == 0)
        {
            return std::nullopt;
        }

        using Candidate = std::pair<float, ElementIndex>; // dist2, element index
        auto maxHeapCmp = [](const Candidate& a, const Candidate& b)
        {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        };

        std::priority_queue<Candidate, std::vector<Candidate>, decltype(maxHeapCmp)> best(maxHeapCmp);
        std::vector<NodeIndex> stack;
        stack.push_back(0u);

        std::size_t visitedNodes = 0;
        std::size_t distanceEvaluations = 0;

        while (!stack.empty())
        {
            const NodeIndex nodeIndex = stack.back();
            stack.pop_back();
            ++visitedNodes;

            const Node& node = m_Nodes[nodeIndex];
            const float nodeLowerBound = DistanceSquaredToNode(query, node);
            if (best.size() == k && nodeLowerBound > best.top().first)
            {
                continue;
            }

            if (node.IsLeaf)
            {
                const std::size_t end = static_cast<std::size_t>(node.FirstElement) + node.NumElements;
                for (std::size_t i = node.FirstElement; i < end; ++i)
                {
                    const ElementIndex elementIndex = m_ElementIndices[i];
                    const float dist2 = static_cast<float>(SquaredDistance(m_ElementAabbs[elementIndex], query));
                    ++distanceEvaluations;

                    if (best.size() < k)
                    {
                        best.emplace(dist2, elementIndex);
                    }
                    else if (dist2 < best.top().first || (dist2 == best.top().first && elementIndex < best.top().second))
                    {
                        best.pop();
                        best.emplace(dist2, elementIndex);
                    }
                }
                continue;
            }

            const Node& left = m_Nodes[node.Left];
            const Node& right = m_Nodes[node.Right];
            const float leftBound = DistanceSquaredToNode(query, left);
            const float rightBound = DistanceSquaredToNode(query, right);

            if (leftBound <= rightBound)
            {
                stack.push_back(node.Right);
                stack.push_back(node.Left);
            }
            else
            {
                stack.push_back(node.Left);
                stack.push_back(node.Right);
            }
        }

        std::vector<Candidate> ordered;
        ordered.reserve(best.size());
        while (!best.empty())
        {
            ordered.push_back(best.top());
            best.pop();
        }

        std::sort(ordered.begin(), ordered.end(), [](const Candidate& a, const Candidate& b)
        {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });

        outElementIndices.reserve(ordered.size());
        for (const auto [dist2, index] : ordered)
        {
            static_cast<void>(dist2);
            outElementIndices.push_back(index);
        }

        return KDTreeKnnResult{
            .ReturnedCount = outElementIndices.size(),
            .VisitedNodes = visitedNodes,
            .DistanceEvaluations = distanceEvaluations,
            .MaxDistanceSquared = ordered.empty() ? 0.0f : ordered.back().first,
        };
    }

    std::optional<KDTreeRadiusResult> KDTree::QueryRadius(const glm::vec3& query, const float radius,
        std::vector<ElementIndex>& outElementIndices) const
    {
        outElementIndices.clear();
        if (m_Nodes.empty() || !std::isfinite(radius) || radius < 0.0f)
        {
            return std::nullopt;
        }

        const float radius2 = radius * radius;

        std::vector<NodeIndex> stack;
        stack.push_back(0u);

        std::size_t visitedNodes = 0;
        std::size_t distanceEvaluations = 0;

        while (!stack.empty())
        {
            const NodeIndex nodeIndex = stack.back();
            stack.pop_back();
            ++visitedNodes;

            const Node& node = m_Nodes[nodeIndex];
            if (DistanceSquaredToNode(query, node) > radius2)
            {
                continue;
            }

            if (node.IsLeaf)
            {
                const std::size_t end = static_cast<std::size_t>(node.FirstElement) + node.NumElements;
                for (std::size_t i = node.FirstElement; i < end; ++i)
                {
                    const ElementIndex elementIndex = m_ElementIndices[i];
                    const float dist2 = static_cast<float>(SquaredDistance(m_ElementAabbs[elementIndex], query));
                    ++distanceEvaluations;
                    if (dist2 <= radius2)
                    {
                        outElementIndices.push_back(elementIndex);
                    }
                }
                continue;
            }

            stack.push_back(node.Left);
            stack.push_back(node.Right);
        }

        std::sort(outElementIndices.begin(), outElementIndices.end());

        return KDTreeRadiusResult{
            .ReturnedCount = outElementIndices.size(),
            .VisitedNodes = visitedNodes,
            .DistanceEvaluations = distanceEvaluations,
        };
    }
}
