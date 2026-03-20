module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Geometry.BVH;

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
            const std::vector<BVH::ElementIndex>& elementIndices, const std::uint32_t start,
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
    }

    std::optional<BVHBuildResult> BVH::Build(std::span<const AABB> elementAabbs, const BVHBuildParams& params)
    {
        m_ElementAabbs.assign(elementAabbs.begin(), elementAabbs.end());
        return BuildFromOwned(params);
    }

    std::optional<BVHBuildResult> BVH::Build(std::vector<AABB>&& elementAabbs, const BVHBuildParams& params)
    {
        m_ElementAabbs = std::move(elementAabbs);
        return BuildFromOwned(params);
    }

    std::optional<BVHBuildResult> BVH::BuildFromOwned(const BVHBuildParams& params)
    {
        m_Nodes.clear();
        m_ElementIndices.clear();

        if (m_ElementAabbs.empty() || params.LeafSize == 0 || params.MaxDepth == 0 ||
            !std::isfinite(params.MinSplitExtent) || params.MinSplitExtent < 0.0f)
        {
            return std::nullopt;
        }

        for (const AABB& box : m_ElementAabbs)
        {
            if (!box.IsValid())
                return std::nullopt;
        }

        m_ElementIndices.resize(m_ElementAabbs.size());
        for (ElementIndex i = 0; i < static_cast<ElementIndex>(m_ElementIndices.size()); ++i)
            m_ElementIndices[i] = i;

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

            AABB centroidBounds{};
            for (std::uint32_t i = 0; i < frame.Count; ++i)
            {
                const AABB& box = m_ElementAabbs[m_ElementIndices[frame.Start + i]];
                const glm::vec3 centroid = box.GetCenter();
                centroidBounds.Min = glm::min(centroidBounds.Min, centroid);
                centroidBounds.Max = glm::max(centroidBounds.Max, centroid);
            }

            const glm::vec3 extent = centroidBounds.GetSize();
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
                    return l < r;
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

            m_Nodes[frame.NodeIndex].Left = leftIndex;
            m_Nodes[frame.NodeIndex].Right = rightIndex;

            stack.push_back(BuildFrame{rightIndex, mid, rightCount, frame.Depth + 1});
            stack.push_back(BuildFrame{leftIndex, frame.Start, leftCount, frame.Depth + 1});
        }

        return BVHBuildResult{
            .ElementCount = m_ElementAabbs.size(),
            .NodeCount = m_Nodes.size(),
            .MaxDepthReached = maxDepthReached,
        };
    }
}

