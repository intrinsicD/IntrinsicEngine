module;
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <numeric>
#include <span>
#include <utility>
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
        // 30-bit Morton code over cubic cells of the given bounds.
        std::uint32_t MortonCode(glm::vec3 p, glm::vec3 lo, float extent)
        {
            glm::uvec3 q{};
            for (int a = 0; a < 3; ++a)
                q[a] = extent > 0
                           ? static_cast<std::uint32_t>(std::clamp((p[a] - lo[a]) / extent * 1024.0f, 0.0f, 1023.0f))
                           : 0u;
            return (Spread(q.x) << 2u) | (Spread(q.y) << 1u) | Spread(q.z);
        }
        // Keys are digit strings: one 30-bit Morton digit per refinement level, then the 32-bit
        // source id. Points sharing a cell get a further digit relative to that cell's own
        // bounds, so clustered inputs keep splitting spatially instead of by id.
        constexpr int kMaxKeyDigits = 10;
        constexpr int kMaxRefinement = kMaxKeyDigits - 2;
        struct KeyDigits
        {
            std::array<std::uint32_t, kMaxKeyDigits> Digit{};
            int Count{};
        };
        // Sorts ids[begin,end), which share digits [0,level), by a Morton digit over their own
        // bounds and recurses into runs that share it. The minimum and maximum of a
        // non-degenerate run land in different sub-cells, so every level splits the run.
        void RefineRun(std::span<const glm::vec3> points, std::span<std::uint32_t> ids,
                       std::span<KeyDigits> keys, int level, std::vector<std::uint64_t>& scratch)
        {
            glm::vec3 lo = points[ids[0]], hi = lo;
            for (const auto id : ids)
            {
                lo = glm::min(lo, points[id]);
                hi = glm::max(hi, points[id]);
            }
            const float extent = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
            if (!(extent > 0) || level > kMaxRefinement)
                return; // coincident points (or the refinement limit) keep ascending id order
            scratch.clear();
            for (const auto id : ids)
                scratch.push_back((std::uint64_t(MortonCode(points[id], lo, extent)) << 32u) | id);
            std::ranges::sort(scratch);
            for (std::size_t i = 0; i < ids.size(); ++i)
            {
                ids[i] = static_cast<std::uint32_t>(scratch[i]);
                keys[i].Digit[level] = static_cast<std::uint32_t>(scratch[i] >> 32u);
                keys[i].Count = level + 1;
            }
            for (std::size_t begin = 0, end; begin < ids.size(); begin = end)
            {
                for (end = begin + 1; end < ids.size() && keys[end].Digit[level] == keys[begin].Digit[level]; ++end) {}
                if (end - begin > 1)
                    RefineRun(points, ids.subspan(begin, end - begin), keys.subspan(begin, end - begin), level + 1, scratch);
            }
        }
        // Common prefix length in bits of two distinct keys; equal-length digit strings share
        // their layout up to the first differing digit.
        int CommonPrefix(const KeyDigits& a, const KeyDigits& b)
        {
            int length = 0;
            for (int level = 0;; ++level)
            {
                const int width = level + 1 == a.Count ? 32 : 30;
                if (a.Digit[level] != b.Digit[level])
                    return length + std::countl_zero(a.Digit[level] ^ b.Digit[level]) - (32 - width);
                length += width;
            }
        }
        bool Better(Neighbor a, Neighbor b)
        {
            return a.SquaredDistance < b.SquaredDistance ||
                   (a.SquaredDistance == b.SquaredDistance && a.Index < b.Index);
        }
        float BoxDistance(const Node& node, glm::vec3 query)
        {
            return Distance(query, glm::clamp(query, node.Min, node.Max));
        }
        // Visits every leaf whose box is within `limit`, nearer child first so shrinking
        // kNN/nearest limits prune early. Pruning is strict, so boxes at exactly the limit
        // are still visited and index tie-breaks do not depend on visit order.
        template <typename Visit>
        void Traverse(std::span<const Node> nodes, glm::vec3 query, float& limit, Visit visit)
        {
            if (nodes.empty() || !ValidPoint(query))
                return;
            // Tree depth is at most the key length: nine 30-bit Morton digits and a 32-bit id,
            // 302 bits. Each expansion replaces one entry by at most two, so 304 entries suffice.
            struct Entry { std::uint32_t Node; float Distance; };
            std::array<Entry, 304> stack{};
            stack[0] = {0u, BoxDistance(nodes[0], query)};
            std::uint32_t size = 1;
            while (size)
            {
                const auto entry = stack[--size];
                if (entry.Distance > limit)
                    continue;
                const auto& node = nodes[entry.Node];
                if (node.Object != InvalidIndex)
                {
                    visit(node.Object);
                    continue;
                }
                Entry near{node.Left, BoxDistance(nodes[node.Left], query)};
                Entry far{node.Right, BoxDistance(nodes[node.Right], query)};
                if (far.Distance < near.Distance) std::swap(near, far);
                if (far.Distance <= limit) stack[size++] = far;
                if (near.Distance <= limit) stack[size++] = near;
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
        // Cubic cells (largest extent on every axis) keep boxes compact for flat inputs;
        // lbvh_morton.comp uses the same quantization.
        const float extent = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
        std::vector<std::uint64_t> sorted;
        sorted.reserve(points.size());
        for (std::uint32_t i = 0; i < points.size(); ++i)
            sorted.push_back((std::uint64_t(MortonCode(points[i], lo, extent)) << 32u) | i);
        std::ranges::sort(sorted);
        std::vector<std::uint32_t> order(points.size());
        std::vector<KeyDigits> keys(points.size());
        for (std::size_t i = 0; i < sorted.size(); ++i)
        {
            order[i] = static_cast<std::uint32_t>(sorted[i]);
            keys[i].Digit[0] = static_cast<std::uint32_t>(sorted[i] >> 32u);
            keys[i].Count = 1;
        }
        std::vector<std::uint64_t> scratch;
        for (std::size_t begin = 0, end; begin < keys.size(); begin = end)
        {
            for (end = begin + 1; end < keys.size() && keys[end].Digit[0] == keys[begin].Digit[0]; ++end) {}
            if (end - begin > 1)
                RefineRun(points, std::span(order).subspan(begin, end - begin),
                          std::span(keys).subspan(begin, end - begin), 1, scratch);
        }
        // The source id is the last digit and makes every key unique.
        for (std::size_t i = 0; i < keys.size(); ++i)
            keys[i].Digit[keys[i].Count++] = order[i];
        m_Nodes.resize(2u * points.size() - 1u);
        for (int i = 0; i < n; ++i)
        {
            const auto id = order[i];
            m_Nodes[n - 1 + i] = {.Min = points[id],
                                  .Max = points[id],
                                  .Object = id,
                                  .First = static_cast<std::uint32_t>(i),
                                  .Last = static_cast<std::uint32_t>(i)};
        }
        auto prefix = [&](int i, int j) {
            return j < 0 || j >= n ? -1 : CommonPrefix(keys[i], keys[j]);
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
            node.Min = node.Max = points[order[first]];
            for (int k = first + 1; k <= last; ++k)
            {
                auto p = points[order[k]];
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
