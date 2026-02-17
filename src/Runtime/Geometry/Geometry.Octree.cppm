module;

#include <array>
#include <algorithm>
#include <limits>
#include <iterator>
#include <utility>
#include <cstdint>
#include <span>
#include <cmath>
#include <vector>
#include <string>
#include <glm/glm.hpp>

export module Geometry:Octree;

import Utils.BoundedHeap;
import :Properties;
import :AABB;
import :Primitives;
import :Containment;
import :Overlap;
import :Support;

export namespace Geometry
{
    template <typename Shape>
    concept SpatialQueryShape =
        requires(const Shape& s, const AABB& box)
        {
            { TestOverlap(box, s) } -> std::convertible_to<bool>;
        };

    template <typename Shape>
    concept VolumetricSpatialQueryShape =
        SpatialQueryShape<Shape> &&
        requires(const Shape& s, const AABB& box)
        {
            { Contains(s, box) } -> std::convertible_to<bool>;
            { Volume(s) } -> std::convertible_to<double>;
        };

    class Octree
    {
    public:
        using NodeIndex = std::uint32_t;
        using ElementIndex = std::uint32_t;
        static constexpr NodeIndex kInvalidIndex = std::numeric_limits<NodeIndex>::max();

        struct Node
        {
            AABB Aabb;
            NodeIndex BaseChildIndex{kInvalidIndex};

            ElementIndex FirstElement = std::numeric_limits<ElementIndex>::max();
            std::uint32_t NumElements = 0;
            std::uint32_t NumStraddlers = 0; // number of elements that straddle child node boundaries
            // total number of elements in this node (including straddlers).Necessary for early out in queries

            uint8_t ChildMask = 0;
            bool IsLeaf = true;

            Node() = default;

            [[nodiscard]] bool ChildExists(std::size_t childIdx) const
            {
                return (ChildMask & (1 << childIdx)) != 0;
            }
        };

        enum class SplitPoint { Center, Mean, Median };

        struct SplitPolicy
        {
            SplitPoint SplitPoint = SplitPoint::Center;
            bool TightChildren = true; // shrink child boxes to exactly fit contents
            float Epsilon = 0.0f; // optional padding when tightening
        };

        std::vector<Node> m_Nodes;
        Nodes NodeProperties;

        std::vector<AABB> ElementAabbs;

        template <class T>
        [[nodiscard]] NodeProperty<T> AddNodeProperty(const std::string& name, T defaultValue = T())
        {
            if (NodeProperties.Size() != m_Nodes.size()) NodeProperties.Resize(m_Nodes.size());
            return NodeProperty<T>(NodeProperties.Add<T>(name, defaultValue));
        }

        template <class T>
        [[nodiscard]] NodeProperty<T> GetNodeProperty(const std::string& name) const
        {
            return NodeProperty<T>(NodeProperties.Get<T>(name));
        }

        template <class T>
        [[nodiscard]] NodeProperty<T> GetOrAddNodeProperty(const std::string& name, T defaultValue = T())
        {
            return NodeProperty<T>(NodeProperties.GetOrAdd<T>(name, defaultValue));
        }

        template <class T>
        void RemoveNodeProperty(NodeProperty<T>& prop)
        {
            NodeProperties.Remove(prop);
        }

        [[nodiscard]] bool HasNodeProperty(const std::string& name) const { return NodeProperties.Exists(name); }


        [[nodiscard]] std::size_t GetMaxElementsPerNode() const noexcept
        {
            return m_MaxElementsPerNode;
        }

        [[nodiscard]] std::size_t GetMaxBvhDepth() const noexcept
        {
            return m_MaxBvhDepth;
        }

        [[nodiscard]] const SplitPolicy& GetSplitPolicy() const noexcept
        {
            return m_SplitPolicy;
        }

        [[nodiscard]] const std::vector<ElementIndex>& GetElementIndices() const noexcept
        {
            return m_ElementIndices;
        }

        /// @brief Build the octree from a span of AABBs.
        /// @return true if build succeeded, false if input was empty or invalid.
        [[nodiscard]] bool Build(std::span<const AABB> aabbs, const SplitPolicy& policy, const std::size_t maxPerNode,
                                 const std::size_t maxDepth)
        {
            ElementAabbs.assign(aabbs.begin(), aabbs.end());
            return BuildFromOwned(policy, maxPerNode, maxDepth);
        }

        /// @brief Build the octree by taking ownership of the AABB storage.
        /// @return true if build succeeded, false if input was empty or invalid.
        [[nodiscard]] bool Build(std::vector<AABB>&& aabbs, const SplitPolicy& policy, const std::size_t maxPerNode,
                                 const std::size_t maxDepth)
        {
            ElementAabbs = std::move(aabbs);
            return BuildFromOwned(policy, maxPerNode, maxDepth);
        }

        void QueryRay(const Ray& queryShape, std::vector<size_t>& result) const
        {
            Query<Ray>(queryShape, result);
        }

        void QueryAABB(const AABB& queryShape, std::vector<size_t>& out) const
        {
            Query<AABB>(queryShape, out);
        }

        void QuerySphere(const Sphere& queryShape, std::vector<size_t>& out) const
        {
            Query<Sphere>(queryShape, out);
        }

        template <VolumetricSpatialQueryShape Shape>
        void Query(const Shape& queryShape, std::vector<size_t>& result) const
        {
            result.clear();
            if (m_Nodes.empty())
            {
                return;
            }

            const auto& nodeData = m_Nodes;
            const Node* nodePtr = nodeData.data(); // Raw pointer for speed

            constexpr double eps = 0.0; // set to a small positive tolerance if you want numerical slack
            const auto queryVolume = static_cast<double>(Volume(queryShape));

            // Use a small local stack to avoid heap allocation for the stack itself if possible,
            // though std::vector is fine given the depth is low (10).
            // Optimization: Use a fixed array stack since MaxDepth is known/limited.
            alignas(64) std::array<NodeIndex, 128> stack{};
            // Depth 10 * 8 children < 64? No, but stack depth is roughly depth*7 in worst case?
            // Actually for DFS, stack size is proportional to Depth. 64 is plenty for depth 10.
            int stackTop = 0;
            stack[stackTop++] = 0; // Push Root (Index 0)

            while (stackTop > 0)
            {
                const size_t nodeIdx = stack[--stackTop];
                const Node& node = nodePtr[nodeIdx];

                if (!TestOverlap(node.Aabb, queryShape)) continue;

                const double nodeVolume = node.Aabb.GetVolume();
                const double strictlyLarger = (queryVolume > nodeVolume + eps);

                if (strictlyLarger && Contains(queryShape, node.Aabb))
                {
                    // Optimization: Batch copy
                    const size_t end = node.FirstElement + node.NumElements;
                    for (size_t i = node.FirstElement; i < end; ++i)
                    {
                        result.push_back(m_ElementIndices[i]);
                    }
                    continue;
                }

                // Process Elements (Leaf or Straddlers)
                size_t elemEnd = node.FirstElement + (node.IsLeaf ? node.NumElements : node.NumStraddlers);
                for (size_t i = node.FirstElement; i < elemEnd; ++i)
                {
                    size_t ei = m_ElementIndices[i];
                    if (TestOverlap(ElementAabbs[ei], queryShape))
                    {
                        result.push_back(ei);
                    }
                }

                if (!node.IsLeaf && node.BaseChildIndex != kInvalidIndex)
                {
                    uint32_t childOffset = 0;
                    for (int i = 0; i < 8; ++i)
                    {
                        // Check if the i-th octant exists
                        if (node.ChildExists(i))
                        {
                            // Calculate absolute index in m_Nodes
                            const NodeIndex childIndex = node.BaseChildIndex + childOffset;

                            // Optimization: Check child AABB before pushing to stack
                            if (TestOverlap(nodePtr[childIndex].Aabb, queryShape))
                            {
                                stack[stackTop++] = childIndex;
                            }

                            // Move to the next existing child in the contiguous block
                            childOffset++;
                        }
                    }
                }
            }
        }

        template <SpatialQueryShape Shape>
        void Query(const Shape& queryShape, std::vector<size_t>& result) const
            requires (!VolumetricSpatialQueryShape<Shape>)
        {
            result.clear();
            if (m_Nodes.empty()) return;

            const Node* nodePtr = m_Nodes.data();

            // OPTIMIZATION: Use std::array stack here too for performance consistency
            alignas(64) std::array<NodeIndex, 128> stack{};
            int stackTop = 0;
            stack[stackTop++] = 0;

            while (stackTop > 0)
            {
                const NodeIndex nodeIdx = stack[--stackTop];
                const Node& node = nodePtr[nodeIdx];

                if (!TestOverlap(node.Aabb, queryShape)) continue;

                if (node.IsLeaf)
                {
                    for (size_t i = 0; i < node.NumElements; ++i)
                    {
                        std::size_t ei = m_ElementIndices[node.FirstElement + i];
                        if (TestOverlap(ElementAabbs[ei], queryShape))
                        {
                            result.push_back(ei);
                        }
                    }
                }
                else
                {
                    for (size_t i = 0; i < node.NumStraddlers; ++i)
                    {
                        std::size_t ei = m_ElementIndices[node.FirstElement + i];
                        if (TestOverlap(ElementAabbs[ei], queryShape))
                        {
                            result.push_back(ei);
                        }
                    }

                    if (node.BaseChildIndex != kInvalidIndex)
                    {
                        uint32_t childOffset = 0;
                        for (int i = 0; i < 8; ++i)
                        {
                            // Check if the i-th octant exists
                            if (node.ChildExists(i))
                            {
                                // Calculate absolute index in m_Nodes
                                const NodeIndex childIndex = node.BaseChildIndex + childOffset;

                                // Optimization: Check child AABB before pushing to stack
                                if (childIndex != kInvalidIndex && TestOverlap(nodePtr[childIndex].Aabb, queryShape))
                                {
                                    stack[stackTop++] = childIndex;
                                }

                                // Move to the next existing child in the contiguous block
                                childOffset++;
                            }
                        }
                    }
                }
            }
        }

        void QueryKnn(const glm::vec3& queryPoint, std::size_t k, std::vector<size_t>& results) const;

        void QueryNearest(const glm::vec3& queryPoint, std::size_t& result) const;

        [[nodiscard]] bool ValidateStructure() const
        {
            if (NodeProperties.Empty()) return m_ElementIndices.empty();
            return ValidateNode(0);
        }

    private:
        [[nodiscard]] bool BuildFromOwned(const SplitPolicy& policy, const std::size_t maxPerNode,
                                          const std::size_t maxDepth);

        [[nodiscard]] bool ValidateNode(NodeIndex nodeIdx) const;

        NodeIndex CreateNode()
        {
            const auto idx = static_cast<NodeIndex>(m_Nodes.size());
            m_Nodes.emplace_back();
            return idx;
        }

        void SubdivideVolume(const NodeIndex nodeIdx, std::size_t depth, std::vector<size_t>& scratch);

        [[nodiscard]] glm::vec3 ComputeMeanCenter(size_t first, std::size_t size,
                                                  const glm::vec3& fallbackCenter) const;

        [[nodiscard]] glm::vec3 ComputeMedianCenter(size_t first, std::size_t size,
                                                    const glm::vec3& fallbackCenter) const;

        [[nodiscard]] glm::vec3 ChooseSplitPoint(NodeIndex nodeIdx) const;

        template <typename FwdIt>
        [[nodiscard]] AABB TightChildAabb(FwdIt begin, FwdIt end, float eps = 0.0f) const
        {
            if (begin == end)
            {
                return {}; // Return an explicitly invalid AABB
            }

            AABB tight = ElementAabbs[*begin];

            for (auto it = std::next(begin); it != end; ++it)
            {
                tight = Union(tight, ElementAabbs[*it]);
            }

            if (eps > 0.0f)
            {
                glm::vec3 padding(eps, eps, eps);
                tight.Min -= padding;
                tight.Max += padding;
            }
            return tight;
        }

        [[nodiscard]] AABB TightChildAabb(const std::vector<size_t>& elems, float eps = 0.0f) const
        {
            return TightChildAabb(elems.begin(), elems.end(), eps);
        }

        std::size_t m_MaxElementsPerNode = 32;
        std::size_t m_MaxBvhDepth = 10;
        SplitPolicy m_SplitPolicy;
        std::vector<ElementIndex> m_ElementIndices;
    };
}
