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
        struct Node
        {
            AABB Aabb;
            std::size_t FirstElement = std::numeric_limits<size_t>::max();
            std::size_t NumStraddlers = 0; // number of elements that straddle child node boundaries
            std::size_t NumElements = 0;
            // total number of elements in this node (including straddlers).Necessary for early out in queries
            std::array<size_t, 8> Children{};
            bool IsLeaf = true;

            Node()
            {
                Children.fill(NodeHandle().Index);
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

        Nodes NodeProperties;
        NodeProperty<Node> Nodes;

        std::span<const AABB> ElementAabbs;

        template <class T>
        [[nodiscard]] NodeProperty<T> AddNodeProperty(const std::string& name, T default_value = T())
        {
            return NodeProperty<T>(NodeProperties.Add<T>(name, default_value));
        }

        template <class T>
        [[nodiscard]] NodeProperty<T> GetNodeProperty(const std::string& name) const
        {
            return NodeProperty<T>(NodeProperties.Get<T>(name));
        }

        template <class T>
        [[nodiscard]] NodeProperty<T> GetOrAddNodeProperty(const std::string& name, T default_value = T())
        {
            return NodeProperty<T>(NodeProperties.GetOrAdd<T>(name, default_value));
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

        [[nodiscard]] const std::vector<size_t>& GetElementIndices() const noexcept
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

            NodeProperties.Clear(); // Clear previous state
            const std::size_t num_elements = ElementAabbs.size();

            if (num_elements == 0)
            {
                m_ElementIndices.clear();
                return false;
            }

            m_ElementIndices.resize(num_elements);
            std::iota(m_ElementIndices.begin(), m_ElementIndices.end(), 0);

            Nodes = AddNodeProperty<Node>("n:nodes");

            // Create root node
            const auto root_idx = CreateNode();
            Nodes[root_idx].FirstElement = 0;
            Nodes[root_idx].NumElements = num_elements;
            Nodes[root_idx].Aabb = Union(ElementAabbs);

            SubdivideVolume(root_idx, 0);
            return true;
        }

        void QueryRay(const Ray& query_shape, std::vector<size_t>& result) const
        {
            Query<Ray>(query_shape, result);
        }

        void QueryAABB(const AABB& query_shape, std::vector<size_t>& out) const
        {
            Query<AABB>(query_shape, out);
        }

        void QuerySphere(const Sphere& query_shape, std::vector<size_t>& out) const
        {
            Query<Sphere>(query_shape, out);
        }

        template <VolumetricSpatialQueryShape Shape>
        void Query(const Shape& query_shape, std::vector<size_t>& result) const
        {
            result.clear();
            if (NodeProperties.Empty())
            {
                return;
            }

            const auto& nodeData = Nodes.Handle().Vector();
            const Node* nodePtr = nodeData.data(); // Raw pointer for speed

            constexpr double eps = 0.0; // set to a small positive tolerance if you want numerical slack
            const auto query_volume = static_cast<double>(Volume(query_shape));

            // Use a small local stack to avoid heap allocation for the stack itself if possible,
            // though std::vector is fine given the depth is low (10).
            // Optimization: Use a fixed array stack since MaxDepth is known/limited.
            std::array<size_t, 64> stack{}; // Depth 10 * 8 children < 64? No, but stack depth is roughly depth*7 in worst case?
            // Actually for DFS, stack size is proportional to Depth. 64 is plenty for depth 10.
            int stackTop = 0;
            stack[stackTop++] = 0; // Push Root (Index 0)

            while (stackTop > 0)
            {
                const size_t node_idx = stack[--stackTop];
                const Node& node = nodePtr[node_idx];

                if (!TestOverlap(node.Aabb, query_shape)) continue;

                const double node_volume = node.Aabb.GetVolume();
                const double strictly_larger = (query_volume > node_volume + eps);

                if (strictly_larger && Contains(query_shape, node.Aabb))
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
                    if (TestOverlap(ElementAabbs[ei], query_shape))
                    {
                        result.push_back(ei);
                    }
                }

                if (!node.IsLeaf)
                {
                    for (const auto ci : node.Children)
                    {
                        // Check validity via index directly
                        if (ci != kInvalidIndex)
                        {
                            // Optimization: Check Child AABB here before pushing to stack?
                            // The original code pushed then checked.
                            // Checking here saves a stack push/pop cycle.
                            if (TestOverlap(nodePtr[ci].Aabb, query_shape))
                            {
                                stack[stackTop++] = ci;
                            }
                        }
                    }
                }
            }
        }

        template <SpatialQueryShape Shape>
        void Query(const Shape& query_shape, std::vector<size_t>& result) const
            requires (!VolumetricSpatialQueryShape<Shape>)
        {
            result.clear();
            if (NodeProperties.Empty())
            {
                return;
            }

            std::vector<NodeHandle> stack{NodeHandle{0}};
            while (!stack.empty())
            {
                const NodeHandle node_idx = stack.back();
                stack.pop_back();
                const Node& node = Nodes[node_idx];

                if (!TestOverlap(node.Aabb, query_shape)) continue;

                if (node.IsLeaf)
                {
                    for (size_t i = 0; i < node.NumElements; ++i)
                    {
                        std::size_t ei = m_ElementIndices[node.FirstElement + i];
                        if (TestOverlap(ElementAabbs[ei], query_shape))
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
                        if (TestOverlap(ElementAabbs[ei], query_shape))
                        {
                            result.push_back(ei);
                        }
                    }
                    for (const auto ci : node.Children)
                    {
                        auto nhci = NodeHandle(ci);
                        if (nhci.IsValid() && TestOverlap(Nodes[nhci].Aabb, query_shape))
                        {
                            stack.push_back(nhci);
                        }
                    }
                }
            }
        }

        void QueryKnn(const glm::vec3& query_point, std::size_t k, std::vector<size_t>& results) const
        {
            results.clear();
            if (NodeProperties.Empty() || k == 0)
            {
                return;
            }

            using QueueElement = std::pair<float, std::size_t>;
            Utils::BoundedHeap<QueueElement> heap(k);

            using Trav = std::pair<float, NodeHandle>; // (node lower-bound d2, node index)
            std::priority_queue<Trav, std::vector<Trav>, std::greater<>> pq;

            constexpr NodeHandle root(0);
            auto d2_node = [&](NodeHandle ni)
            {
                return static_cast<float>(SquaredDistance(Nodes[ni].Aabb, query_point));
            };
            auto d2_elem = [&](size_t ei)
            {
                return static_cast<float>(SquaredDistance(ElementAabbs[ei], query_point));
            };

            pq.emplace(d2_node(root), root);
            float tau = std::numeric_limits<float>::infinity();
            auto update_tau = [&]()
            {
                tau = (heap.Size() == k) ? heap.top().first : std::numeric_limits<float>::infinity();
            };

            while (!pq.empty())
            {
                auto [nd2, ni] = pq.top();
                pq.pop();

                // Global prune: the best remaining node is already worse than our kth best.
                if (heap.Size() == k && nd2 > tau) break;

                const Node& node = Nodes[ni];

                if (node.IsLeaf)
                {
                    for (size_t i = 0; i < node.NumElements; ++i)
                    {
                        const std::size_t ei = m_ElementIndices[node.FirstElement + i];
                        const QueueElement candidate{d2_elem(ei), ei};
                        if (heap.Size() < k || candidate < heap.top())
                        {
                            heap.Push(candidate);
                            update_tau();
                        }
                    }
                }
                else
                {
                    // Score straddlers at this node
                    for (size_t i = 0; i < node.NumStraddlers; ++i)
                    {
                        const std::size_t ei = m_ElementIndices[node.FirstElement + i];
                        const QueueElement candidate{d2_elem(ei), ei};
                        if (heap.Size() < k || candidate < heap.top())
                        {
                            heap.Push(candidate);
                            update_tau();
                        }
                    }
                    // Push children best-first, pruned by current tau
                    for (const auto ci : node.Children)
                    {
                        const auto nhci = NodeHandle(ci);
                        if (!nhci.IsValid()) continue;
                        const float cd2 = d2_node(nhci);
                        if (cd2 <= tau) pq.emplace(cd2, ci);
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

        void QueryNearest(const glm::vec3& query_point, std::size_t& result) const
        {
            result = std::numeric_limits<size_t>::max();
            if (NodeProperties.Empty())
            {
                return;
            }

            double min_dist_sq = std::numeric_limits<double>::max();

            using TraversalElement = std::pair<float, NodeHandle>;
            std::priority_queue<TraversalElement, std::vector<TraversalElement>, std::greater<>> pq;

            constexpr auto root_idx = NodeHandle(0);
            const double root_dist_sq = SquaredDistance(
                Nodes[root_idx].Aabb, query_point);
            pq.emplace(root_dist_sq, root_idx);

            while (!pq.empty())
            {
                const float node_dist_sq = pq.top().first;
                const NodeHandle node_idx = pq.top().second;
                pq.pop();

                if (node_dist_sq >= min_dist_sq)
                {
                    break;
                }

                const Node& node = Nodes[node_idx];

                if (node.IsLeaf)
                {
                    // This is a leaf, so process its elements.
                    for (size_t i = 0; i < node.NumElements; ++i)
                    {
                        assert(node.FirstElement + i < m_ElementIndices.size());
                        const std::size_t elem_idx = m_ElementIndices[node.FirstElement + i];
                        assert(elem_idx < ElementAabbs.size());
                        const double elem_dist_sq = SquaredDistance(ElementAabbs[elem_idx], query_point);

                        if (elem_dist_sq < min_dist_sq)
                        {
                            min_dist_sq = elem_dist_sq;
                            result = elem_idx;
                        }
                    }
                }
                else
                {
                    for (size_t i = 0; i < node.NumStraddlers; ++i)
                    {
                        assert(node.FirstElement + i < m_ElementIndices.size());
                        const std::size_t elem_idx = m_ElementIndices[node.FirstElement + i];
                        assert(elem_idx < ElementAabbs.size());
                        const double elem_dist_sq = SquaredDistance(ElementAabbs[elem_idx], query_point);

                        if (elem_dist_sq < min_dist_sq)
                        {
                            min_dist_sq = elem_dist_sq;
                            result = elem_idx;
                        }
                    }
                    // This is an internal node, so traverse to its children.
                    for (const auto child_idx : node.Children)
                    {
                        const auto nhci = NodeHandle(child_idx);
                        if (nhci.IsValid())
                        {
                            const double child_dist_sq = SquaredDistance(Nodes[nhci].Aabb, query_point);
                            if (child_dist_sq < min_dist_sq)
                            {
                                pq.emplace(child_dist_sq, child_idx);
                            }
                        }
                    }
                }
            }
        }

        [[nodiscard]] bool ValidateStructure() const
        {
            if (NodeProperties.Empty()) return m_ElementIndices.empty();
            return ValidateNode(NodeHandle{0});
        }

    private:
        [[nodiscard]] bool ValidateNode(NodeHandle node_idx) const
        {
            const Node& node = Nodes[node_idx];
            if (node.FirstElement > m_ElementIndices.size()) return false;
            if (node.FirstElement + node.NumElements > m_ElementIndices.size()) return false;

            if (node.IsLeaf)
            {
                return node.NumStraddlers == 0;
            }

            std::size_t accumulated = node.FirstElement + node.NumStraddlers;
            std::size_t child_total = 0;
            for (const auto ci : node.Children)
            {
                const auto nhci = NodeHandle(ci);
                if (!nhci.IsValid()) continue;

                const Node& child = Nodes[nhci];
                if (child.FirstElement != accumulated) return false;
                if (child.NumElements == 0) return false;
                if (child.FirstElement + child.NumElements > node.FirstElement + node.NumElements) return false;
                if (!ValidateNode(nhci)) return false;

                accumulated += child.NumElements;
                child_total += child.NumElements;
            }

            return accumulated == node.FirstElement + node.NumElements &&
                child_total + node.NumStraddlers == node.NumElements;
        }

        NodeHandle CreateNode()
        {
            size_t node_idx = NodeProperties.Size();
            NodeProperties.PushBack();
            return NodeHandle(node_idx);
        }

        void SubdivideVolume(const NodeHandle node_idx, std::size_t depth)
        {
            const Node& node = Nodes[node_idx]; // We'll be modifying the node

            if (depth >= m_MaxBvhDepth || node.NumElements <= m_MaxElementsPerNode)
            {
                Nodes[node_idx].IsLeaf = true;
                return;
            }

            glm::vec3 sp = ChooseSplitPoint(node_idx);

            //Jitter/tighten the split point when it hits data
            for (int ax = 0; ax < 3; ++ax)
            {
                const float lo = node.Aabb.Min[ax], hi = node.Aabb.Max[ax];
                float& s = sp[ax];
                if (s <= lo || s >= hi) s = 0.5f * (lo + hi);
                if (s == lo) s = std::nextafter(s, hi);
                else if (s == hi) s = std::nextafter(s, lo);
            }

            std::array<AABB, 8> octant_aabbs;
            for (int j = 0; j < 8; ++j)
            {
                glm::vec3 child_min = {
                    (j & 1) ? sp[0] : node.Aabb.Min[0], (j & 2) ? sp[1] : node.Aabb.Min[1],
                    (j & 4) ? sp[2] : node.Aabb.Min[2]
                };
                glm::vec3 child_max = {
                    (j & 1) ? node.Aabb.Max[0] : sp[0], (j & 2) ? node.Aabb.Max[1] : sp[1],
                    (j & 4) ? node.Aabb.Max[2] : sp[2]
                };
                octant_aabbs[j] = {.Min = child_min, .Max = child_max};
            }

            std::array<std::vector<size_t>, 8> child_elements;
            m_ScratchIndices.clear();
            m_ScratchIndices.reserve(node.NumElements);
            auto& straddlers = m_ScratchIndices;

            for (size_t i = 0; i < node.NumElements; ++i)
            {
                std::size_t elem_idx = m_ElementIndices[node.FirstElement + i];
                const auto& elem_aabb = ElementAabbs[elem_idx];
                int found_child = -1;

                if (elem_aabb.Min == elem_aabb.Max)
                {
                    const glm::vec3& p = elem_aabb.Min;
                    // Element is a point. Directly assign it to one of the octants.
                    int code = 0;
                    code |= (p[0] >= sp[0]) ? 1 : 0;
                    code |= (p[1] >= sp[1]) ? 2 : 0;
                    code |= (p[2] >= sp[2]) ? 4 : 0;
                    child_elements[code].push_back(elem_idx);
                }
                else
                {
                    for (int j = 0; j < 8; ++j)
                    {
                        if (Contains(octant_aabbs[j], elem_aabb))
                        {
                            if (found_child == -1)
                            {
                                found_child = j;
                            }
                            else
                            {
                                // The element is contained in more than one child box, which shouldn't happen with this logic.
                                // Treat as a straddler just in case of floating point issues.
                                found_child = -1;
                                break;
                            }
                        }
                    }
                    if (found_child != -1)
                    {
                        child_elements[found_child].push_back(elem_idx);
                    }
                    else
                    {
                        // Fallback: assign by center if we will tighten children;
                        // otherwise keep as straddler to preserve correctness.
                        if (m_SplitPolicy.TightChildren)
                        {
                            const glm::vec3 c = elem_aabb.GetCenter();
                            int code = 0;
                            code |= (c[0] >= sp[0]) ? 1 : 0;
                            code |= (c[1] >= sp[1]) ? 2 : 0;
                            code |= (c[2] >= sp[2]) ? 4 : 0;
                            child_elements[code].push_back(elem_idx);
                        }
                        else
                        {
                            straddlers.push_back(elem_idx);
                        }
                    }
                }
            }

            // If we couldn't push a significant number of elements down, it's better to stop and make this a leaf.
            // This prevents creating child nodes with very few elements.
            if (straddlers.size() == node.NumElements)
            {
                Nodes[node_idx].IsLeaf = true;
                return;
            }

            // This node is now an internal node. It stores nothing itself.
            // Re-arrange the element_indices array.
            std::size_t current_pos = node.FirstElement;
            // First, place all the straddlers
            for (size_t idx : straddlers)
            {
                m_ElementIndices[current_pos++] = idx;
            }
            // Then, place the elements for each child sequentially
            std::array<size_t, 8> child_starts{};
            for (int i = 0; i < 8; ++i)
            {
                child_starts[i] = current_pos;
                for (size_t idx : child_elements[i])
                {
                    m_ElementIndices[current_pos++] = idx;
                }
            }

            // --- This node is now officially an internal node ---
            // Its 'first_element' points to the start of the straddlers
            // Its 'num_straddlers' counts how many straddlers there are
            // Its 'num_elements' counts all elements (straddlers + children)
            // Its children[] point to the new child nodes (created below)
            // We need to keep the straddlers at the start of the range for correct querying,
            // But we also still need to keep track of the total number of elements for early out. This is important!
            Nodes[node_idx].IsLeaf = false;
            Nodes[node_idx].NumStraddlers = straddlers.size();

            // Create children and recurse
            for (int i = 0; i < 8; ++i)
            {
                if (!child_elements[i].empty())
                {
                    const auto child_node_handle = CreateNode();
                    Nodes[node_idx].Children[i] = child_node_handle.Index;

                    Node& child = Nodes[child_node_handle];
                    child.FirstElement = child_starts[i];
                    child.NumElements = child_elements[i].size();

                    if (m_SplitPolicy.TightChildren)
                    {
                        child.Aabb = TightChildAabb(child_elements[i].begin(), child_elements[i].end(),
                                                    m_SplitPolicy.Epsilon);
                    }
                    else
                    {
                        child.Aabb = octant_aabbs[i];
                    }

                    SubdivideVolume(child_node_handle, depth + 1);
                }
            }
        }

        [[nodiscard]] glm::vec3 ComputeMeanCenter(size_t first, std::size_t size,
                                                  const glm::vec3& fallback_center) const
        {
            if (size == 0)
            {
                return fallback_center; // fallback; or pass node_idx and use aabbs[node_idx]
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
                                                    const glm::vec3& fallback_center) const
        {
            if (size == 0)
            {
                return fallback_center; // fallback; or pass node_idx and use aabbs[node_idx]
            }
            std::vector<glm::vec3> centers;
            centers.reserve(size);
            for (size_t i = 0; i < size; ++i)
            {
                centers.push_back(ElementAabbs[m_ElementIndices[first + i]].GetCenter());
            }
            const auto median_idx = centers.size() / 2;
            auto kth = [](std::vector<glm::vec3>& centers, std::size_t median_idx, int dim)
            {
                std::ranges::nth_element(centers, centers.begin() + median_idx,
                                         [dim](const auto& a, const auto& b) { return a[dim] < b[dim]; });
                return centers[median_idx][dim];
            };
            return {kth(centers, median_idx, 0), kth(centers, median_idx, 1), kth(centers, median_idx, 2)};
        }

        [[nodiscard]] glm::vec3 ChooseSplitPoint(NodeHandle node_idx) const
        {
            const auto& node = Nodes[node_idx];
            const glm::vec3 fallback_center = Nodes[node_idx].Aabb.GetCenter();
            switch (m_SplitPolicy.SplitPoint)
            {
            case SplitPoint::Mean: return ComputeMeanCenter(node.FirstElement, node.NumElements,
                                                            fallback_center);
            case SplitPoint::Median: return ComputeMedianCenter(node.FirstElement, node.NumElements,
                                                                fallback_center);
            case SplitPoint::Center:
            default: return fallback_center;
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
        std::vector<size_t> m_ElementIndices;
        std::vector<size_t> m_ScratchIndices;
    };
}
