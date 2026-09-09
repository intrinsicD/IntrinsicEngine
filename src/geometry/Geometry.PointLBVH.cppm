// Deterministic point LBVH and CPU query oracle for reusable spatial searches.
module;
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include <span>
#include <vector>
export module Geometry.PointLBVH;

export namespace Geometry::PointLBVH
{
    constexpr std::uint32_t InvalidIndex = ~0u;
    // Bounds keep squared-distance arithmetic finite for both CPU and float GPU kernels.
    constexpr float CoordinateLimit = 1.0e18f;
    struct Neighbor
    {
        std::uint32_t Index{InvalidIndex};
        float SquaredDistance{std::numeric_limits<float>::infinity()};
    };
    struct RadiusResult
    {
        std::vector<Neighbor> Neighbors{};
        std::uint32_t TotalCount{};
        [[nodiscard]] bool Overflowed() const noexcept
        {
            return TotalCount > Neighbors.size();
        }
    };
    struct Node
    {
        glm::vec3 Min{};
        std::uint32_t Left{InvalidIndex};
        glm::vec3 Max{};
        std::uint32_t Right{InvalidIndex};
        std::uint32_t Object{InvalidIndex};
        std::uint32_t First{}, Last{}, Reserved{};
    };
    [[nodiscard]] bool ValidPoint(glm::vec3 point) noexcept;
    [[nodiscard]] Neighbor NearestReference(std::span<const glm::vec3> points, glm::vec3 query,
                                             std::uint32_t excludedIndex = InvalidIndex);
    [[nodiscard]] RadiusResult RadiusReference(std::span<const glm::vec3> points, glm::vec3 query,
                                               float radius, std::uint32_t capacity,
                                               std::uint32_t excludedIndex = InvalidIndex);
    // Ascending (squared distance,index); exclusion removes an ID, never coincident peers.
    // k=0 returns empty; k larger than membership returns all eligible points.
    [[nodiscard]] std::vector<Neighbor> KNearestReference(
        std::span<const glm::vec3> points, glm::vec3 query, std::uint32_t k,
        std::uint32_t excludedIndex = InvalidIndex);
    class Index
    {
      public:
        // Failure clears the old index. Coordinates and queries use the caller's declared index space.
        [[nodiscard]] bool Build(std::span<const glm::vec3> points);
        [[nodiscard]] Neighbor Nearest(glm::vec3 query,
                                        std::uint32_t excludedIndex = InvalidIndex) const;
        [[nodiscard]] RadiusResult Radius(glm::vec3 query, float radius,
                                          std::uint32_t capacity,
                                          std::uint32_t excludedIndex = InvalidIndex) const;
        [[nodiscard]] std::vector<Neighbor> KNearest(
            glm::vec3 query, std::uint32_t k, std::uint32_t excludedIndex = InvalidIndex) const;
        [[nodiscard]] std::span<const Node> Nodes() const noexcept
        {
            return m_Nodes;
        }
        [[nodiscard]] std::span<const glm::vec3> Points() const noexcept
        {
            return m_Points;
        }

      private:
        std::vector<glm::vec3> m_Points{};
        std::vector<Node> m_Nodes{};
    };
} // namespace Geometry::PointLBVH
