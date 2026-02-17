module;

#include <algorithm>
#include <span>
#include <numeric>
#include <queue>
#include <functional>
#include <glm/glm.hpp>

module Geometry:Octree.Impl;
import :Octree;

namespace Geometry
{
    void Octree::QueryNearest(const glm::vec3& queryPoint, std::size_t& result) const
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

                            const double childDistSq = SquaredDistance(m_Nodes[childIndex].Aabb, queryPoint);
                            if (childDistSq < minDistSq)
                            {
                                pq.emplace(childDistSq, childIndex);
                            }

                            // Move to the next existing child in the contiguous block
                            childOffset++;
                        }
                    }
                }
            }
        }
    }

    void Octree::QueryKnn(const glm::vec3& queryPoint, std::size_t k, std::vector<size_t>& results) const
        {
            results.clear();
            if (m_Nodes.empty() || k == 0)
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

                                if (childIndex == kInvalidIndex) continue;
                                const float cd2 = d2Node(childIndex);
                                if (cd2 <= tau) pq.emplace(cd2, childIndex);

                                // Move to the next existing child in the contiguous block
                                childOffset++;
                            }
                        }
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

    bool Octree::BuildFromOwned(const SplitPolicy& policy, const std::size_t maxPerNode,
                                const std::size_t maxDepth)
    {
        if (ElementAabbs.empty())
        {
            return false;
        }

        m_SplitPolicy = policy;
        m_MaxElementsPerNode = maxPerNode;
        m_MaxBvhDepth = maxDepth;

        m_Nodes.clear();
        m_Nodes.reserve(ElementAabbs.size() / 4);
        NodeProperties.Clear(); // Clear previous state

        const std::size_t numElements = ElementAabbs.size();
        m_ElementIndices.resize(numElements);
        std::iota(m_ElementIndices.begin(), m_ElementIndices.end(), 0);

        // Create root node
        m_Nodes.emplace_back();
        m_Nodes[0].FirstElement = 0;
        m_Nodes[0].NumElements = static_cast<uint32_t>(numElements);
        m_Nodes[0].Aabb = Union(std::span<const AABB>(ElementAabbs));

        std::vector<size_t> localScratch;
        localScratch.reserve(m_ElementIndices.size());
        SubdivideVolume(0, 0, localScratch);

        NodeProperties.Resize(m_Nodes.size());

        return true;
    }

    bool Octree::ValidateNode(NodeIndex nodeIdx) const
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

                    if (childIndex == kInvalidIndex) continue;

                    const Node& child = m_Nodes[childIndex];
                    if (child.FirstElement != accumulated) return false;
                    if (child.NumElements == 0) return false;
                    if (child.FirstElement + child.NumElements > node.FirstElement + node.NumElements) return false;
                    if (!ValidateNode(childIndex)) return false;

                    accumulated += child.NumElements;
                    childTotal += child.NumElements;

                    // Move to the next existing child in the contiguous block
                    childOffset++;
                }
            }
        }

        return accumulated == node.FirstElement + node.NumElements &&
            childTotal + node.NumStraddlers == node.NumElements;
    }

    void Octree::SubdivideVolume(const NodeIndex nodeIdx, std::size_t depth, std::vector<size_t>& scratch)
    {
        // 1. Capture Parent Data
        // We copy values because m_Nodes.resize() later might invalidate references.
        AABB nodeAabb = m_Nodes[nodeIdx].Aabb;
        uint32_t firstElement = m_Nodes[nodeIdx].FirstElement;
        uint32_t numElements = m_Nodes[nodeIdx].NumElements;

        // 2. Recursion Termination Check
        if (depth >= m_MaxBvhDepth || numElements <= m_MaxElementsPerNode)
        {
            m_Nodes[nodeIdx].IsLeaf = true;
            return;
        }

        // 3. Calculate Split Point
        glm::vec3 sp = ChooseSplitPoint(nodeIdx);

        // Jitter/tighten the split point to avoid slicing empty space or coincident geometry
        for (int ax = 0; ax < 3; ++ax)
        {
            const float lo = nodeAabb.Min[ax], hi = nodeAabb.Max[ax];
            float& s = sp[ax];
            if (s <= lo || s >= hi) s = 0.5f * (lo + hi);
            if (s == lo) s = std::nextafter(s, hi);
            else if (s == hi) s = std::nextafter(s, lo);
        }

        // 4. Define Octant Bounds
        std::array<AABB, 8> octantAabbs;
        for (int j = 0; j < 8; ++j)
        {
            glm::vec3 childMin = {
                (j & 1) ? sp[0] : nodeAabb.Min[0],
                (j & 2) ? sp[1] : nodeAabb.Min[1],
                (j & 4) ? sp[2] : nodeAabb.Min[2]
            };
            glm::vec3 childMax = {
                (j & 1) ? nodeAabb.Max[0] : sp[0],
                (j & 2) ? nodeAabb.Max[1] : sp[1],
                (j & 4) ? nodeAabb.Max[2] : sp[2]
            };
            octantAabbs[j] = {.Min = childMin, .Max = childMax};
        }

        // 5. Bucket Elements
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
                // Point optimization
                const glm::vec3& p = elemAabb.Min;
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
                            // Overlaps multiple octants -> Straddler
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
                    // Fallback strategy: loose fit or straddler
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

        // 6. Check Efficiency
        // If everything straddles, we can't subdivide further. Make leaf.
        if (straddlers.size() == numElements)
        {
            m_Nodes[nodeIdx].IsLeaf = true;
            return;
        }

        // 7. Calculate Sparse Mask & Allocation Requirements
        uint8_t mask = 0;
        uint32_t childrenNeeded = 0;
        for (int i = 0; i < 8; ++i)
        {
            if (!childElements[i].empty())
            {
                mask |= (1 << i);
                childrenNeeded++;
            }
        }

        if (childrenNeeded == 0)
        {
            m_Nodes[nodeIdx].IsLeaf = true;
            return;
        }

        // 8. Allocate Contiguous Children
        const NodeIndex baseChildIdx = static_cast<NodeIndex>(m_Nodes.size());
        // Resize vector. WARNING: This invalidates pointers/references to existing nodes!
        // We must use indices to access nodes from here on.
        m_Nodes.resize(m_Nodes.size() + childrenNeeded);

        // 9. Update Parent Node
        {
            Node& node = m_Nodes[nodeIdx]; // Re-fetch reference after resize
            node.IsLeaf = false;
            node.NumStraddlers = static_cast<uint32_t>(straddlers.size());
            node.ChildMask = mask;
            node.BaseChildIndex = baseChildIdx;
            // node.NumElements includes straddlers + children elements (recursive total).
            // We leave it as total count for early culling optimization.
        }

        // 10. Reorder Index Buffer (Straddlers -> Child 0 -> Child 1 ...)
        // We repack the elements in-place within the global m_ElementIndices vector.
        std::size_t currentPos = firstElement;

        // A. Place Straddlers first
        for (size_t idx : straddlers)
        {
            m_ElementIndices[currentPos++] = idx;
        }

        // B. Initialize Children and Recurse
        uint32_t childOffset = 0;
        for (int i = 0; i < 8; ++i)
        {
            if (mask & (1 << i))
            {
                const NodeIndex currentChildIdx = baseChildIdx + childOffset;
                Node& child = m_Nodes[currentChildIdx];

                // Assign Range
                child.FirstElement = static_cast<uint32_t>(currentPos);
                child.NumElements = static_cast<uint32_t>(childElements[i].size());

                // Fill Buffer
                for (size_t idx : childElements[i])
                {
                    m_ElementIndices[currentPos++] = idx;
                }

                // Compute Bounds
                if (m_SplitPolicy.TightChildren)
                {
                    child.Aabb = TightChildAabb(childElements[i].begin(), childElements[i].end(),
                                                m_SplitPolicy.Epsilon);
                }
                else
                {
                    child.Aabb = octantAabbs[i];
                }

                // Recurse (Depth First)
                SubdivideVolume(currentChildIdx, depth + 1, scratch);

                childOffset++;
            }
        }
    }

    glm::vec3 Octree::ComputeMeanCenter(size_t first, std::size_t size,
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

        glm::vec3 Octree::ComputeMedianCenter(size_t first, std::size_t size,
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
            const auto medianIdx = static_cast<std::ptrdiff_t>(centers.size() / 2);
            auto kth = [](std::vector<glm::vec3>& centers, std::ptrdiff_t medianIdx, int dim)
            {
                std::ranges::nth_element(centers, centers.begin() + medianIdx,
                                         [dim](const auto& a, const auto& b) { return a[dim] < b[dim]; });
                return centers[static_cast<size_t>(medianIdx)][dim];
            };
            return {kth(centers, medianIdx, 0), kth(centers, medianIdx, 1), kth(centers, medianIdx, 2)};
        }

        glm::vec3 Octree::ChooseSplitPoint(NodeIndex nodeIdx) const
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
}
