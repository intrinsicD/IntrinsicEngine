module;

#include <limits>
#include <cstdint>
#include <span>
#include <vector>
#include <string>
#include <glm/glm.hpp>

export module Geometry.Octree;

import Geometry.Properties;
import Geometry.AABB;
import Geometry.Primitives;
import Geometry.SpatialQueries;

export namespace Geometry
{
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

        [[nodiscard]] bool Build(std::span<const AABB> aabbs, const SplitPolicy& policy, std::size_t maxPerNode,
                                 std::size_t maxDepth);
        [[nodiscard]] bool Build(std::vector<AABB>&& aabbs, const SplitPolicy& policy, std::size_t maxPerNode,
                                 std::size_t maxDepth);
        [[nodiscard]] bool BuildFromPoints(std::span<const glm::vec3> points, const SplitPolicy& policy,
                                           std::size_t maxPerNode, std::size_t maxDepth);

        void QueryRay(const Ray& queryShape, std::vector<size_t>& out) const;
        void QueryAABB(const AABB& queryShape, std::vector<size_t>& out) const;
        void QuerySphere(const Sphere& queryShape, std::vector<size_t>& out) const;

        void QueryKNN(const glm::vec3& queryPoint, std::size_t k, std::vector<size_t>& out) const;

        void QueryNearest(const glm::vec3& queryPoint, std::size_t& out) const;

        [[nodiscard]] bool ValidateStructure() const;

    private:
        [[nodiscard]] bool BuildFromOwned(const SplitPolicy& policy, const std::size_t maxPerNode,
                                          const std::size_t maxDepth);

        [[nodiscard]] bool ValidateNode(NodeIndex nodeIdx) const;

        NodeIndex CreateNode();

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
