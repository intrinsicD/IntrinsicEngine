module;
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <numeric>
#include <span>
#include <vector>
module Geometry.PointLBVH;

namespace Geometry::PointLBVH
{
    namespace
    {
        float Distance(glm::vec3 a, glm::vec3 b)
        {
            const auto d = a - b;
            return (d.x * d.x + d.y * d.y) + d.z * d.z;
        }
        std::uint32_t Spread(std::uint32_t x)
        {
            x = (x | (x << 16u)) & 0x030000FFu;
            x = (x | (x << 8u)) & 0x0300F00Fu;
            x = (x | (x << 4u)) & 0x030C30C3u;
            return (x | (x << 2u)) & 0x09249249u;
        }
        bool Better(Neighbor a, Neighbor b)
        {
            return a.SquaredDistance < b.SquaredDistance ||
                   (a.SquaredDistance == b.SquaredDistance && a.Index < b.Index);
        }
        template <typename Visit>
        void Traverse(std::span<const Node> nodes, glm::vec3 query, float& limit, Visit visit)
        {
            if (nodes.empty() || !ValidPoint(query))
                return;
            // A radix tree over (30-bit Morton,32-bit index) has depth at most 62.
            std::array<std::uint32_t, 64> stack{};
            std::uint32_t size = 1;
            while (size)
            {
                const auto& node = nodes[stack[--size]];
                if (Distance(query, glm::clamp(query, node.Min, node.Max)) > limit)
                    continue;
                if (node.Object != InvalidIndex)
                    visit(node.Object);
                else
                {
                    stack[size++] = node.Right;
                    stack[size++] = node.Left;
                }
            }
        }
    } // namespace
    bool ValidPoint(glm::vec3 p) noexcept
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
               glm::all(glm::lessThanEqual(glm::abs(p), glm::vec3(CoordinateLimit)));
    }
    Neighbor NearestReference(std::span<const glm::vec3> points, glm::vec3 query,
                              std::uint32_t excludedIndex)
    {
        Neighbor best;
        if (!ValidPoint(query))
            return best;
        for (std::uint32_t i = 0; i < points.size(); ++i)
        {
            if (!ValidPoint(points[i]))
                return {};
            if (i == excludedIndex) continue;
            Neighbor candidate{i, Distance(points[i], query)};
            if (Better(candidate, best))
                best = candidate;
        }
        return best;
    }
    RadiusResult RadiusReference(std::span<const glm::vec3> points, glm::vec3 query, float radius,
                                 std::uint32_t capacity, std::uint32_t excludedIndex)
    {
        RadiusResult result;
        if (!ValidPoint(query) || !std::isfinite(radius) || radius < 0 || radius > CoordinateLimit)
            return result;
        for (std::uint32_t i = 0; i < points.size(); ++i)
        {
            if (!ValidPoint(points[i]))
                return {};
            if (i == excludedIndex) continue;
            const float d = Distance(points[i], query);
            if (d > radius * radius)
                continue;
            ++result.TotalCount;
            if (result.Neighbors.size() < capacity)
                result.Neighbors.push_back({i, d});
        }
        return result;
    }
    std::vector<Neighbor> KNearestReference(std::span<const glm::vec3> points,
                                            glm::vec3 query, std::uint32_t k,
                                            std::uint32_t excludedIndex)
    {
        std::vector<Neighbor> result;
        if (!ValidPoint(query) || k == 0) return result;
        for (std::uint32_t i = 0; i < points.size(); ++i)
        {
            if (!ValidPoint(points[i])) return {};
            if (i != excludedIndex) result.push_back({i, Distance(points[i], query)});
        }
        std::ranges::sort(result, Better);
        if (result.size() > k) result.resize(k);
        return result;
    }
    bool Index::Build(std::span<const glm::vec3> points)
    {
        if (points.size() > (1u << 24u) || !std::ranges::all_of(points, ValidPoint))
        {
            m_Points.clear();
            m_Nodes.clear();
            return false;
        }
        // A caller may rebuild from a subspan of this index's current snapshot.
        m_Points = std::vector<glm::vec3>(points.begin(), points.end());
        m_Nodes.clear();
        points = m_Points;
        if (points.empty())
            return true;
        const int n = static_cast<int>(points.size());
        glm::vec3 lo = points[0], hi = lo;
        for (auto p : points)
        {
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        std::vector<std::uint64_t> keys;
        keys.reserve(points.size());
        for (std::uint32_t i = 0; i < points.size(); ++i)
        {
            glm::uvec3 q{};
            for (int a = 0; a < 3; ++a)
                q[a] = hi[a] > lo[a]
                           ? static_cast<std::uint32_t>(std::clamp(
                                 (points[i][a] - lo[a]) / (hi[a] - lo[a]) * 1024.0f, 0.0f, 1023.0f))
                           : 0u;
            const auto code = (Spread(q.x) << 2u) | (Spread(q.y) << 1u) | Spread(q.z);
            keys.push_back((std::uint64_t(code) << 32u) | i);
        }
        std::ranges::sort(keys);
        m_Nodes.resize(2u * points.size() - 1u);
        for (int i = 0; i < n; ++i)
        {
            const auto id = static_cast<std::uint32_t>(keys[i]);
            m_Nodes[n - 1 + i] = {.Min = points[id],
                                  .Max = points[id],
                                  .Object = id,
                                  .First = static_cast<std::uint32_t>(i),
                                  .Last = static_cast<std::uint32_t>(i)};
        }
        auto prefix = [&](int i, int j) {
            return j < 0 || j >= n ? -1 : std::countl_zero(keys[i] ^ keys[j]);
        };
        for (int i = 0; i < n - 1; ++i)
        {
            const int d = prefix(i, i + 1) > prefix(i, i - 1) ? 1 : -1;
            const int minimum = prefix(i, i - d);
            int maximum = 2;
            while (prefix(i, i + maximum * d) > minimum)
                maximum *= 2;
            int length = 0;
            for (int step = maximum / 2; step; step /= 2)
                if (prefix(i, i + (length + step) * d) > minimum)
                    length += step;
            const int j = i + length * d;
            const int first = std::min(i, j), last = std::max(i, j);
            const int common = prefix(first, last);
            int split = first, step = last - first;
            do
            {
                step = (step + 1) / 2;
                const int candidate = split + step;
                if (candidate < last && prefix(first, candidate) > common)
                    split = candidate;
            } while (step > 1);
            auto& node = m_Nodes[i];
            node.Left = split == first ? n - 1 + split : split;
            node.Right = split + 1 == last ? n + split : split + 1;
            node.First = first;
            node.Last = last;
            node.Min = node.Max = points[static_cast<std::uint32_t>(keys[first])];
            for (int k = first + 1; k <= last; ++k)
            {
                auto p = points[static_cast<std::uint32_t>(keys[k])];
                node.Min = glm::min(node.Min, p);
                node.Max = glm::max(node.Max, p);
            }
        }
        return true;
    }
    Neighbor Index::Nearest(glm::vec3 query, std::uint32_t excludedIndex) const
    {
        Neighbor best;
        Traverse(m_Nodes, query, best.SquaredDistance, [&](std::uint32_t i) {
            if (i == excludedIndex) return;
            Neighbor candidate{i, Distance(m_Points[i], query)};
            if (Better(candidate, best))
                best = candidate;
        });
        return best;
    }
    RadiusResult Index::Radius(glm::vec3 query, float radius, std::uint32_t capacity,
                              std::uint32_t excludedIndex) const
    {
        RadiusResult result;
        if (!std::isfinite(radius) || radius < 0 || radius > CoordinateLimit)
            return result;
        float limit = radius * radius;
        Traverse(m_Nodes, query, limit, [&](std::uint32_t i) {
            if (i == excludedIndex) return;
            const float d = Distance(m_Points[i], query);
            if (d > limit)
                return;
            ++result.TotalCount;
            auto at = std::ranges::lower_bound(result.Neighbors, i, {}, &Neighbor::Index);
            if (at != result.Neighbors.end() || result.Neighbors.size() < capacity)
                result.Neighbors.insert(at, {i, d});
            if (result.Neighbors.size() > capacity)
                result.Neighbors.pop_back();
        });
        return result;
    }
    std::vector<Neighbor> Index::KNearest(glm::vec3 query, std::uint32_t k,
                                          std::uint32_t excludedIndex) const
    {
        std::vector<Neighbor> result;
        k = std::min(k, static_cast<std::uint32_t>(m_Points.size()));
        if (k == 0 || !ValidPoint(query)) return result;
        result.reserve(k);
        float limit = std::numeric_limits<float>::infinity();
        Traverse(m_Nodes, query, limit, [&](std::uint32_t i) {
            if (i == excludedIndex) return;
            const Neighbor candidate{i, Distance(m_Points[i], query)};
            if (result.size() == k)
            {
                if (!Better(candidate, result.front())) return;
                std::pop_heap(result.begin(), result.end(), Better);
                result.pop_back();
            }
            result.push_back(candidate);
            std::push_heap(result.begin(), result.end(), Better);
            if (result.size() == k) limit = result.front().SquaredDistance;
        });
        std::ranges::sort(result, Better);
        return result;
    }
} // namespace Geometry::PointLBVH
