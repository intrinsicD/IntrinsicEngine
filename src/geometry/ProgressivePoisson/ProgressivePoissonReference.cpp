/// @file ProgressivePoissonReference.cpp
/// @brief CPU reference implementation of progressive Poisson-disk subsampling.
///
/// Serial reproduction of the phase-parallel spatial-hashing algorithm
/// (code/progressive_poisson.cu). The reference is the canonical truth: it is
/// deterministic by construction. Where the GPU form leaves a choice to
/// scheduling (which point in a contested cell wins, intra-phase output order),
/// the reference fixes it deterministically — first point in remaining order
/// wins a cell; accepted points are emitted in remaining order (stable). The
/// within-level permutation is a reference-owned deterministic shuffle; it is
/// guarantee-preserving (any subset of a level prefix keeps min-distance), so it
/// need not match the GPU's thrust::shuffle bit-for-bit.

#include "ProgressivePoissonReference.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Intrinsic::Methods::Geometry::ProgressivePoissonReference
{
    namespace
    {
        constexpr float kSqrt2 = 1.41421356237f;
        constexpr float kSqrt3 = 1.73205080757f;

        // Deterministic 32-bit integer mixer (bijection) — matches the reference
        // mix_u32 so per-level grid-origin offsets are reproducible.
        [[nodiscard]] std::uint32_t MixU32(std::uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7feb352dU;
            x ^= x >> 15;
            x *= 0x846ca68bU;
            x ^= x >> 16;
            return x;
        }

        // 32-bit integer -> float in [0,1) using the top 24 bits (exact in float).
        [[nodiscard]] float U32ToUnif01(std::uint32_t x)
        {
            const std::uint32_t hi = x >> 8;
            return static_cast<float>(hi) * (1.0f / 16777216.0f); // 2^24
        }

        // splitmix64 — portable deterministic RNG for the within-level shuffle.
        struct SplitMix64
        {
            std::uint64_t state;
            explicit SplitMix64(std::uint64_t seed) : state(seed) {}
            [[nodiscard]] std::uint64_t Next()
            {
                std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
                z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
                z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
                return z ^ (z >> 31);
            }
            // Unbiased index in [0, n).
            [[nodiscard]] std::uint32_t Bounded(std::uint32_t n)
            {
                // Lemire-style rejection on the high bits.
                const std::uint64_t threshold = (~static_cast<std::uint64_t>(0) - n + 1) % n;
                while (true)
                {
                    const std::uint64_t r = Next();
                    if (r >= threshold)
                        return static_cast<std::uint32_t>(r % n);
                }
            }
        };

        // Pack an integer cell coordinate triple into a 64-bit key. Mirrors the
        // reference: 2D packs two 32-bit signed coords; 3D packs three signed
        // 21-bit coords (valid range per axis [-2^20, 2^20-1]).
        [[nodiscard]] std::uint64_t PackCell(int ix, int iy, int iz, int dim)
        {
            if (dim == 3)
            {
                auto u = [](int v) -> std::uint64_t {
                    return static_cast<std::uint64_t>(v + (1 << 20)) & 0x1FFFFFull;
                };
                return (u(ix) << 42) | (u(iy) << 21) | u(iz);
            }
            const std::uint32_t ux = static_cast<std::uint32_t>(ix) ^ 0x80000000u;
            const std::uint32_t uy = static_cast<std::uint32_t>(iy) ^ 0x80000000u;
            return (static_cast<std::uint64_t>(ux) << 32) | static_cast<std::uint64_t>(uy);
        }

        [[nodiscard]] int FloorToInt(float v)
        {
            return static_cast<int>(std::floor(v));
        }

        struct CellCoord
        {
            int X = 0, Y = 0, Z = 0;
        };

        [[nodiscard]] CellCoord PointCell(float px, float py, float pz,
                                          float invCell, float ox, float oy, float oz, int dim)
        {
            CellCoord c;
            c.X = FloorToInt((px + ox) * invCell);
            c.Y = FloorToInt((py + oy) * invCell);
            c.Z = (dim == 3) ? FloorToInt((pz + oz) * invCell) : 0;
            return c;
        }

        [[nodiscard]] int PhaseOfCell(const CellCoord& c, int dim)
        {
            if (dim == 3)
                return (c.X & 1) | ((c.Y & 1) << 1) | ((c.Z & 1) << 2);
            return (c.X & 1) | ((c.Y & 1) << 1);
        }

        // Accepted-point spatial map at one level's cell resolution: packed cell
        // key -> point indices occupying it.
        using CellMap = std::unordered_map<std::uint64_t, std::vector<std::uint32_t>>;

        [[nodiscard]] bool HasNeighborWithin(const CellMap& map,
                                             const float* px, const float* py, const float* pz,
                                             float qx, float qy, float qz,
                                             float invCell, float rSq,
                                             float ox, float oy, float oz, int dim)
        {
            const CellCoord c = PointCell(qx, qy, qz, invCell, ox, oy, oz, dim);
            const int zlo = (dim == 3) ? -1 : 0;
            const int zhi = (dim == 3) ? 1 : 0;
            for (int dz = zlo; dz <= zhi; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const std::uint64_t key = PackCell(c.X + dx, c.Y + dy, c.Z + dz, dim);
                        const auto it = map.find(key);
                        if (it == map.end())
                            continue;
                        for (const std::uint32_t idx : it->second)
                        {
                            const float ex = px[idx] - qx;
                            const float ey = py[idx] - qy;
                            const float ez = (dim == 3) ? (pz[idx] - qz) : 0.0f;
                            if (ex * ex + ey * ey + ez * ez < rSq)
                                return true;
                        }
                    }
            return false;
        }

        // Build a uniform spatial-hash grid (packed cell key -> point indices) at
        // edge length `cell` over order[0..count), origin 0.
        [[nodiscard]] CellMap BuildGrid(const float* px, const float* py, const float* pz,
                                        std::span<const std::uint32_t> order, std::uint32_t count,
                                        int dim, float cell)
        {
            const float invCell = 1.0f / cell;
            CellMap map;
            map.reserve(count);
            for (std::uint32_t r = 0; r < count; ++r)
            {
                const std::uint32_t idx = order[r];
                const CellCoord c = PointCell(px[idx], py[idx], (dim == 3 ? pz[idx] : 0.0f),
                                              invCell, 0, 0, 0, dim);
                map[PackCell(c.X, c.Y, c.Z, dim)].push_back(idx);
            }
            return map;
        }

        // Exact squared nearest-neighbor distance for point `idx` among the points
        // stored in `map` (a uniform grid at edge length `cell`), excluding idx.
        //
        // Searches outward in L-infinity shells and stops as soon as the best
        // distance found is no larger than the closest possible point in any
        // unsearched shell (best^2 <= (shell*cell)^2) — so the result is the EXACT
        // nearest neighbor regardless of how many cells separate the pair, not just
        // the immediate 3^d neighborhood. `maxShell` bounds pathological inputs;
        // with a grid sized to ~1 point/cell it is reached only when no neighbor
        // exists. Returns the sentinel when no neighbor is found within maxShell.
        [[nodiscard]] float NearestNeighborSq(const CellMap& map,
                                              const float* px, const float* py, const float* pz,
                                              std::uint32_t idx, int dim, float cell, int maxShell)
        {
            const float invCell = 1.0f / cell;
            const float qx = px[idx];
            const float qy = py[idx];
            const float qz = (dim == 3) ? pz[idx] : 0.0f;
            const CellCoord c = PointCell(qx, qy, qz, invCell, 0, 0, 0, dim);

            float best = std::numeric_limits<float>::max();
            bool found = false;
            for (int shell = 0; shell <= maxShell; ++shell)
            {
                const int zlo = (dim == 3) ? -shell : 0;
                const int zhi = (dim == 3) ? shell : 0;
                for (int dz = zlo; dz <= zhi; ++dz)
                    for (int dy = -shell; dy <= shell; ++dy)
                        for (int dx = -shell; dx <= shell; ++dx)
                        {
                            // Only the cells on the current shell boundary are new.
                            const int ad = std::max({std::abs(dx), std::abs(dy),
                                                     (dim == 3) ? std::abs(dz) : 0});
                            if (ad != shell)
                                continue;
                            const auto it = map.find(PackCell(c.X + dx, c.Y + dy, c.Z + dz, dim));
                            if (it == map.end())
                                continue;
                            for (const std::uint32_t nidx : it->second)
                            {
                                if (nidx == idx)
                                    continue;
                                const float ex = px[nidx] - qx;
                                const float ey = py[nidx] - qy;
                                const float ez = (dim == 3) ? (pz[nidx] - qz) : 0.0f;
                                const float d2 = ex * ex + ey * ey + ez * ez;
                                if (d2 < best)
                                {
                                    best = d2;
                                    found = true;
                                }
                            }
                        }
                if (found)
                {
                    const float minOutside = static_cast<float>(shell) * cell;
                    if (best <= minOutside * minOutside)
                        break;
                }
            }
            return found ? best : std::numeric_limits<float>::max();
        }

        // Exact minimum pairwise distance over order[0..count). Builds one uniform
        // grid sized to ~1 point/cell from the prefix's own bounding box (the
        // single-level analog of the sampler's per-level grids) and runs the exact
        // shell search above — no brute force, correct for any separation.
        [[nodiscard]] float ExactMinPairwise(const float* px, const float* py, const float* pz,
                                             std::span<const std::uint32_t> order,
                                             std::uint32_t count, int dim)
        {
            const float sentinel = std::numeric_limits<float>::max();
            if (count < 2)
                return sentinel;

            float lo[3] = {px[order[0]], py[order[0]], (dim == 3) ? pz[order[0]] : 0.0f};
            float hi[3] = {lo[0], lo[1], lo[2]};
            for (std::uint32_t r = 1; r < count; ++r)
            {
                const std::uint32_t idx = order[r];
                lo[0] = std::min(lo[0], px[idx]);
                hi[0] = std::max(hi[0], px[idx]);
                lo[1] = std::min(lo[1], py[idx]);
                hi[1] = std::max(hi[1], py[idx]);
                if (dim == 3)
                {
                    lo[2] = std::min(lo[2], pz[idx]);
                    hi[2] = std::max(hi[2], pz[idx]);
                }
            }
            const float extent = (dim == 3)
                                     ? std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]})
                                     : std::max(hi[0] - lo[0], hi[1] - lo[1]);
            if (extent <= 0.0f)
                return 0.0f; // all points coincident

            // ~1 point per cell along each axis; sideCells >= 1.
            const int sideCells = std::max(1, static_cast<int>(
                                                  std::floor(std::pow(static_cast<double>(count),
                                                                      1.0 / static_cast<double>(dim)))));
            const float cell = extent / static_cast<float>(sideCells);
            // The whole prefix spans ~sideCells cells, so a neighbor anywhere in the
            // bbox is reached within sideCells+2 shells -> the search stays exact.
            const int maxShell = sideCells + 2;

            const CellMap map = BuildGrid(px, py, pz, order, count, dim, cell);
            float bestSq = sentinel;
            for (std::uint32_t r = 0; r < count; ++r)
            {
                const float nnSq = NearestNeighborSq(map, px, py, pz, order[r], dim, cell, maxShell);
                if (nnSq < bestSq)
                    bestSq = nnSq;
            }
            return (bestSq == sentinel) ? sentinel : std::sqrt(bestSq);
        }

        // BestOfCandidates score: squared distance to the nearest accepted point in the 3^d
        // cell neighborhood, capped at the squared cell size (points outside the neighborhood
        // are at least one cell away, so the cap is exact).
        [[nodiscard]] float LocalClearanceSq(const CellMap& map, const float* px, const float* py, const float* pz,
                                             std::uint32_t idx, float invCell, float capSq,
                                             float ox, float oy, float oz, int dim)
        {
            const float qx = px[idx], qy = py[idx], qz = (dim == 3) ? pz[idx] : 0.0f;
            const CellCoord c = PointCell(qx, qy, qz, invCell, ox, oy, oz, dim);
            float best = capSq;
            const int zlo = (dim == 3) ? -1 : 0;
            const int zhi = (dim == 3) ? 1 : 0;
            for (int dz = zlo; dz <= zhi; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const auto it = map.find(PackCell(c.X + dx, c.Y + dy, c.Z + dz, dim));
                        if (it == map.end())
                            continue;
                        for (const std::uint32_t other : it->second)
                        {
                            const float ex = px[other] - qx;
                            const float ey = py[other] - qy;
                            float d2 = ex * ex + ey * ey;
                            if (dim == 3)
                            {
                                const float ez = pz[other] - qz;
                                d2 += ez * ez;
                            }
                            best = std::min(best, d2);
                        }
                    }
            return best;
        }

        // Morton key of a position already shifted into the padded frame, normalized by the
        // padded extent; 21 bits per axis in 2-D and 3-D (the CUDA sampler's quantization).
        [[nodiscard]] std::uint32_t QuantizeUnit(float u)
        {
            constexpr std::uint32_t kMax = (1u << 21) - 1u;
            if (!(u > 0.0f))
                return 0u;
            if (u >= 1.0f)
                return kMax;
            const auto q = static_cast<std::uint32_t>(u * static_cast<float>(1u << 21));
            return q > kMax ? kMax : q;
        }

        template <int Dim>
        [[nodiscard]] std::uint64_t MortonKey(const float* px, const float* py, const float* pz, std::uint32_t id,
                                              float invExtent)
        {
            const std::uint32_t qx = QuantizeUnit(px[id] * invExtent);
            const std::uint32_t qy = QuantizeUnit(py[id] * invExtent);
            const std::uint32_t qz = Dim == 3 ? QuantizeUnit(pz[id] * invExtent) : 0u;
            std::uint64_t key = 0;
            for (std::uint32_t b = 0; b < 21u; ++b)
            {
                if constexpr (Dim == 3)
                {
                    key |= static_cast<std::uint64_t>((qx >> b) & 1u) << (3u * b);
                    key |= static_cast<std::uint64_t>((qy >> b) & 1u) << (3u * b + 1u);
                    key |= static_cast<std::uint64_t>((qz >> b) & 1u) << (3u * b + 2u);
                }
                else
                {
                    key |= static_cast<std::uint64_t>((qx >> b) & 1u) << (2u * b);
                    key |= static_cast<std::uint64_t>((qy >> b) & 1u) << (2u * b + 1u);
                }
            }
            return key;
        }

        // Permutes one completed level segment. Any subset of a level prefix keeps the level's
        // minimum distance, so neither ordering can break the guarantee.
        template <int Dim>
        void PermuteLevel(std::uint32_t* segment, std::uint32_t count, const float* px, const float* py,
                          const float* pz, float extent, WithinLevelOrdering ordering, std::uint32_t shuffleSeed,
                          std::uint32_t level)
        {
            if (count < 2)
                return;
            if (ordering == WithinLevelOrdering::RandomShuffle)
            {
                SplitMix64 rng(static_cast<std::uint64_t>(shuffleSeed) +
                               0x9e3779b9ull * (static_cast<std::uint64_t>(level) + 1u));
                for (std::uint32_t i = count - 1; i > 0; --i)
                {
                    const std::uint32_t j = rng.Bounded(i + 1);
                    std::swap(segment[i], segment[j]);
                }
                return;
            }
            // Spatially balanced: ids ascending, stable Morton sort, bit-reversed emission.
            std::vector<std::uint32_t> ids(segment, segment + count);
            std::sort(ids.begin(), ids.end());
            const float invExtent = 1.0f / extent;
            std::vector<std::uint64_t> keys(count);
            for (std::uint32_t k = 0; k < count; ++k)
                keys[k] = MortonKey<Dim>(px, py, pz, ids[k], invExtent);
            std::vector<std::uint32_t> rank(count);
            for (std::uint32_t k = 0; k < count; ++k)
                rank[k] = k;
            std::stable_sort(rank.begin(), rank.end(),
                             [&](std::uint32_t a, std::uint32_t b) { return keys[a] < keys[b]; });
            std::uint32_t bits = 0;
            while ((std::uint64_t{1} << bits) < count)
                ++bits;
            std::uint32_t out = 0;
            for (std::uint64_t j = 0; j < (std::uint64_t{1} << bits); ++j)
            {
                std::uint32_t reversed = 0;
                for (std::uint32_t b = 0; b < bits; ++b)
                    reversed |= ((static_cast<std::uint32_t>(j) >> b) & 1u) << (bits - 1u - b);
                if (reversed < count)
                    segment[out++] = ids[rank[reversed]];
            }
        }

        // Padded bounding-box frame shared by Compute and ReorderWithinLevels.
        struct Frame
        {
            float Min[3]{0.0f, 0.0f, 0.0f};
            float Pad{0.0f};
            float Extent{1.0f};
        };

        template <int Dim>
        [[nodiscard]] Frame MakeFrame(const float* hx, const float* hy, const float* hz, std::uint32_t n)
        {
            Frame f;
            float bmax[3] = {hx[0], hy[0], (Dim == 3 ? hz[0] : 0.0f)};
            f.Min[0] = hx[0]; f.Min[1] = hy[0]; f.Min[2] = Dim == 3 ? hz[0] : 0.0f;
            for (std::uint32_t i = 1; i < n; ++i)
            {
                f.Min[0] = std::min(f.Min[0], hx[i]);
                bmax[0] = std::max(bmax[0], hx[i]);
                f.Min[1] = std::min(f.Min[1], hy[i]);
                bmax[1] = std::max(bmax[1], hy[i]);
                if constexpr (Dim == 3)
                {
                    f.Min[2] = std::min(f.Min[2], hz[i]);
                    bmax[2] = std::max(bmax[2], hz[i]);
                }
            }
            float extent = (Dim == 3)
                               ? std::max({bmax[0] - f.Min[0], bmax[1] - f.Min[1], bmax[2] - f.Min[2]})
                               : std::max(bmax[0] - f.Min[0], bmax[1] - f.Min[1]);
            if (extent < 1e-12f)
                extent = 1.0f;
            f.Pad = extent * 0.01f;
            f.Extent = extent + 2.0f * f.Pad;
            return f;
        }

        template <int Dim>
        Result ComputeImpl(const float* hx, const float* hy, const float* hz,
                           std::uint32_t n, const Config& cfg, Diagnostics diag,
                           std::span<const float> priorityScores)
        {
            Result result;
            result.Diag = diag;
            result.LevelOffsets.push_back(0);
            if (n == 0)
            {
                result.Diag.Code = ValidationCode::Valid;
                return result;
            }

            // ── Normalize: bbox -> shift into the positive orthant with padding ──
            const Frame frame = MakeFrame<Dim>(hx, hy, hz, n);
            const float extent = frame.Extent;
            std::vector<float> sx(n), sy(n), sz(n, 0.0f);
            for (std::uint32_t i = 0; i < n; ++i)
            {
                sx[i] = hx[i] - frame.Min[0] + frame.Pad;
                sy[i] = hy[i] - frame.Min[1] + frame.Pad;
                if constexpr (Dim == 3)
                    sz[i] = hz[i] - frame.Min[2] + frame.Pad;
            }
            const float* px = sx.data();
            const float* py = sy.data();
            const float* pz = sz.data();

            float alpha = cfg.RadiusAlpha;
            if (!(alpha > 0.0f && alpha < 1.0f))
                alpha = 0.5f * (Dim == 3 ? kSqrt3 : kSqrt2);
            result.Diag.UsedAlpha = alpha;

            constexpr int kPhases = (Dim == 3) ? 8 : 4;
            const bool priority = cfg.Selection == CellSelection::FeaturePriority;
            const std::uint32_t retries = std::min<std::uint32_t>(cfg.MaxCellRetries, 8u);
            const std::uint32_t repairLevels = std::min(cfg.RepairCoarseLevels, cfg.MaxLevels);
            const std::uint32_t exhaustiveLevels = std::min(cfg.ExhaustiveCoarseLevels, cfg.MaxLevels);

            std::vector<std::uint32_t> remaining(n);
            for (std::uint32_t i = 0; i < n; ++i)
                remaining[i] = i;

            CellMap globalMap; // accepted-so-far at the current level resolution

            for (std::uint32_t L = 0; L < cfg.MaxLevels && !remaining.empty(); ++L)
            {
                const float cell = extent / (static_cast<float>(cfg.GridWidth) * std::ldexp(1.0f, static_cast<int>(L)));
                const float rL = cell * alpha;
                const float rSq = rL * rL;
                const float invC = 1.0f / cell;
                const float capSq = cell * cell;
                if (L == 0)
                    result.BaseRadius = rL;

                float ox = 0.0f, oy = 0.0f, oz = 0.0f;
                if (cfg.RandomizeGridOrigin)
                {
                    const std::uint32_t s = cfg.GridOriginSeed;
                    ox = U32ToUnif01(MixU32(s + 0x9e3779b9u * (3u * L + 1u))) * cell;
                    oy = U32ToUnif01(MixU32(s + 0x9e3779b9u * (3u * L + 2u))) * cell;
                    if constexpr (Dim == 3)
                        oz = U32ToUnif01(MixU32(s + 0x9e3779b9u * (3u * L + 3u))) * cell;
                }

                // Rebuild the accepted map at this resolution + origin.
                globalMap.clear();
                for (const std::uint32_t idx : result.Order)
                {
                    const CellCoord c = PointCell(px[idx], py[idx], pz[idx], invC, ox, oy, oz, Dim);
                    globalMap[PackCell(c.X, c.Y, c.Z, Dim)].push_back(idx);
                }

                // Phase visit order: ascending, or the CUDA sampler's seeded Fisher-Yates.
                std::array<int, kPhases> phases{};
                for (int ph = 0; ph < kPhases; ++ph)
                    phases[static_cast<std::size_t>(ph)] = ph;
                if (cfg.RandomizePhaseOrder)
                {
                    std::uint32_t h = MixU32(cfg.PhaseOrderSeed ^ (0x9e3779b9u * (L + 1u)));
                    for (int i = kPhases - 1; i >= 1; --i)
                    {
                        h = MixU32(h + 0x9e3779b9u);
                        std::swap(phases[static_cast<std::size_t>(i)], phases[h % static_cast<std::uint32_t>(i + 1)]);
                    }
                }

                // Per-level policy: coarse levels may be promoted to Exhaustive.
                CellSelection policy = cfg.Selection;
                if ((policy == CellSelection::Bounded || policy == CellSelection::BestOfCandidates) &&
                    L < exhaustiveLevels)
                    policy = CellSelection::Exhaustive;
                const std::uint32_t contenders = L < repairLevels ? 1u + retries : 1u;

                std::unordered_set<std::uint64_t> claimed; // one winner per cell this level
                const std::uint32_t levelStart = static_cast<std::uint32_t>(result.Order.size());
                const int bands = priority && cfg.PriorityTwoBands ? 2 : 1;
                for (int band = 0; band < bands && !remaining.empty(); ++band)
                    for (const int ph : phases)
                    {
                        if (remaining.empty())
                            break;
                        // Candidate lists per cell of this phase, in remaining order; cells in
                        // order of first appearance. Same-phase cells are >= 2 cells apart, so
                        // their winners cannot conflict and the cell order does not matter.
                        std::unordered_map<std::uint64_t, std::uint32_t> listOf;
                        std::vector<std::vector<std::uint32_t>> lists; // positions in `remaining`
                        std::vector<std::uint64_t> listKeys;
                        for (std::uint32_t pos = 0; pos < remaining.size(); ++pos)
                        {
                            const std::uint32_t idx = remaining[pos];
                            if (bands == 2 && (priorityScores[idx] >= cfg.PriorityBandThreshold) != (band == 0))
                                continue;
                            const CellCoord c = PointCell(px[idx], py[idx], pz[idx], invC, ox, oy, oz, Dim);
                            if (PhaseOfCell(c, Dim) != ph)
                                continue;
                            const std::uint64_t key = PackCell(c.X, c.Y, c.Z, Dim);
                            if (claimed.contains(key))
                                continue;
                            const auto [it, inserted] = listOf.try_emplace(key, static_cast<std::uint32_t>(lists.size()));
                            if (inserted)
                            {
                                lists.emplace_back();
                                listKeys.push_back(key);
                            }
                            lists[it->second].push_back(pos);
                        }
                        const auto feasible = [&](const std::uint32_t idx)
                        {
                            return !HasNeighborWithin(globalMap, px, py, pz, px[idx], py[idx], pz[idx], invC, rSq,
                                                      ox, oy, oz, Dim);
                        };
                        std::vector<std::uint32_t> winners; // positions in `remaining`
                        for (std::size_t k = 0; k < lists.size(); ++k)
                        {
                            const auto& list = lists[k];
                            std::uint32_t winner = std::numeric_limits<std::uint32_t>::max();
                            switch (policy)
                            {
                            case CellSelection::Exhaustive:
                            case CellSelection::Bounded:
                            {
                                const std::size_t limit = policy == CellSelection::Exhaustive
                                                              ? list.size()
                                                              : std::min<std::size_t>(list.size(), contenders);
                                for (std::size_t t = 0; t < limit; ++t)
                                    if (feasible(remaining[list[t]]))
                                    {
                                        winner = list[t];
                                        break;
                                    }
                                break;
                            }
                            case CellSelection::BestOfCandidates:
                            {
                                float best = -1.0f;
                                const std::size_t limit = std::min<std::size_t>(list.size(), cfg.CandidateBudget);
                                for (std::size_t t = 0; t < limit; ++t)
                                {
                                    const std::uint32_t idx = remaining[list[t]];
                                    if (!feasible(idx))
                                        continue;
                                    const float score = LocalClearanceSq(globalMap, px, py, pz, idx, invC, capSq, ox,
                                                                         oy, oz, Dim);
                                    if (score > best)
                                    {
                                        best = score;
                                        winner = list[t];
                                    }
                                }
                                break;
                            }
                            case CellSelection::FeaturePriority:
                            {
                                for (const std::uint32_t pos : list)
                                {
                                    const std::uint32_t idx = remaining[pos];
                                    if (!feasible(idx))
                                        continue;
                                    if (winner == std::numeric_limits<std::uint32_t>::max())
                                    {
                                        winner = pos;
                                        continue;
                                    }
                                    const std::uint32_t current = remaining[winner];
                                    const float a = priorityScores[idx], b = priorityScores[current];
                                    // Bitwise-equal scores (-0 == +0) tie to the lowest input index.
                                    if (a > b || (a == b && idx < current))
                                        winner = pos;
                                }
                                break;
                            }
                            }
                            if (winner != std::numeric_limits<std::uint32_t>::max())
                            {
                                winners.push_back(winner);
                                claimed.insert(listKeys[k]);
                            }
                        }
                        if (winners.empty())
                            continue;
                        // Emit this phase's winners in remaining order (stable), then drop them.
                        std::sort(winners.begin(), winners.end());
                        std::vector<std::uint8_t> taken(remaining.size(), 0u);
                        for (const std::uint32_t pos : winners)
                        {
                            const std::uint32_t idx = remaining[pos];
                            taken[pos] = 1u;
                            result.Order.push_back(idx);
                            const CellCoord c = PointCell(px[idx], py[idx], pz[idx], invC, ox, oy, oz, Dim);
                            globalMap[PackCell(c.X, c.Y, c.Z, Dim)].push_back(idx);
                        }
                        std::vector<std::uint32_t> next;
                        next.reserve(remaining.size() - winners.size());
                        for (std::uint32_t pos = 0; pos < remaining.size(); ++pos)
                            if (!taken[pos])
                                next.push_back(remaining[pos]);
                        remaining.swap(next);
                    }

                const std::uint32_t acceptedThisLevel =
                    static_cast<std::uint32_t>(result.Order.size()) - levelStart;
                result.LevelOffsets.push_back(static_cast<std::uint32_t>(result.Order.size()));
                result.Diag.LevelCounts.push_back(acceptedThisLevel);
                result.Diag.LevelRadii.push_back(rL);

                if (cfg.ShuffleWithinLevels && acceptedThisLevel > 1)
                    PermuteLevel<Dim>(result.Order.data() + levelStart, acceptedThisLevel, px, py, pz, extent,
                                      cfg.Ordering, cfg.ShuffleSeed, L);
            }

            result.Diag.AcceptedCount = static_cast<std::uint32_t>(result.Order.size());

            // ── Splat radii + per-level measured min-distance ──
            // Splat radius for a point introduced at level L is its nearest-neighbor
            // distance within the prefix ending at level L, measured at that level's
            // spacing scale rL_prefix = base_radius / 2^L (a rendering hint). The
            // per-level measured min-distance diagnostic is the EXACT minimum
            // pairwise distance of the prefix, found via the adaptive-grid shell
            // search (correct for sparse prefixes, not just adjacent cells).
            if (!cfg.ComputeSplatRadii)
            {
                result.Diag.Code = ValidationCode::Valid;
                return result; // order only: no radii and no per-level min distance
            }
            result.SplatRadii.assign(result.Order.size(), 0.0f);
            const int numLevels = static_cast<int>(result.LevelOffsets.size()) - 1;
            for (int L = 0; L < numLevels; ++L)
            {
                const std::uint32_t ls = result.LevelOffsets[L];
                const std::uint32_t le = result.LevelOffsets[L + 1];
                if (ls == le)
                {
                    result.Diag.LevelMinDistance.push_back(0.0f);
                    continue;
                }

                const float rLPrefix = std::ldexp(result.BaseRadius, -L);
                const float cellH = (rLPrefix > 0.0f) ? rLPrefix : 1.0f;

                const std::span<const std::uint32_t> prefix(result.Order.data(), le);
                const CellMap prefixMap = BuildGrid(px, py, pz, prefix, le, Dim, cellH);

                // The splat radius is a per-point rendering hint at this level's
                // scale; bound the search and map a missing neighbor (sentinel) to 0.
                constexpr int splatMaxShell = 64;
                for (std::uint32_t rank = ls; rank < le; ++rank)
                {
                    const float nnSq = NearestNeighborSq(prefixMap, px, py, pz,
                                                         result.Order[rank], Dim, cellH, splatMaxShell);
                    result.SplatRadii[rank] = (nnSq == std::numeric_limits<float>::max())
                                                  ? 0.0f
                                                  : std::sqrt(nnSq);
                }

                const float exactMin = ExactMinPairwise(px, py, pz, prefix, le, Dim);
                result.Diag.LevelMinDistance.push_back(
                    (exactMin == std::numeric_limits<float>::max()) ? 0.0f : exactMin);
            }

            result.Diag.Code = ValidationCode::Valid;
            return result;
        }

        [[nodiscard]] bool AllFinite(std::span<const glm::vec3> v, bool use3d)
        {
            for (const glm::vec3& p : v)
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || (use3d && !std::isfinite(p.z)))
                    return false;
            return true;
        }
    } // namespace

    Config WithProfile(Config base, const Profile profile)
    {
        switch (profile)
        {
        case Profile::Fast: base.Selection = CellSelection::Bounded; base.MaxCellRetries = 0; base.RepairCoarseLevels = 0; break;
        case Profile::Balanced: base.Selection = CellSelection::Bounded; base.MaxCellRetries = 1; base.RepairCoarseLevels = 4; break;
        case Profile::Quality: base.Selection = CellSelection::Bounded; base.MaxCellRetries = 2; base.RepairCoarseLevels = 4; break;
        case Profile::Hapds: base.Selection = CellSelection::Exhaustive; break;
        }
        return base;
    }

    Result Compute(std::span<const glm::vec3> points, const Config& configIn, std::span<const float> priorityScores)
    {
        Config cfg = configIn;
        Diagnostics diag;
        diag.InputCount = static_cast<std::uint32_t>(points.size());

        Result invalid;
        invalid.LevelOffsets.push_back(0);
        invalid.Diag = diag;

        if (cfg.Dimension != 2 && cfg.Dimension != 3)
        {
            invalid.Diag.Code = ValidationCode::InvalidDimension;
            return invalid;
        }
        const bool use3d = cfg.Dimension == 3;
        if (!AllFinite(points, use3d))
        {
            invalid.Diag.Code = ValidationCode::NonFiniteInput;
            return invalid;
        }

        // Policy misuse fails closed, as the CUDA sampler's argument checks do.
        const bool priority = cfg.Selection == CellSelection::FeaturePriority;
        if (cfg.Selection > CellSelection::FeaturePriority || cfg.Ordering > WithinLevelOrdering::SpatiallyBalanced ||
            (priority && ((!points.empty() && priorityScores.size() != points.size()) ||
                          !std::all_of(priorityScores.begin(), priorityScores.end(), [](float v) { return std::isfinite(v); }) ||
                          !std::isfinite(cfg.PriorityBandThreshold) || cfg.ExhaustiveCoarseLevels != 0u)) ||
            (!priority && (!priorityScores.empty() || cfg.PriorityTwoBands)) ||
            (cfg.Selection == CellSelection::BestOfCandidates && (cfg.CandidateBudget < 1u || cfg.CandidateBudget > 32u)))
        {
            invalid.Diag.Code = ValidationCode::InvalidConfig;
            return invalid;
        }

        if (cfg.GridWidth == 0)
        {
            cfg.GridWidth = 1;
            diag.ClampedGridWidth = true;
        }
        if (cfg.MaxLevels == 0)
        {
            cfg.MaxLevels = 1;
            diag.ClampedMaxLevels = true;
        }
        diag.AlphaDefaulted = !(cfg.RadiusAlpha > 0.0f && cfg.RadiusAlpha < 1.0f);

        // Deinterleave once into cache-friendly SoA for the hashing passes.
        const auto n = static_cast<std::uint32_t>(points.size());
        std::vector<float> px(n), py(n), pz(n, 0.0f);
        for (std::uint32_t i = 0; i < n; ++i)
        {
            px[i] = points[i].x;
            py[i] = points[i].y;
            if (use3d)
                pz[i] = points[i].z;
        }

        if (use3d)
            return ComputeImpl<3>(px.data(), py.data(), pz.data(), n, cfg, diag, priorityScores);
        return ComputeImpl<2>(px.data(), py.data(), nullptr, n, cfg, diag, priorityScores);
    }

    Result ReorderWithinLevels(const Result& cached, std::span<const glm::vec3> points, std::uint32_t dimension,
                               WithinLevelOrdering ordering, std::uint32_t shuffleSeed)
    {
        Result out = cached;
        const auto fail = [&](ValidationCode code) {
            Result invalid;
            invalid.LevelOffsets.push_back(0);
            invalid.Diag = cached.Diag;
            invalid.Diag.Code = code;
            return invalid;
        };
        if (dimension != 2 && dimension != 3)
            return fail(ValidationCode::InvalidDimension);
        const auto n = static_cast<std::uint32_t>(points.size());
        const auto& offsets = cached.LevelOffsets;
        bool wellFormed = !offsets.empty() && offsets.front() == 0u && offsets.back() == cached.Order.size() &&
                          std::is_sorted(offsets.begin(), offsets.end()) &&
                          (cached.SplatRadii.empty() || cached.SplatRadii.size() == cached.Order.size()) &&
                          ordering <= WithinLevelOrdering::SpatiallyBalanced;
        std::vector<std::uint8_t> seen(n, 0u);
        for (const std::uint32_t id : cached.Order)
        {
            if (id >= n || seen[id]) { wellFormed = false; break; }
            seen[id] = 1u;
        }
        if (!wellFormed)
            return fail(ValidationCode::InvalidConfig);
        if (cached.Order.empty())
            return out;
        if (!AllFinite(points, dimension == 3))
            return fail(ValidationCode::NonFiniteInput);
        std::vector<float> hx(n), hy(n), hz(n, 0.0f);
        for (std::uint32_t i = 0; i < n; ++i)
        {
            hx[i] = points[i].x;
            hy[i] = points[i].y;
            if (dimension == 3) hz[i] = points[i].z;
        }
        const auto run = [&]<int Dim>() {
            const Frame frame = MakeFrame<Dim>(hx.data(), hy.data(), hz.data(), n);
            std::vector<float> sx(n), sy(n), sz(n, 0.0f);
            for (std::uint32_t i = 0; i < n; ++i)
            {
                sx[i] = hx[i] - frame.Min[0] + frame.Pad;
                sy[i] = hy[i] - frame.Min[1] + frame.Pad;
                if constexpr (Dim == 3) sz[i] = hz[i] - frame.Min[2] + frame.Pad;
            }
            // Radii belong to ids; carry them through the permutation.
            std::vector<float> radiusOf;
            if (!cached.SplatRadii.empty())
            {
                radiusOf.assign(n, 0.0f);
                for (std::size_t r = 0; r < cached.Order.size(); ++r) radiusOf[cached.Order[r]] = cached.SplatRadii[r];
            }
            for (std::size_t L = 0; L + 1 < offsets.size(); ++L)
                PermuteLevel<Dim>(out.Order.data() + offsets[L], offsets[L + 1] - offsets[L], sx.data(), sy.data(),
                                  sz.data(), frame.Extent, ordering, shuffleSeed, static_cast<std::uint32_t>(L));
            if (!radiusOf.empty())
                for (std::size_t r = 0; r < out.Order.size(); ++r) out.SplatRadii[r] = radiusOf[out.Order[r]];
        };
        if (dimension == 3) run.template operator()<3>();
        else run.template operator()<2>();
        return out;
    }

    float MinPairwiseDistance(std::span<const glm::vec3> points, std::span<const std::uint32_t> order,
                              std::uint32_t count, std::uint32_t dimension)
    {
        if (count < 2)
            return std::numeric_limits<float>::max();
        const int dim = (dimension == 3) ? 3 : 2;
        const auto n = static_cast<std::uint32_t>(points.size());

        // Deinterleave into SoA once so the shared grid search can index by point id.
        std::vector<float> px(n), py(n), pz(n, 0.0f);
        for (std::uint32_t i = 0; i < n; ++i)
        {
            px[i] = points[i].x;
            py[i] = points[i].y;
            if (dim == 3)
                pz[i] = points[i].z;
        }
        return ExactMinPairwise(px.data(), py.data(), pz.data(), order, count, dim);
    }
}
