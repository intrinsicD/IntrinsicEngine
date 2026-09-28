module;

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "ProgressivePoissonReference.hpp"

module Geometry.PointSampling;

// The sieve's pruning compares a point distance with an AABB bound computed by the same
// operation sequence; fused multiply-adds would break that equality.
#pragma clang fp contract(off)

namespace Geometry::PointSampling
{
    std::string_view ToString(const Method value) noexcept
    {
        switch (value)
        {
        case Method::Random: return "random";
        case Method::FarthestPoint: return "farthest_point";
        case Method::ProgressivePoisson: return "progressive_poisson";
        case Method::CoupledSieve: return "coupled_sieve";
        case Method::FlatGreedy: return "flat_greedy";
        case Method::SampleElimination: return "sample_elimination";
        case Method::LazyGreedy: return "lazy_greedy";
        case Method::Tournament: return "tournament";
        }
        return "unknown";
    }

    std::string_view ToString(const Status value) noexcept
    {
        switch (value)
        {
        case Status::Success: return "success";
        case Status::EmptyInput: return "empty_input";
        case Status::NonFiniteInput: return "non_finite_input";
        case Status::InvalidParameters: return "invalid_parameters";
        case Status::UnsupportedMagnitude: return "unsupported_magnitude";
        }
        return "unknown";
    }

    namespace
    {
        constexpr double kMaxSquaredExtent = 1e300;

        // Squared distance with a fixed operation order: dx*dx, then + dy*dy, then + dz*dz.
        [[nodiscard]] inline double Squared(const double ax, const double ay, const double az, const double bx,
                                            const double by, const double bz) noexcept
        {
            const double dx = ax - bx, dy = ay - by, dz = az - bz;
            double s = dx * dx;
            s += dy * dy;
            s += dz * dz;
            return s;
        }

        [[nodiscard]] Status Validate(const PointView points)
        {
            const std::size_t n = points.Size();
            if (points.Y.size() != n || points.Z.size() != n) return Status::InvalidParameters;
            if (n == 0) return Status::EmptyInput;
            double largest = 0.0;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (!std::isfinite(points.X[i]) || !std::isfinite(points.Y[i]) || !std::isfinite(points.Z[i]))
                    return Status::NonFiniteInput;
                largest = std::max({largest, std::abs(points.X[i]), std::abs(points.Y[i]), std::abs(points.Z[i])});
            }
            const double diameter = 2.0 * largest;
            const double extent = 3.0 * diameter * diameter;
            return std::isfinite(extent) && extent <= kMaxSquaredExtent ? Status::Success : Status::UnsupportedMagnitude;
        }

        // Uniform integer in [0, bound) from a 64-bit engine (Lemire), identical on every platform.
        [[nodiscard]] std::uint64_t Bounded(std::mt19937_64& engine, const std::uint64_t bound)
        {
            unsigned __int128 product = static_cast<unsigned __int128>(engine()) * bound;
            std::uint64_t low = static_cast<std::uint64_t>(product);
            if (low < bound)
            {
                const std::uint64_t threshold = (0u - bound) % bound;
                while (low < threshold)
                {
                    product = static_cast<unsigned __int128>(engine()) * bound;
                    low = static_cast<std::uint64_t>(product);
                }
            }
            return static_cast<std::uint64_t>(product >> 64);
        }
    }

    namespace Detail
    {
        Status ValidatePoints(const PointView points) { return Validate(points); }
        double SquaredDistance(const double ax, const double ay, const double az, const double bx, const double by,
                               const double bz) noexcept
        {
            return Squared(ax, ay, az, bx, by, bz);
        }
        std::uint32_t MixU32(std::uint32_t x) noexcept
        {
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return x;
        }
    }

    Status FarthestPointSieve::Build(const PointView points, const std::uint32_t firstIndex,
                                     const std::span<const double> weights, const std::uint32_t leafSize)
    {
        *this = {};
        if (const Status status = Validate(points); status != Status::Success) return status;
        const std::size_t n = points.Size();
        if (leafSize == 0u || firstIndex >= n || n > std::size_t(std::numeric_limits<std::uint32_t>::max()))
            return Status::InvalidParameters;
        if (!weights.empty() &&
            (weights.size() != n || !std::ranges::all_of(weights, [](const double w) { return std::isfinite(w) && w > 0.0; })))
            return Status::InvalidParameters;

        // Morton order over the bounding box (21 bits per axis); ties keep ascending index.
        std::array<double, 3> lo{points.X[0], points.Y[0], points.Z[0]}, hi = lo;
        for (std::size_t i = 1; i < n; ++i)
        {
            const std::array<double, 3> p{points.X[i], points.Y[i], points.Z[i]};
            for (int a = 0; a < 3; ++a) { lo[a] = std::min(lo[a], p[a]); hi[a] = std::max(hi[a], p[a]); }
        }
        constexpr unsigned kBits = 21u;
        const double scale = double((1u << kBits) - 1u);
        std::vector<std::uint64_t> keys(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::array<double, 3> p{points.X[i], points.Y[i], points.Z[i]};
            std::uint64_t key = 0u;
            for (unsigned a = 0; a < 3u; ++a)
            {
                const double extent = hi[a] - lo[a];
                const double t = extent > 0.0 ? std::clamp((p[a] - lo[a]) / extent, 0.0, 1.0) : 0.0;
                const auto cell = std::uint64_t(t * scale);
                for (unsigned b = 0; b < kBits; ++b) key |= ((cell >> b) & 1u) << (b * 3u + a);
            }
            keys[i] = key;
        }
        m_Ids.resize(n);
        std::iota(m_Ids.begin(), m_Ids.end(), 0u);
        std::ranges::stable_sort(m_Ids, [&](const std::uint32_t a, const std::uint32_t b) { return keys[a] < keys[b]; });
        m_PositionOf.resize(n);
        m_X.resize(n); m_Y.resize(n); m_Z.resize(n);
        m_Weighted = !weights.empty();
        m_Weight.assign(n, 1.0);
        for (std::size_t k = 0; k < n; ++k)
        {
            const std::uint32_t id = m_Ids[k];
            m_PositionOf[id] = std::uint32_t(k);
            m_X[k] = points.X[id]; m_Y[k] = points.Y[id]; m_Z[k] = points.Z[id];
            if (m_Weighted) m_Weight[k] = weights[id];
        }
        m_Clear.assign(n, std::numeric_limits<double>::infinity());
        m_Selected.assign(n, 0u);
        m_LeafSize = leafSize;
        const std::size_t leafCount = (n + leafSize - 1u) / leafSize;
        m_Leaves = std::bit_ceil(leafCount);
        const std::size_t nodes = 2u * m_Leaves;
        m_Lo.assign(3u * nodes, 0.0);
        m_Hi.assign(3u * nodes, 0.0);
        m_MaxClear.assign(nodes, -1.0);
        m_MaxKey.assign(nodes, -1.0);
        m_Winner.assign(nodes, -1);
        std::vector<std::size_t> count(nodes, 0u);
        for (std::size_t leaf = 0; leaf < leafCount; ++leaf)
        {
            const std::size_t node = m_Leaves + leaf, begin = leaf * leafSize, end = std::min(begin + leafSize, n);
            count[node] = end - begin;
            for (std::size_t i = begin; i < end; ++i)
            {
                const double p[3]{m_X[i], m_Y[i], m_Z[i]};
                for (std::size_t a = 0; a < 3u; ++a)
                {
                    m_Lo[3u * node + a] = i == begin ? p[a] : std::min(m_Lo[3u * node + a], p[a]);
                    m_Hi[3u * node + a] = i == begin ? p[a] : std::max(m_Hi[3u * node + a], p[a]);
                }
            }
            RefreshLeaf(node);
        }
        for (std::size_t node = m_Leaves - 1u; node >= 1u; --node)
        {
            const std::size_t left = 2u * node, right = left + 1u;
            count[node] = count[left] + count[right];
            for (std::size_t a = 0; a < 3u; ++a)
            {
                if (count[left] > 0u && count[right] > 0u)
                {
                    m_Lo[3u * node + a] = std::min(m_Lo[3u * left + a], m_Lo[3u * right + a]);
                    m_Hi[3u * node + a] = std::max(m_Hi[3u * left + a], m_Hi[3u * right + a]);
                }
                else if (count[left] > 0u || count[right] > 0u)
                {
                    const std::size_t only = count[left] > 0u ? left : right;
                    m_Lo[3u * node + a] = m_Lo[3u * only + a];
                    m_Hi[3u * node + a] = m_Hi[3u * only + a];
                }
            }
            RefreshNode(node);
        }
        m_Stack.assign(2u * (std::bit_width(m_Leaves) + 2u), 0u);
        m_Visited.assign(m_Leaves, 0u);
        m_First = firstIndex;
        m_Order.reserve(std::min<std::size_t>(n, 1u << 16));
        return Status::Success;
    }

    void FarthestPointSieve::RefreshLeaf(const std::size_t node)
    {
        const std::size_t begin = (node - m_Leaves) * m_LeafSize, end = std::min(begin + m_LeafSize, m_X.size());
        double maxClear = -1.0, maxKey = -1.0;
        std::int64_t winner = -1;
        for (std::size_t i = begin; i < end; ++i)
        {
            if (m_Selected[i]) continue;
            const double clear = m_Clear[i];
            const double key = m_Weighted ? m_Weight[i] * clear : clear;
            const std::int64_t id = m_Ids[i];
            maxClear = std::max(maxClear, clear);
            if (key > maxKey || (key == maxKey && (winner < 0 || id < winner)))
            {
                maxKey = key;
                winner = id;
            }
        }
        m_MaxClear[node] = maxClear;
        m_MaxKey[node] = maxKey;
        m_Winner[node] = winner;
    }

    void FarthestPointSieve::RefreshNode(const std::size_t node)
    {
        const std::size_t left = 2u * node, right = left + 1u;
        m_MaxClear[node] = std::max(m_MaxClear[left], m_MaxClear[right]);
        const double a = m_MaxKey[left], b = m_MaxKey[right];
        const std::int64_t wa = m_Winner[left], wb = m_Winner[right];
        const bool takeLeft = a > b || (a == b && (wb < 0 || (wa >= 0 && wa < wb)));
        m_MaxKey[node] = takeLeft ? a : b;
        m_Winner[node] = takeLeft ? wa : wb;
    }

    void FarthestPointSieve::Select(const std::size_t position)
    {
        m_Selected[position] = 1u;
        std::size_t node = m_Leaves + position / m_LeafSize;
        RefreshLeaf(node);
        for (node /= 2u; node >= 1u; node /= 2u) RefreshNode(node);
    }

    // Lowers the clearances by the new sample q. A node whose AABB is farther from q than its
    // largest clearance cannot change: every point p in it has |p - q|^2 >= bound > clearance.
    // The bound uses the point distance's operation order and is lowered by one ulp, so
    // rounding can only miss a skip, never make a false one.
    void FarthestPointSieve::Update(const std::size_t position)
    {
        const double qx = m_X[position], qy = m_Y[position], qz = m_Z[position];
        std::size_t top = 0u, visited = 0u;
        m_Stack[top++] = 1u;
        while (top > 0u)
        {
            const std::size_t node = m_Stack[--top];
            const double largest = m_MaxClear[node];
            if (largest < 0.0) continue; // empty or fully selected
            const double q[3]{qx, qy, qz};
            double bound = 0.0;
            for (std::size_t a = 0; a < 3u; ++a)
            {
                const double lo = m_Lo[3u * node + a], hi = m_Hi[3u * node + a];
                const double gap = q[a] < lo ? lo - q[a] : (q[a] > hi ? q[a] - hi : 0.0);
                bound += gap * gap;
            }
            if (std::nextafter(bound, -std::numeric_limits<double>::infinity()) > largest) continue;
            if (node >= m_Leaves)
            {
                const std::size_t begin = (node - m_Leaves) * m_LeafSize, end = std::min(begin + m_LeafSize, m_X.size());
                for (std::size_t i = begin; i < end; ++i)
                {
                    if (m_Selected[i]) continue;
                    const double d = Squared(m_X[i], m_Y[i], m_Z[i], qx, qy, qz);
                    ++m_Pairs;
                    if (d < m_Clear[i]) m_Clear[i] = d;
                }
                RefreshLeaf(node);
            }
            else
            {
                m_Visited[visited++] = node;
                m_Stack[top++] = 2u * node + 1u;
                m_Stack[top++] = 2u * node;
            }
        }
        // Reverse pre-order refreshes children before their parents.
        while (visited > 0u) RefreshNode(m_Visited[--visited]);
    }

    std::size_t FarthestPointSieve::Extend(const std::size_t count)
    {
        const std::size_t n = m_X.size(), target = std::min(count, n);
        while (m_Order.size() < target)
        {
            std::size_t position = 0u;
            double key = std::numeric_limits<double>::infinity();
            if (m_Order.empty()) position = m_PositionOf[m_First];
            else
            {
                if (m_Winner[1] < 0) break;
                position = m_PositionOf[std::size_t(m_Winner[1])];
                key = m_MaxKey[1];
            }
            m_Order.push_back(m_Ids[position]);
            m_Clearance.push_back(key);
            // Remove the winner before the update so no pruned branch keeps it as a winner.
            Select(position);
            if (m_Order.size() < n) Update(position);
        }
        return m_Order.size();
    }

    namespace
    {
        namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;
        static_assert(std::uint8_t(PoissonCellSelection::Bounded) == std::uint8_t(PPR::CellSelection::Bounded) &&
                      std::uint8_t(PoissonCellSelection::Exhaustive) == std::uint8_t(PPR::CellSelection::Exhaustive) &&
                      std::uint8_t(PoissonCellSelection::BestOfCandidates) == std::uint8_t(PPR::CellSelection::BestOfCandidates) &&
                      std::uint8_t(PoissonCellSelection::FeaturePriority) == std::uint8_t(PPR::CellSelection::FeaturePriority) &&
                      std::uint8_t(PoissonOrdering::SpatiallyBalanced) == std::uint8_t(PPR::WithinLevelOrdering::SpatiallyBalanced) &&
                      std::uint8_t(PoissonProfile::Hapds) == std::uint8_t(PPR::Profile::Hapds));

        [[nodiscard]] PPR::Config ToConfig(const PoissonSettings& s)
        {
            return PPR::Config{.Dimension = s.Dimension, .GridWidth = s.GridWidth, .MaxLevels = s.MaxLevels,
                               .RadiusAlpha = s.RadiusAlpha, .RandomizeGridOrigin = s.RandomizeGridOrigin,
                               .GridOriginSeed = s.GridOriginSeed, .ShuffleWithinLevels = s.ShuffleWithinLevels,
                               .ShuffleSeed = s.ShuffleSeed, .Selection = PPR::CellSelection(s.Selection),
                               .MaxCellRetries = s.MaxCellRetries, .RepairCoarseLevels = s.RepairCoarseLevels,
                               .ExhaustiveCoarseLevels = s.ExhaustiveCoarseLevels, .CandidateBudget = s.CandidateBudget,
                               .RandomizePhaseOrder = s.RandomizePhaseOrder, .PhaseOrderSeed = s.PhaseOrderSeed,
                               .PriorityTwoBands = s.PriorityTwoBands, .PriorityBandThreshold = s.PriorityBandThreshold,
                               .Ordering = PPR::WithinLevelOrdering(s.Ordering), .ComputeSplatRadii = s.ComputeSplatRadii};
        }

        Result Poisson(const std::span<const glm::vec3> points, const PoissonSettings& settings,
                       const std::span<const float> scores, const std::size_t count)
        {
            Result result;
            if (points.empty())
            {
                result.State = Status::EmptyInput;
                return result;
            }
            PPR::Result computed = PPR::Compute(points, ToConfig(settings), scores);
            switch (computed.Diag.Code)
            {
            case PPR::ValidationCode::Valid: break;
            case PPR::ValidationCode::InvalidDimension: result.State = Status::InvalidParameters; return result;
            case PPR::ValidationCode::NonFiniteInput: result.State = Status::NonFiniteInput; return result;
            case PPR::ValidationCode::InvalidConfig: result.State = Status::InvalidParameters; return result;
            }
            const std::size_t k = std::min(count, computed.Order.size());
            computed.Order.resize(k);
            if (!computed.SplatRadii.empty()) computed.SplatRadii.resize(k);
            result.Order = std::move(computed.Order);
            result.SplatRadii = std::move(computed.SplatRadii);
            result.LevelOffsets = std::move(computed.LevelOffsets);
            result.BaseRadius = computed.BaseRadius;
            return result;
        }
    }

    PoissonSettings WithProfile(PoissonSettings base, const PoissonProfile profile) noexcept
    {
        switch (profile)
        {
        case PoissonProfile::Fast: base.Selection = PoissonCellSelection::Bounded; base.MaxCellRetries = 0u; base.RepairCoarseLevels = 0u; break;
        case PoissonProfile::Balanced: base.Selection = PoissonCellSelection::Bounded; base.MaxCellRetries = 1u; base.RepairCoarseLevels = 4u; break;
        case PoissonProfile::Quality: base.Selection = PoissonCellSelection::Bounded; base.MaxCellRetries = 2u; base.RepairCoarseLevels = 4u; break;
        case PoissonProfile::Hapds: base.Selection = PoissonCellSelection::Exhaustive; break;
        }
        return base;
    }

    // Eta-relaxed batches (the CUDA coupled sieve's order definition). Within a batch the tree
    // is only pruned of the accepted points, whose own leaves are refreshed by Select; every
    // other node keeps a valid upper bound, so the root peek returns the next point by
    // batch-start priority. Accepted points then update the clearances together.
    std::size_t FarthestPointSieve::ExtendRelaxed(const std::size_t count, const double eta, const std::uint32_t cap)
    {
        const std::size_t n = m_X.size(), target = std::min(count, n);
        if (m_BatchOffsets.empty()) m_BatchOffsets.push_back(0u);
        std::vector<std::size_t> accepted;
        while (m_Order.size() < target)
        {
            if (m_Order.empty())
            {
                const std::size_t position = m_PositionOf[m_First];
                m_Order.push_back(m_Ids[position]);
                m_Clearance.push_back(std::numeric_limits<double>::infinity());
                Select(position);
                if (m_Order.size() < n) Update(position);
                m_BatchOffsets.push_back(std::uint32_t(m_Order.size()));
                continue;
            }
            if (m_Winner[1] < 0) break;
            const double threshold = (eta * eta) * m_MaxKey[1];
            accepted.clear();
            for (std::uint32_t j = 0; j < cap && m_Winner[1] >= 0; ++j)
            {
                const std::size_t position = m_PositionOf[std::size_t(m_Winner[1])];
                double clear = m_Clear[position];
                for (const std::size_t a : accepted)
                {
                    const double d = Squared(m_X[position], m_Y[position], m_Z[position], m_X[a], m_Y[a], m_Z[a]);
                    ++m_Pairs;
                    clear = std::min(clear, d);
                }
                const double priority = m_Weighted ? m_Weight[position] * clear : clear;
                // Candidate 0 is the exact winner (priority U >= eta^2 U).
                if (j > 0 && !(priority >= threshold)) break;
                m_Order.push_back(m_Ids[position]);
                m_Clearance.push_back(priority);
                Select(position);
                accepted.push_back(position);
            }
            if (m_Order.size() < n)
                for (const std::size_t a : accepted) Update(a);
            m_BatchOffsets.push_back(std::uint32_t(m_Order.size()));
        }
        return m_Order.size();
    }

    namespace
    {
        // Cuts an order and its batch offsets at the requested count.
        void CutBatches(Result& result, const std::size_t count)
        {
            if (result.Order.size() <= count) return;
            result.Order.resize(count);
            if (!result.Clearance.empty()) result.Clearance.resize(count);
            std::erase_if(result.BatchOffsets, [&](const std::uint32_t offset) { return offset >= count; });
            result.BatchOffsets.push_back(std::uint32_t(count));
        }
    }

    Result Order(const PointView points, const Params& params, const std::size_t count)
    {
        Result result;
        if (params.Method == Method::FlatGreedy || params.Method == Method::LazyGreedy)
        {
            result = params.Method == Method::FlatGreedy ? Detail::FlatGreedyOrder(points, params, count)
                                                         : Detail::LazyGreedyOrder(points, params, count);
            if (result.Succeeded()) CutBatches(result, count);
            return result;
        }
        if (params.Method == Method::SampleElimination) return Detail::SampleEliminationOrder(points, params, count);
        if (params.Method == Method::Tournament) return Detail::TournamentOrder(points, params, count);
        if (params.Method == Method::CoupledSieve)
        {
            if (!(params.Eta > 0.0 && params.Eta <= 1.0) || params.CandidateCap < 1u || params.CandidateCap > 32u ||
                (params.Eta == 1.0 && params.CandidateCap != 1u))
            {
                result.State = Status::InvalidParameters;
                return result;
            }
            FarthestPointSieve sieve;
            result.State = sieve.Build(points, params.FirstIndex, params.Weights, params.LeafSize);
            if (!result.Succeeded()) return result;
            sieve.ExtendRelaxed(count, params.Eta, params.CandidateCap);
            result.Order.assign(sieve.Order().begin(), sieve.Order().end());
            result.Clearance.assign(sieve.Clearance().begin(), sieve.Clearance().end());
            result.BatchOffsets.assign(sieve.BatchOffsets().begin(), sieve.BatchOffsets().end());
            result.DistancePairs = sieve.DistancePairs();
            CutBatches(result, count);
            return result;
        }
        if (params.Method == Method::ProgressivePoisson)
        {
            if (points.Y.size() != points.Size() || points.Z.size() != points.Size())
            {
                result.State = Status::InvalidParameters;
                return result;
            }
            std::vector<glm::vec3> converted(points.Size());
            for (std::size_t i = 0; i < converted.size(); ++i)
                converted[i] = glm::vec3(float(points.X[i]), float(points.Y[i]), float(points.Z[i]));
            return Poisson(converted, params.Poisson, params.PriorityScores, count);
        }
        switch (params.Method)
        {
        case Method::Random:
        {
            result.State = Validate(points);
            if (!result.Succeeded()) return result;
            const std::size_t n = points.Size(), k = std::min(count, n);
            std::vector<std::uint32_t> permutation(n);
            std::iota(permutation.begin(), permutation.end(), 0u);
            std::mt19937_64 engine(params.Seed);
            for (std::size_t i = 0; i < k; ++i)
                std::swap(permutation[i], permutation[i + Bounded(engine, n - i)]);
            permutation.resize(k);
            result.Order = std::move(permutation);
            return result;
        }
        case Method::ProgressivePoisson:
        case Method::CoupledSieve:
        case Method::FlatGreedy:
        case Method::SampleElimination:
        case Method::LazyGreedy:
        case Method::Tournament: break; // handled above
        case Method::FarthestPoint:
        {
            FarthestPointSieve sieve;
            result.State = sieve.Build(points, params.FirstIndex, params.Weights, params.LeafSize);
            if (!result.Succeeded()) return result;
            sieve.Extend(count);
            result.Order.assign(sieve.Order().begin(), sieve.Order().end());
            result.Clearance.assign(sieve.Clearance().begin(), sieve.Clearance().end());
            result.DistancePairs = sieve.DistancePairs();
            return result;
        }
        }
        result.State = Status::InvalidParameters;
        return result;
    }

    Result Order(const std::span<const glm::vec3> points, const Params& params, const std::size_t count)
    {
        if (params.Method == Method::ProgressivePoisson)
            return Poisson(points, params.Poisson, params.PriorityScores, count);
        std::vector<double> x(points.size()), y(points.size()), z(points.size());
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            x[i] = points[i].x; y[i] = points[i].y; z[i] = points[i].z;
        }
        return Order(PointView{x, y, z}, params, count);
    }
}
