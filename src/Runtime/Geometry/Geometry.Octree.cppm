module;

#include <array>
#include <algorithm>
#include <queue>
#include <limits>
#include <numeric>
#include <iterator>
#include <utility>
#include <cstdint>
#include <span>
#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include <glm/glm.hpp>

export module Runtime.Geometry.Octree;

import Runtime.Geometry.Utils.BoundedHeap;
import Runtime.Geometry.Properties;
import Runtime.Geometry.AABB;
import Runtime.Geometry.Primitives;
import Runtime.Geometry.Containment;
import Runtime.Geometry.Overlap;
import Runtime.Geometry.Support;

export namespace Runtime::Geometry
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
            std::array<NodeIndex, 8> Children{};

            ElementIndex FirstElement = std::numeric_limits<ElementIndex>::max();
            std::uint32_t NumElements = 0;
            std::uint32_t NumStraddlers = 0; // number of elements that straddle child node boundaries
            // total number of elements in this node (including straddlers).Necessary for early out in queries

            bool IsLeaf = true;

            Node()
            {
                Children.fill(kInvalidIndex);
            }

            friend std::ostream& operator<<(std::ostream& stream, const Node&)
            {
                stream << "output for node not yet implemented";
                return stream;
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

        std::span<const AABB> ElementAabbs;

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

        bool Build(std::span<const AABB> aabbs, const SplitPolicy& policy, const std::size_t maxPerNode,
                   const std::size_t maxDepth)
        {
            ElementAabbs = aabbs;

            if (ElementAabbs.empty())
            {
                return false;
            }

            m_SplitPolicy = policy;
            m_MaxElementsPerNode = maxPerNode;
            m_MaxBvhDepth = maxDepth;

            m_Nodes.clear();
            m_Nodes.reserve(aabbs.size() / 4);
            NodeProperties.Clear(); // Clear previous state

            const std::size_t numElements = ElementAabbs.size();
            m_ElementIndices.resize(numElements);
            std::iota(m_ElementIndices.begin(), m_ElementIndices.end(), 0);

            // Create root node
            m_Nodes.emplace_back();
            m_Nodes[0].FirstElement = 0;
            m_Nodes[0].NumElements = static_cast<uint32_t>(numElements);
            m_Nodes[0].Aabb = Union(ElementAabbs);

            std::vector<size_t> localScratch;
            localScratch.reserve(m_ElementIndices.size());
            SubdivideVolume(0, 0, localScratch);

            NodeProperties.Resize(m_Nodes.size());

            return true;
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
            std::array<NodeIndex, 64> stack{};
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

                if (!node.IsLeaf)
                {
                    for (const auto childIndex : node.Children)
                    {
                        // Check validity via index directly
                        if (childIndex != kInvalidIndex)
                        {
                            // Optimization: Check Child AABB here before pushing to stack?
                            // The original code pushed then checked.
                            // Checking here saves a stack push/pop cycle.
                            if (TestOverlap(nodePtr[childIndex].Aabb, queryShape))
                            {
                                stack[stackTop++] = childIndex;
                            }
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
            std::array<NodeIndex, 64> stack{};
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
                    for (const auto childIndex : node.Children)
                    {
                        if (TestOverlap(m_Nodes[childIndex].Aabb, queryShape))
                        {
                            stack[stackTop++] = childIndex;
                        }
                    }
                }
            }
        }

        void QueryKnn(const glm::vec3& queryPoint, std::size_t k, std::vector<size_t>& results) const
        {
            results.clear();
            if (NodeProperties.Empty() || k == 0)
            {
                return;
            }

            using QueueElement = std::pair<float, std::size_t>;
            Utils::BoundedHeap<QueueElement> heap(k);

            using Trav = std::pair<float, NodeIndex>; // (node lower-bound d2, node index)
            std::priority_queue<Trav, std::vector<Trav>, std::greater<>> pq;

            constexpr NodeIndex rootIndex(0);
            auto d2Node = [&](NodeIndex nodeIndex)
            {
                return static_cast<float>(SquaredDistance(m_Nodes[nodeIndex].Aabb, queryPoint));
            };
            auto d2Elem = [&](size_t ei)
            {
                return static_cast<float>(SquaredDistance(ElementAabbs[ei], queryPoint));
            };

            pq.emplace(d2Node(rootIndex), rootIndex);
            float tau = std::numeric_limits<float>::infinity();
            auto UpdateTau = [&]()
            {
                tau = (heap.Size() == k) ? heap.top().first : std::numeric_limits<float>::infinity();
            };

            while (!pq.empty())
            {
                auto [nd2, nodeIndex] = pq.top();
                pq.pop();

                // Global prune: the best remaining node is already worse than our kth best.
                if (heap.Size() == k && nd2 > tau) break;

                const Node& node = m_Nodes[nodeIndex];

                if (node.IsLeaf)
                {
                    for (size_t i = 0; i < node.NumElements; ++i)
                    {
                        const std::size_t ei = m_ElementIndices[node.FirstElement + i];
                        const QueueElement candidate{d2Elem(ei), ei};
                        if (heap.Size() < k || candidate < heap.top())
                        {
                            heap.Push(candidate);
                            UpdateTau();
                        }
                    }
                }
                else
                {
                    // Score straddlers at this node
                    for (size_t i = 0; i < node.NumStraddlers; ++i)
                    {
                        const std::size_t ei = m_ElementIndices[node.FirstElement + i];
                        const QueueElement candidate{d2Elem(ei), ei};
                        if (heap.Size() < k || candidate < heap.top())
                        {
                            heap.Push(candidate);
                            UpdateTau();
                        }
                    }
                    // Push children best-first, pruned by current tau
                    for (const auto childIndex : node.Children)
                    {
                        if (childIndex == kInvalidIndex) continue;
                        const float cd2 = d2Node(childIndex);
                        if (cd2 <= tau) pq.emplace(cd2, childIndex);
                    }
                }
            }

            auto pairs = heap.GetSortedData(); // ascending
            results.resize(pairs.size());
            for (size_t i = 0; i < pairs.size(); ++i)
            {
                results[i] = pairs[i].second;
            }
        }

        void QueryNearest(const glm::vec3& queryPoint, std::size_t& result) const
        {
            result = std::numeric_limits<size_t>::max();
            if (NodeProperties.Empty())
            {
                return;
            }

            double minDistSq = std::numeric_limits<double>::max();

            using TraversalElement = std::pair<float, NodeIndex>;
            std::priority_queue<TraversalElement, std::vector<TraversalElement>, std::greater<>> pq;

            constexpr NodeIndex rootIndex = 0;
            const double rootDistSq = SquaredDistance(
                m_Nodes[rootIndex].Aabb, queryPoint);
            pq.emplace(rootDistSq, rootIndex);

            while (!pq.empty())
            {
                const float nodeDistSq = pq.top().first;
                const NodeIndex nodeIdx = pq.top().second;
                pq.pop();

                if (nodeDistSq >= minDistSq)
                {
                    break;
                }

                const Node& node = m_Nodes[nodeIdx];

                if (node.IsLeaf)
                {
                    // This is a leaf, so process its elements.
                    for (size_t i = 0; i < node.NumElements; ++i)
                    {
                        assert(node.FirstElement + i < m_ElementIndices.size());
                        const std::size_t elemIdx = m_ElementIndices[node.FirstElement + i];
                        assert(elemIdx < ElementAabbs.size());
                        const double elemDistSq = SquaredDistance(ElementAabbs[elemIdx], queryPoint);

                        if (elemDistSq < minDistSq)
                        {
                            minDistSq = elemDistSq;
                            result = elemIdx;
                        }
                    }
                }
                else
                {
                    for (size_t i = 0; i < node.NumStraddlers; ++i)
                    {
                        assert(node.FirstElement + i < m_ElementIndices.size());
                        const std::size_t elemIdx = m_ElementIndices[node.FirstElement + i];
                        assert(elemIdx < ElementAabbs.size());
                        const double elemDistSq = SquaredDistance(ElementAabbs[elemIdx], queryPoint);

                        if (elemDistSq < minDistSq)
                        {
                            minDistSq = elemDistSq;
                            result = elemIdx;
                        }
                    }
                    // This is an internal node, so traverse to its children.
                    for (const auto childIndex : node.Children)
                    {
                        if (childIndex != kInvalidIndex)
                        {
                            const double childDistSq = SquaredDistance(m_Nodes[childIndex].Aabb, queryPoint);
                            if (childDistSq < minDistSq)
                            {
                                pq.emplace(childDistSq, childIndex);
                            }
                        }
                    }
                }
            }
        }

        [[nodiscard]] bool ValidateStructure() const
        {
            if (NodeProperties.Empty()) return m_ElementIndices.empty();
            return ValidateNode(0);
        }

    private:
        [[nodiscard]] bool ValidateNode(NodeIndex nodeIdx) const
        {
            const Node& node = m_Nodes[nodeIdx];
            if (node.FirstElement > m_ElementIndices.size()) return false;
            if (node.FirstElement + node.NumElements > m_ElementIndices.size()) return false;

            if (node.IsLeaf)
            {
                return node.NumStraddlers == 0;
            }

            std::size_t accumulated = node.FirstElement + node.NumStraddlers;
            std::size_t childTotal = 0;
            for (const auto childIndex : node.Children)
            {
                if (childIndex == kInvalidIndex) continue;

                const Node& child = m_Nodes[childIndex];
                if (child.FirstElement != accumulated) return false;
                if (child.NumElements == 0) return false;
                if (child.FirstElement + child.NumElements > node.FirstElement + node.NumElements) return false;
                if (!ValidateNode(childIndex)) return false;

                accumulated += child.NumElements;
                childTotal += child.NumElements;
            }

            return accumulated == node.FirstElement + node.NumElements &&
                childTotal + node.NumStraddlers == node.NumElements;
        }

        NodeIndex CreateNode()
        {
            const auto idx = static_cast<NodeIndex>(m_Nodes.size());
            m_Nodes.emplace_back();
            return idx;
        }

        void SubdivideVolume(const NodeIndex nodeIdx, std::size_t depth, std::vector<size_t>& scratch)
        {
            AABB nodeAabb = m_Nodes[nodeIdx].Aabb;
            uint32_t firstElement = m_Nodes[nodeIdx].FirstElement;
            uint32_t numElements = m_Nodes[nodeIdx].NumElements;

            if (depth >= m_MaxBvhDepth || numElements <= m_MaxElementsPerNode)
            {
                m_Nodes[nodeIdx].IsLeaf = true;
                return;
            }

            glm::vec3 sp = ChooseSplitPoint(nodeIdx);

            //Jitter/tighten the split point when it hits data
            for (int ax = 0; ax < 3; ++ax)
            {
                const float lo = nodeAabb.Min[ax], hi = nodeAabb.Max[ax];
                float& s = sp[ax];
                if (s <= lo || s >= hi) s = 0.5f * (lo + hi);
                if (s == lo) s = std::nextafter(s, hi);
                else if (s == hi) s = std::nextafter(s, lo);
            }

            std::array<AABB, 8> octantAabbs;
            for (int j = 0; j < 8; ++j)
            {
                glm::vec3 childMin = {
                    (j & 1) ? sp[0] : nodeAabb.Min[0], (j & 2) ? sp[1] : nodeAabb.Min[1],
                    (j & 4) ? sp[2] : nodeAabb.Min[2]
                };
                glm::vec3 childMax = {
                    (j & 1) ? nodeAabb.Max[0] : sp[0], (j & 2) ? nodeAabb.Max[1] : sp[1],
                    (j & 4) ? nodeAabb.Max[2] : sp[2]
                };
                octantAabbs[j] = {.Min = childMin, .Max = childMax};
            }

            std::array<std::vector<size_t>, 8> childElements;
            for (auto& vec : childElements) vec.reserve(numElements / 8);
            scratch.clear();
            scratch.reserve(numElements);
            auto& straddlers = scratch;

            for (size_t i = 0; i < numElements; ++i)
            {
                std::size_t elemIdx = m_ElementIndices[firstElement + i];
                const auto& elemAabb = ElementAabbs[elemIdx];
                int foundChild = -1;

                if (elemAabb.Min == elemAabb.Max)
                {
                    const glm::vec3& p = elemAabb.Min;
                    // Element is a point. Directly assign it to one of the octants.
                    int code = 0;
                    code |= (p[0] >= sp[0]) ? 1 : 0;
                    code |= (p[1] >= sp[1]) ? 2 : 0;
                    code |= (p[2] >= sp[2]) ? 4 : 0;
                    childElements[code].push_back(elemIdx);
                }
                else
                {
                    for (int j = 0; j < 8; ++j)
                    {
                        if (Contains(octantAabbs[j], elemAabb))
                        {
                            if (foundChild == -1)
                            {
                                foundChild = j;
                            }
                            else
                            {
                                // The element is contained in more than one child box, which shouldn't happen with this logic.
                                // Treat as a straddler just in case of floating point issues.
                                foundChild = -1;
                                break;
                            }
                        }
                    }
                    if (foundChild != -1)
                    {
                        childElements[foundChild].push_back(elemIdx);
                    }
                    else
                    {
                        // Fallback: assign by center if we will tighten children;
                        // otherwise keep as straddler to preserve correctness.
                        if (m_SplitPolicy.TightChildren)
                        {
                            const glm::vec3 c = elemAabb.GetCenter();
                            int code = 0;
                            code |= (c[0] >= sp[0]) ? 1 : 0;
                            code |= (c[1] >= sp[1]) ? 2 : 0;
                            code |= (c[2] >= sp[2]) ? 4 : 0;
                            childElements[code].push_back(elemIdx);
                        }
                        else
                        {
                            straddlers.push_back(elemIdx);
                        }
                    }
                }
            }

            // If we couldn't push a significant number of elements down, it's better to stop and make this a leaf.
            // This prevents creating child nodes with very few elements.
            if (straddlers.size() == numElements)
            {
                m_Nodes[nodeIdx].IsLeaf = true;
                return;
            }

            // This node is now an internal node. It stores nothing itself.
            // Re-arrange the element_indices array.
            std::size_t currentPos = firstElement;
            // First, place all the straddlers
            for (size_t idx : straddlers)
            {
                m_ElementIndices[currentPos++] = idx;
            }
            // Then, place the elements for each child sequentially
            std::array<size_t, 8> childStarts{};
            for (int i = 0; i < 8; ++i)
            {
                childStarts[i] = currentPos;
                for (size_t idx : childElements[i])
                {
                    m_ElementIndices[currentPos++] = idx;
                }
            }

            // --- This node is now officially an internal node ---
            // Its 'firstElement' points to the start of the straddlers
            // Its 'numStraddlers' counts how many straddlers there are
            // Its 'numElements' counts all elements (straddlers + children)
            // Its children[] point to the new child nodes (created below)
            // We need to keep the straddlers at the start of the range for correct querying,
            // But we also still need to keep track of the total number of elements for early out. This is important!
            m_Nodes[nodeIdx].IsLeaf = false;
            m_Nodes[nodeIdx].NumStraddlers = straddlers.size();

            // Create children and recurse
            for (int i = 0; i < 8; ++i)
            {
                if (!childElements[i].empty())
                {
                    const auto childIndex = CreateNode();
                    m_Nodes[nodeIdx].Children[i] = childIndex;

                    Node& child = m_Nodes[childIndex];
                    child.FirstElement = childStarts[i];
                    child.NumElements = childElements[i].size();

                    if (m_SplitPolicy.TightChildren)
                    {
                        child.Aabb = TightChildAabb(childElements[i].begin(), childElements[i].end(),
                                                    m_SplitPolicy.Epsilon);
                    }
                    else
                    {
                        child.Aabb = octantAabbs[i];
                    }

                    SubdivideVolume(childIndex, depth + 1, scratch);
                }
            }
        }

        [[nodiscard]] glm::vec3 ComputeMeanCenter(size_t first, std::size_t size,
                                                  const glm::vec3& fallbackCenter) const
        {
            if (size == 0)
            {
                return fallbackCenter; // fallback; or pass nodeIdx and use aabbs[nodeIdx]
            }
            glm::vec3 acc(0.0f, 0.0f, 0.0f);
            for (size_t i = 0; i < size; ++i)
            {
                const auto idx = m_ElementIndices[first + i];
                acc += ElementAabbs[idx].GetCenter();
            }
            return acc / float(size);
        }

        [[nodiscard]] glm::vec3 ComputeMedianCenter(size_t first, std::size_t size,
                                                    const glm::vec3& fallbackCenter) const
        {
            if (size == 0)
            {
                return fallbackCenter; // fallback; or pass nodeIdx and use aabbs[nodeIdx]
            }
            std::vector<glm::vec3> centers;
            centers.reserve(size);
            for (size_t i = 0; i < size; ++i)
            {
                centers.push_back(ElementAabbs[m_ElementIndices[first + i]].GetCenter());
            }
            const auto medianIdx = centers.size() / 2;
            auto kth = [](std::vector<glm::vec3>& centers, std::size_t medianIdx, int dim)
            {
                std::ranges::nth_element(centers, centers.begin() + medianIdx,
                                         [dim](const auto& a, const auto& b) { return a[dim] < b[dim]; });
                return centers[medianIdx][dim];
            };
            return {kth(centers, medianIdx, 0), kth(centers, medianIdx, 1), kth(centers, medianIdx, 2)};
        }

        [[nodiscard]] glm::vec3 ChooseSplitPoint(NodeIndex nodeIdx) const
        {
            const auto& node = m_Nodes[nodeIdx];
            const glm::vec3 fallbackCenter = m_Nodes[nodeIdx].Aabb.GetCenter();
            switch (m_SplitPolicy.SplitPoint)
            {
            case SplitPoint::Mean: return ComputeMeanCenter(node.FirstElement, node.NumElements,
                                                            fallbackCenter);
            case SplitPoint::Median: return ComputeMedianCenter(node.FirstElement, node.NumElements,
                                                                fallbackCenter);
            case SplitPoint::Center:
            default: return fallbackCenter;
            }
        }

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
