// GEOM-113: CPU references of the approximate farthest-point family and of weighted sample
// elimination for Geometry.PointSampling.
module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <set>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

module Geometry.PointSampling;

#pragma clang fp contract(off)

namespace Geometry::PointSampling::Detail
{
    namespace
    {
        // Uniform hash grid over a subset of points for fixed-radius neighbor queries.
        class Grid
        {
        public:
            Grid(const PointView points, const std::span<const std::uint32_t> ids, const double cell)
                : m_Points(points), m_Inverse(1.0 / cell)
            {
                m_Cells.reserve(ids.size());
                for (const std::uint32_t id : ids) m_Cells[Key(Cell(id))].push_back(id);
            }

            // body(id, squared distance) for every indexed point with |p - q|^2 < radius2 (q excluded).
            template <typename Body>
            void ForEachWithin(const std::uint32_t query, const double radius2, Body&& body) const
            {
                const auto [cx, cy, cz] = Cell(query);
                for (std::int64_t dz = -1; dz <= 1; ++dz)
                    for (std::int64_t dy = -1; dy <= 1; ++dy)
                        for (std::int64_t dx = -1; dx <= 1; ++dx)
                        {
                            const auto it = m_Cells.find(Key({cx + dx, cy + dy, cz + dz}));
                            if (it == m_Cells.end()) continue;
                            for (const std::uint32_t other : it->second)
                            {
                                if (other == query) continue;
                                const double d = SquaredDistance(m_Points.X[query], m_Points.Y[query], m_Points.Z[query],
                                                                 m_Points.X[other], m_Points.Y[other], m_Points.Z[other]);
                                if (d < radius2) body(other, d);
                            }
                        }
            }

        private:
            [[nodiscard]] std::array<std::int64_t, 3> Cell(const std::uint32_t id) const
            {
                return {std::int64_t(std::floor(m_Points.X[id] * m_Inverse)),
                        std::int64_t(std::floor(m_Points.Y[id] * m_Inverse)),
                        std::int64_t(std::floor(m_Points.Z[id] * m_Inverse))};
            }
            [[nodiscard]] static std::uint64_t Key(const std::array<std::int64_t, 3>& c)
            {
                // Hash only; equal keys of distinct cells cost extra distance tests, never results.
                return std::uint64_t(c[0]) * 0x9e3779b97f4a7c15ull ^ std::uint64_t(c[1]) * 0xc2b2ae3d27d4eb4full ^
                       std::uint64_t(c[2]) * 0x165667b19e3779f9ull;
            }

            PointView m_Points;
            double m_Inverse;
            std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> m_Cells;
        };

        [[nodiscard]] bool UsableCell(const double cell, const PointView points)
        {
            if (!(cell > 0.0) || !std::isfinite(cell)) return false;
            double largest = 0.0;
            for (std::size_t i = 0; i < points.Size(); ++i)
                largest = std::max({largest, std::abs(points.X[i]), std::abs(points.Y[i]), std::abs(points.Z[i])});
            return largest / cell < 1e15;
        }
    }

    // Beta-greedy batches (the CUDA flat greedy's definition in double precision): U is the
    // largest clearance; the batch is an independent set, in the conflict graph of radius
    // U / beta, of the points with clearance >= U / beta, grown by priority rounds (a point
    // joins when no undecided neighbor ranks higher); its members are emitted by the ordering
    // key. Ties: seeded random key, then the smaller index. beta = 1 emits the exact farthest
    // point; a zero largest clearance emits the remaining duplicates in index order.
    Result FlatGreedyOrder(const PointView points, const Params& params, const std::size_t count)
    {
        Result result;
        result.State = ValidatePoints(points);
        if (!result.Succeeded()) return result;
        const std::size_t n = points.Size();
        if (!(params.Beta >= 1.0) || !std::isfinite(params.Beta) || params.FirstIndex >= n ||
            params.MaxMisRounds == 0u || params.MaxBatchCandidates == 0u)
        {
            result.State = Status::InvalidParameters;
            return result;
        }
        const std::size_t target = std::min(count, n);
        std::vector<double> clear(n, std::numeric_limits<double>::infinity());
        std::vector<std::uint8_t> selected(n, 0u);
        std::uint32_t batches = 0u;
        result.BatchOffsets.push_back(0u);
        const auto distance = [&](const std::uint32_t a, const std::uint32_t b) {
            ++result.DistancePairs;
            return SquaredDistance(points.X[a], points.Y[a], points.Z[a], points.X[b], points.Y[b], points.Z[b]);
        };
        const auto commit = [&](const std::vector<std::uint32_t>& ids) {
            for (std::size_t k = 0; k < ids.size(); ++k)
            {
                double insertion = clear[ids[k]];
                for (std::size_t e = 0; e < k; ++e) insertion = std::min(insertion, distance(ids[k], ids[e]));
                result.Order.push_back(ids[k]);
                result.Clearance.push_back(insertion);
                selected[ids[k]] = 1u;
                clear[ids[k]] = 0.0;
            }
            for (std::uint32_t i = 0; i < n; ++i)
                if (!selected[i])
                    for (const std::uint32_t id : ids) clear[i] = std::min(clear[i], distance(i, id));
            result.BatchOffsets.push_back(std::uint32_t(result.Order.size()));
            ++batches;
        };

        commit({params.FirstIndex});
        std::vector<std::uint32_t> candidates, accepted;
        std::vector<double> gain;
        while (result.Order.size() < target)
        {
            double largest = -1.0;
            std::uint32_t farthest = 0u;
            for (std::uint32_t i = 0; i < n; ++i)
                if (!selected[i] && clear[i] > largest) { largest = clear[i]; farthest = i; }
            if (largest < 0.0) break;
            if (!(largest > 0.0))
            {
                std::vector<std::uint32_t> tail;
                for (std::uint32_t i = 0; i < n; ++i)
                    if (!selected[i]) tail.push_back(i);
                commit(tail);
                break;
            }
            if (params.Beta == 1.0)
            {
                commit({farthest});
                continue;
            }
            const double radius = std::sqrt(largest) / params.Beta;
            const double threshold = radius * radius;
            // Admissible points, the exact farthest first so a cap never drops it.
            candidates.assign(1u, farthest);
            for (std::uint32_t i = 0; i < n && candidates.size() < params.MaxBatchCandidates; ++i)
                if (!selected[i] && i != farthest && clear[i] >= threshold) candidates.push_back(i);
            if (candidates.size() == 1u || !UsableCell(radius, points))
            {
                commit({farthest});
                continue;
            }
            const std::uint32_t batchSeed = params.GreedySeed ^ MixU32(batches);
            // Sampled coverage gain: common probes p, sum of max(0, clear_p - |p - c|^2).
            const bool needGain = params.BatchPriority == GreedyPriority::CoverageGain ||
                                  params.BatchOrdering == GreedyPriority::CoverageGain;
            gain.assign(n, 0.0);
            if (needGain && params.GainSamples > 0u)
            {
                const std::uint32_t take = std::uint32_t(std::min<std::size_t>(params.GainSamples, n));
                const std::uint64_t start = MixU32(batchSeed) % n, stride = MixU32(batchSeed ^ 0x9e3779b9u) | 1u;
                for (const std::uint32_t c : candidates)
                    for (std::uint32_t s = 0; s < take; ++s)
                    {
                        const auto p = std::uint32_t((start + std::uint64_t(s) * stride) % n);
                        gain[c] += std::max(0.0, clear[p] - distance(p, c));
                    }
            }
            const auto higher = [&](const std::uint32_t a, const std::uint32_t b, const GreedyPriority mode) {
                if (mode == GreedyPriority::Clearance && clear[a] != clear[b]) return clear[a] > clear[b];
                if (mode == GreedyPriority::CoverageGain && gain[a] != gain[b]) return gain[a] > gain[b];
                const std::uint32_t ra = MixU32(a ^ batchSeed), rb = MixU32(b ^ batchSeed);
                if (ra != rb) return ra > rb;
                return a < b;
            };
            // Priority rounds of a maximal independent set in the implicit radius graph.
            const Grid grid(points, candidates, radius);
            std::unordered_map<std::uint32_t, std::uint8_t> state; // 0 undecided, 1 in, 2 out
            for (const std::uint32_t c : candidates) state[c] = 0u;
            std::vector<std::uint32_t> winners;
            for (std::uint32_t round = 0; round < params.MaxMisRounds; ++round)
            {
                winners.clear();
                for (const std::uint32_t c : candidates)
                {
                    if (state[c] != 0u) continue;
                    bool win = true;
                    grid.ForEachWithin(c, threshold, [&](const std::uint32_t other, double) {
                        if (win && state[other] == 0u && higher(other, c, params.BatchPriority)) win = false;
                    });
                    if (win) winners.push_back(c);
                }
                if (winners.empty()) break;
                for (const std::uint32_t w : winners) state[w] = 1u;
                for (const std::uint32_t w : winners)
                    grid.ForEachWithin(w, threshold, [&](const std::uint32_t other, double) {
                        if (state[other] == 0u) state[other] = 2u;
                    });
            }
            accepted.clear();
            for (const std::uint32_t c : candidates)
                if (state[c] == 1u) accepted.push_back(c);
            if (accepted.empty())
            {
                commit({farthest});
                continue;
            }
            const GreedyPriority ordering = params.BatchOrdering;
            std::sort(accepted.begin(), accepted.end(),
                      [&](const std::uint32_t a, const std::uint32_t b) { return higher(a, b, ordering); });
            commit(accepted);
        }
        return result;
    }

    // Weighted sample elimination (Yuksel, EG 2015), progressive variant, in double precision
    // with a deterministic tie rule (heaviest first, then the smaller index). Implemented from
    // the paper and the MIT-licensed cyCodeBase semantics (weight (1 - d / d_max)^alpha with
    // d clamped below by the weight limit d_min = d_max (1 - (k / n)^gamma) beta).
    Result SampleEliminationOrder(const PointView points, const Params& params, const std::size_t count)
    {
        Result result;
        result.State = ValidatePoints(points);
        if (!result.Succeeded()) return result;
        const std::size_t n = points.Size();
        const std::uint32_t dims = params.ManifoldDimension;
        if (!(params.EliminationAlpha > 0.0) || !std::isfinite(params.EliminationAlpha) ||
            !(params.EliminationBeta >= 0.0 && params.EliminationBeta < 1.0) ||
            !(params.EliminationGamma > 0.0) || !std::isfinite(params.EliminationGamma) ||
            !(params.EliminationRadius >= 0.0) || !std::isfinite(params.EliminationRadius) || (dims != 2u && dims != 3u))
        {
            result.State = Status::InvalidParameters;
            return result;
        }
        const std::size_t k = std::min(count, n);
        if (k == 0u) return result;

        // Weight radius: 2 r_max over the bounding box of the `dims` largest extents.
        double dMax = params.EliminationRadius;
        if (!(dMax > 0.0))
        {
            std::array<double, 3> lo{points.X[0], points.Y[0], points.Z[0]}, hi = lo;
            for (std::size_t i = 1; i < n; ++i)
            {
                lo = {std::min(lo[0], points.X[i]), std::min(lo[1], points.Y[i]), std::min(lo[2], points.Z[i])};
                hi = {std::max(hi[0], points.X[i]), std::max(hi[1], points.Y[i]), std::max(hi[2], points.Z[i])};
            }
            std::array<double, 3> extent{hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};
            std::sort(extent.begin(), extent.end(), std::greater<>());
            double domain = extent[0];
            for (std::uint32_t d = 1; d < dims; ++d) domain *= extent[d];
            if (!(domain > 0.0)) domain = std::max(extent[0], 1.0);
            const double area = domain / double(k);
            const double rMax = dims == 2u ? std::sqrt(area / (2.0 * std::sqrt(3.0)))
                                           : std::cbrt(area / (4.0 * std::sqrt(2.0)));
            dMax = 2.0 * rMax;
        }
        const double dMin = params.WeightLimiting
            ? dMax * (1.0 - std::pow(double(k) / double(n), params.EliminationGamma)) * params.EliminationBeta
            : 0.0;
        const auto weight = [&](const double d2, const double radius) {
            const double d = std::max(std::sqrt(d2), dMin);
            return std::pow(1.0 - d / radius, params.EliminationAlpha);
        };

        // Eliminates the heaviest points of `ids` until `keep` remain; returns them in
        // elimination order and leaves the survivors (ascending index) in `ids`.
        const auto eliminate = [&](std::vector<std::uint32_t>& ids, const std::size_t keep, const double radius) {
            std::vector<std::uint32_t> eliminated;
            if (ids.size() <= keep) return eliminated;
            if (!UsableCell(radius, points))
            {
                // Degenerate radius: drop from the back of the index order.
                eliminated.assign(ids.begin() + std::ptrdiff_t(keep), ids.end());
                std::reverse(eliminated.begin(), eliminated.end());
                ids.resize(keep);
                return eliminated;
            }
            const Grid grid(points, ids, radius);
            const double radius2 = radius * radius;
            std::unordered_map<std::uint32_t, double> w;
            for (const std::uint32_t i : ids)
            {
                double sum = 0.0;
                grid.ForEachWithin(i, radius2, [&](std::uint32_t, const double d2) { sum += weight(d2, radius); });
                w[i] = sum;
            }
            const auto heavier = [](const std::pair<double, std::uint32_t>& a, const std::pair<double, std::uint32_t>& b) {
                return a.first != b.first ? a.first > b.first : a.second < b.second;
            };
            std::set<std::pair<double, std::uint32_t>, decltype(heavier)> heap(heavier);
            for (const std::uint32_t i : ids) heap.insert({w[i], i});
            std::unordered_map<std::uint32_t, std::uint8_t> removed;
            while (heap.size() > keep)
            {
                const std::uint32_t top = heap.begin()->second;
                heap.erase(heap.begin());
                removed[top] = 1u;
                eliminated.push_back(top);
                grid.ForEachWithin(top, radius2, [&](const std::uint32_t other, const double d2) {
                    if (removed.contains(other)) return;
                    heap.erase({w[other], other});
                    w[other] -= weight(d2, radius);
                    heap.insert({w[other], other});
                });
            }
            ids.clear();
            for (const auto& [value, id] : heap) ids.push_back(id);
            std::sort(ids.begin(), ids.end());
            return eliminated;
        };

        std::vector<std::uint32_t> survivors(n);
        std::iota(survivors.begin(), survivors.end(), 0u);
        (void)eliminate(survivors, k, dMax);
        // Progressive: halve with a growing radius; later-eliminated points come first.
        std::vector<std::vector<std::uint32_t>> stages;
        double radius = dMax;
        const double growth = dims == 2u ? std::sqrt(2.0) : std::pow(2.0, 1.0 / double(dims));
        while (survivors.size() >= 3u)
        {
            radius *= growth;
            auto eliminated = eliminate(survivors, survivors.size() / 2u, radius);
            std::reverse(eliminated.begin(), eliminated.end());
            stages.push_back(std::move(eliminated));
        }
        result.Order = survivors;
        for (auto stage = stages.rbegin(); stage != stages.rend(); ++stage)
            result.Order.insert(result.Order.end(), stage->begin(), stage->end());
        return result;
    }
}

namespace Geometry::PointSampling::Detail
{
    // The CUDA lazy greedy's batch rule with eager clearances: U is the largest clearance; the
    // points with clearance >= U / beta become records keyed by (parity phase of their cell of
    // size U / beta, xor a seeded batch phase; cell; void density when chosen; seeded random
    // key; index); the first record of each cell survives, and phases are visited in order,
    // accepting a record unless an accepted point lies within U / beta. Same-phase cells are
    // at least two cells apart, so the batch is independent and keeps the beta guarantee.
    Result LazyGreedyOrder(const PointView points, const Params& params, const std::size_t count)
    {
        Result result;
        result.State = ValidatePoints(points);
        if (!result.Succeeded()) return result;
        const std::size_t n = points.Size();
        if (!(params.Beta >= 1.0) || !std::isfinite(params.Beta) || params.FirstIndex >= n ||
            params.MaxBatchCandidates == 0u || params.LazyBatchPriority > LazyPriority::VoidDensity)
        {
            result.State = Status::InvalidParameters;
            return result;
        }
        std::array<double, 3> origin{points.X[0], points.Y[0], points.Z[0]};
        for (std::size_t i = 1; i < n; ++i)
            origin = {std::min(origin[0], points.X[i]), std::min(origin[1], points.Y[i]), std::min(origin[2], points.Z[i])};
        const std::size_t target = std::min(count, n);
        std::vector<double> clear(n, std::numeric_limits<double>::infinity());
        std::vector<std::uint8_t> selected(n, 0u);
        result.BatchOffsets.push_back(0u);
        const auto distance = [&](const std::uint32_t a, const std::uint32_t b) {
            ++result.DistancePairs;
            return SquaredDistance(points.X[a], points.Y[a], points.Z[a], points.X[b], points.Y[b], points.Z[b]);
        };
        const auto commit = [&](const std::vector<std::uint32_t>& ids) {
            for (std::size_t k = 0; k < ids.size(); ++k)
            {
                double insertion = clear[ids[k]];
                for (std::size_t e = 0; e < k; ++e) insertion = std::min(insertion, distance(ids[k], ids[e]));
                result.Order.push_back(ids[k]);
                result.Clearance.push_back(insertion);
                selected[ids[k]] = 1u;
                clear[ids[k]] = 0.0;
            }
            for (std::uint32_t i = 0; i < n; ++i)
                if (!selected[i])
                    for (const std::uint32_t id : ids) clear[i] = std::min(clear[i], distance(i, id));
            result.BatchOffsets.push_back(std::uint32_t(result.Order.size()));
        };

        struct Record
        {
            std::uint32_t Phase{}, Random{}, Id{};
            std::array<std::int64_t, 3> Cell{};
            double Density{};
        };
        commit({params.FirstIndex});
        while (result.Order.size() < target)
        {
            double largest = -1.0;
            std::uint32_t farthest = 0u;
            for (std::uint32_t i = 0; i < n; ++i)
                if (!selected[i] && clear[i] > largest) { largest = clear[i]; farthest = i; }
            if (largest < 0.0) break;
            if (!(largest > 0.0))
            {
                std::vector<std::uint32_t> tail;
                for (std::uint32_t i = 0; i < n; ++i)
                    if (!selected[i]) tail.push_back(i);
                commit(tail);
                break;
            }
            const double radius = std::sqrt(largest) / params.Beta;
            if (params.Beta == 1.0 || !UsableCell(radius, points))
            {
                commit({farthest});
                continue;
            }
            const double threshold = radius * radius;
            std::vector<std::uint32_t> admitted{farthest};
            for (std::uint32_t i = 0; i < n && admitted.size() < params.MaxBatchCandidates; ++i)
                if (!selected[i] && i != farthest && clear[i] >= threshold) admitted.push_back(i);
            const std::uint32_t batchIndex = std::uint32_t(result.BatchOffsets.size());
            const std::uint32_t seed = params.GreedySeed ^ batchIndex;
            const std::uint32_t phaseXor = MixU32(params.GreedySeed ^ batchIndex) & 7u;
            const double densityRadius = 2.0 * radius;
            std::vector<Record> records;
            records.reserve(admitted.size());
            for (const std::uint32_t id : admitted)
            {
                Record r;
                r.Id = id;
                r.Cell = {std::int64_t(std::floor((points.X[id] - origin[0]) / radius)),
                          std::int64_t(std::floor((points.Y[id] - origin[1]) / radius)),
                          std::int64_t(std::floor((points.Z[id] - origin[2]) / radius))};
                r.Phase = std::uint32_t((r.Cell[0] & 1) | ((r.Cell[1] & 1) << 1) | ((r.Cell[2] & 1) << 2)) ^ phaseXor;
                r.Random = MixU32(id ^ seed);
                if (params.LazyBatchPriority == LazyPriority::VoidDensity)
                    for (const std::uint32_t s : result.Order)
                    {
                        const double t = std::max(0.0, 1.0 - std::sqrt(distance(id, s)) / densityRadius);
                        r.Density += t * t * t;
                    }
                records.push_back(r);
            }
            const auto before = [&](const Record& a, const Record& b) {
                if (a.Phase != b.Phase) return a.Phase < b.Phase;
                if (a.Cell != b.Cell) return a.Cell < b.Cell;
                if (params.LazyBatchPriority == LazyPriority::VoidDensity && a.Density != b.Density)
                    return a.Density < b.Density;
                if (a.Random != b.Random) return a.Random < b.Random;
                return a.Id < b.Id;
            };
            std::sort(records.begin(), records.end(), before);
            records.erase(std::unique(records.begin(), records.end(),
                                      [](const Record& a, const Record& b) { return a.Phase == b.Phase && a.Cell == b.Cell; }),
                          records.end());
            std::vector<Record> accepted;
            std::vector<std::uint32_t> acceptedIds;
            for (const Record& r : records)
            {
                bool close = false;
                for (const std::uint32_t a : acceptedIds)
                    if (distance(r.Id, a) < threshold) { close = true; break; }
                if (close) continue;
                accepted.push_back(r);
                acceptedIds.push_back(r.Id);
            }
            std::sort(accepted.begin(), accepted.end(), before);
            std::vector<std::uint32_t> ids;
            for (const Record& r : accepted) ids.push_back(r.Id);
            commit(ids.empty() ? std::vector<std::uint32_t>{farthest} : ids);
        }
        return result;
    }

    // Tournament baseline over the Morton-ordered balanced tree (the CUDA version uses a Karras
    // LBVH): leaves hold single points; an internal node keeps the child winner nearer its box
    // center (ties: smaller index) and records the other as its loser. The order is the root
    // winner, then the losers by node box diagonal, largest first (ties: seeded random key of
    // the node, then index).
    Result TournamentOrder(const PointView points, const Params& params, const std::size_t count)
    {
        Result result;
        result.State = ValidatePoints(points);
        if (!result.Succeeded()) return result;
        const std::size_t n = points.Size();
        std::array<double, 3> lo{points.X[0], points.Y[0], points.Z[0]}, hi = lo;
        for (std::size_t i = 1; i < n; ++i)
        {
            lo = {std::min(lo[0], points.X[i]), std::min(lo[1], points.Y[i]), std::min(lo[2], points.Z[i])};
            hi = {std::max(hi[0], points.X[i]), std::max(hi[1], points.Y[i]), std::max(hi[2], points.Z[i])};
        }
        std::vector<std::uint64_t> keys(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::array<double, 3> p{points.X[i], points.Y[i], points.Z[i]};
            std::uint64_t key = 0u;
            for (unsigned a = 0; a < 3u; ++a)
            {
                const double extent = hi[a] - lo[a];
                const auto cell = std::uint64_t((extent > 0.0 ? std::clamp((p[a] - lo[a]) / extent, 0.0, 1.0) : 0.0) *
                                                double((1u << 21) - 1u));
                for (unsigned b = 0; b < 21u; ++b) key |= ((cell >> b) & 1u) << (b * 3u + a);
            }
            keys[i] = key;
        }
        std::vector<std::uint32_t> sorted(n);
        std::iota(sorted.begin(), sorted.end(), 0u);
        std::stable_sort(sorted.begin(), sorted.end(), [&](const std::uint32_t a, const std::uint32_t b) { return keys[a] < keys[b]; });
        std::size_t leaves = 1u;
        while (leaves < n) leaves *= 2u;
        constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> winner(2u * leaves, kNone);
        std::vector<std::array<double, 6>> box(2u * leaves);
        for (std::size_t k = 0; k < n; ++k)
        {
            const std::uint32_t id = sorted[k];
            winner[leaves + k] = id;
            box[leaves + k] = {points.X[id], points.Y[id], points.Z[id], points.X[id], points.Y[id], points.Z[id]};
        }
        struct Loser
        {
            double Scale{};
            std::uint32_t Tie{}, Id{};
        };
        std::vector<Loser> losers;
        for (std::size_t node = leaves - 1u; node >= 1u; --node)
        {
            const std::uint32_t a = winner[2u * node], b = winner[2u * node + 1u];
            if (a == kNone || b == kNone)
            {
                winner[node] = a == kNone ? b : a;
                box[node] = a == kNone ? box[2u * node + 1u] : box[2u * node];
                continue;
            }
            const auto& l = box[2u * node];
            const auto& r = box[2u * node + 1u];
            box[node] = {std::min(l[0], r[0]), std::min(l[1], r[1]), std::min(l[2], r[2]),
                         std::max(l[3], r[3]), std::max(l[4], r[4]), std::max(l[5], r[5])};
            const auto& bx = box[node];
            const double cx = 0.5 * (bx[0] + bx[3]), cy = 0.5 * (bx[1] + bx[4]), cz = 0.5 * (bx[2] + bx[5]);
            const double da = SquaredDistance(points.X[a], points.Y[a], points.Z[a], cx, cy, cz);
            const double db = SquaredDistance(points.X[b], points.Y[b], points.Z[b], cx, cy, cz);
            const bool keepA = da < db || (da == db && a < b);
            winner[node] = keepA ? a : b;
            const double sx = bx[3] - bx[0], sy = bx[4] - bx[1], sz = bx[5] - bx[2];
            losers.push_back({std::sqrt(sx * sx + sy * sy + sz * sz), MixU32(std::uint32_t(node) ^ params.GreedySeed),
                              keepA ? b : a});
        }
        std::sort(losers.begin(), losers.end(), [](const Loser& a, const Loser& b) {
            if (a.Scale != b.Scale) return a.Scale > b.Scale;
            if (a.Tie != b.Tie) return a.Tie < b.Tie;
            return a.Id < b.Id;
        });
        result.Order.push_back(winner[1]);
        for (const Loser& l : losers) result.Order.push_back(l.Id);
        if (result.Order.size() > count) result.Order.resize(count);
        return result;
    }
}
