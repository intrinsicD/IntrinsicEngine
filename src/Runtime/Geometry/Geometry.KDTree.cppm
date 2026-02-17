module;

#include <cstddef>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>
#include <glm/glm.hpp>

export module Geometry:KDTree;

import :AABB;
import :Primitives;
import :Containment;
import :Overlap;

export namespace Geometry
{
    template <typename Shape>
    concept SpatialQueryShape =
        requires(const Shape& s, const AABB& box)
        {
            { TestOverlap(box, s) } -> std::convertible_to<bool>;
        };

    struct KDTreeBuildParams
    {
        std::uint32_t LeafSize{16};
        std::uint32_t MaxDepth{32};
        float MinSplitExtent{1.0e-12f};
    };

    struct KDTreeBuildResult
    {
        std::size_t ElementCount{0};
        std::size_t NodeCount{0};
        std::uint32_t MaxDepthReached{0};
    };

    struct KDTreeKnnResult
    {
        std::size_t ReturnedCount{0};
        std::size_t VisitedNodes{0};
        std::size_t DistanceEvaluations{0};
        float MaxDistanceSquared{0.0f};
    };

    struct KDTreeRadiusResult
    {
        std::size_t ReturnedCount{0};
        std::size_t VisitedNodes{0};
        std::size_t DistanceEvaluations{0};
    };

    class KDTree
    {
    public:
        using NodeIndex = std::uint32_t;
        using ElementIndex = std::uint32_t;
        static constexpr NodeIndex kInvalidIndex = std::numeric_limits<NodeIndex>::max();

        struct Node
        {
            AABB Aabb{};
            NodeIndex Left{kInvalidIndex};
            NodeIndex Right{kInvalidIndex};
            ElementIndex FirstElement{0};
            std::uint32_t NumElements{0};
            std::uint8_t SplitAxis{0};
            float SplitValue{0.0f};
            bool IsLeaf{true};
        };

        [[nodiscard]] std::optional<KDTreeBuildResult> Build(std::span<const AABB> elementAabbs,
            const KDTreeBuildParams& params = {});
        [[nodiscard]] std::optional<KDTreeBuildResult> Build(std::vector<AABB>&& elementAabbs,
            const KDTreeBuildParams& params = {});

        [[nodiscard]] std::optional<KDTreeBuildResult> BuildFromPoints(std::span<const glm::vec3> points,
            const KDTreeBuildParams& params = {});

        template <SpatialQueryShape Shape>
        void Query(const Shape& queryShape, std::vector<ElementIndex>& out) const
        {
            out.clear();
            if (m_Nodes.empty()) return;

            std::vector<NodeIndex> stack;
            stack.push_back(0u);

            while (!stack.empty())
            {
                const NodeIndex nodeIndex = stack.back();
                stack.pop_back();

                const Node& node = m_Nodes[nodeIndex];
                if (!TestOverlap(node.Aabb, queryShape))
                {
                    continue;
                }

                if (node.IsLeaf)
                {
                    const std::size_t end = static_cast<std::size_t>(node.FirstElement) + node.NumElements;
                    for (std::size_t i = node.FirstElement; i < end; ++i)
                    {
                        const ElementIndex elementIndex = m_ElementIndices[i];
                        if (TestOverlap(m_ElementAabbs[elementIndex], queryShape))
                        {
                            out.push_back(elementIndex);
                        }
                    }
                    continue;
                }

                stack.push_back(node.Left);
                stack.push_back(node.Right);
            }
        }

        void QueryAABB(const AABB& queryShape, std::vector<ElementIndex>& out) const
        {
            Query(queryShape, out);
        }

        void QuerySphere(const Sphere& queryShape, std::vector<ElementIndex>& out) const
        {
            Query(queryShape, out);
        }

        void QueryRay(const Ray& queryShape, std::vector<ElementIndex>& out) const
        {
            Query(queryShape, out);
        }

        [[nodiscard]] std::optional<KDTreeKnnResult> QueryKnn(const glm::vec3& query, std::uint32_t k,
            std::vector<ElementIndex>& outElementIndices) const;

        [[nodiscard]] std::optional<KDTreeRadiusResult> QueryRadius(const glm::vec3& query, float radius,
            std::vector<ElementIndex>& outElementIndices) const;

        [[nodiscard]] const std::vector<AABB>& ElementAabbs() const noexcept { return m_ElementAabbs; }
        [[nodiscard]] const std::vector<ElementIndex>& ElementIndices() const noexcept { return m_ElementIndices; }
        [[nodiscard]] const std::vector<Node>& Nodes() const noexcept { return m_Nodes; }

    private:
        [[nodiscard]] std::optional<KDTreeBuildResult> BuildFromOwned(const KDTreeBuildParams& params);

        std::vector<AABB> m_ElementAabbs{};
        std::vector<ElementIndex> m_ElementIndices{};
        std::vector<Node> m_Nodes{};
    };
}
