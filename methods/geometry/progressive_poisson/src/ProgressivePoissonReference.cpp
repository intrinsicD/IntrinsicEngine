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

        template <int Dim>
        Result ComputeImpl(const float* hx, const float* hy, const float* hz,
                           std::uint32_t n, const Config& cfg, Diagnostics diag)
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
            float bmin[3] = {hx[0], hy[0], (Dim == 3 ? hz[0] : 0.0f)};
            float bmax[3] = {hx[0], hy[0], (Dim == 3 ? hz[0] : 0.0f)};
            for (std::uint32_t i = 1; i < n; ++i)
            {
                bmin[0] = std::min(bmin[0], hx[i]);
                bmax[0] = std::max(bmax[0], hx[i]);
                bmin[1] = std::min(bmin[1], hy[i]);
                bmax[1] = std::max(bmax[1], hy[i]);
                if constexpr (Dim == 3)
                {
                    bmin[2] = std::min(bmin[2], hz[i]);
                    bmax[2] = std::max(bmax[2], hz[i]);
                }
            }

            float extent = (Dim == 3)
                               ? std::max({bmax[0] - bmin[0], bmax[1] - bmin[1], bmax[2] - bmin[2]})
                               : std::max(bmax[0] - bmin[0], bmax[1] - bmin[1]);
            if (extent < 1e-12f)
                extent = 1.0f;
            const float pad = extent * 0.01f;
            extent += 2.0f * pad;

            std::vector<float> sx(n), sy(n), sz(n, 0.0f);
            for (std::uint32_t i = 0; i < n; ++i)
            {
                sx[i] = hx[i] - bmin[0] + pad;
                sy[i] = hy[i] - bmin[1] + pad;
                if constexpr (Dim == 3)
                    sz[i] = hz[i] - bmin[2] + pad;
            }
            const float* px = sx.data();
            const float* py = sy.data();
            const float* pz = sz.data();

            float alpha = cfg.RadiusAlpha;
            if (!(alpha > 0.0f && alpha < 1.0f))
                alpha = 0.5f * (Dim == 3 ? kSqrt3 : kSqrt2);
            result.Diag.UsedAlpha = alpha;

            constexpr int kPhases = (Dim == 3) ? 8 : 4;

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

                std::unordered_set<std::uint64_t> claimed; // one winner per cell this level
                const std::uint32_t levelStart = static_cast<std::uint32_t>(result.Order.size());

                for (int ph = 0; ph < kPhases && !remaining.empty(); ++ph)
                {
                    std::vector<std::uint32_t> next;
                    next.reserve(remaining.size());
                    for (const std::uint32_t idx : remaining)
                    {
                        const float qx = px[idx];
                        const float qy = py[idx];
                        const float qz = pz[idx];
                        const CellCoord c = PointCell(qx, qy, qz, invC, ox, oy, oz, Dim);
                        if (PhaseOfCell(c, Dim) != ph)
                        {
                            next.push_back(idx);
                            continue;
                        }
                        if (HasNeighborWithin(globalMap, px, py, pz, qx, qy, qz, invC, rSq, ox, oy, oz, Dim))
                        {
                            next.push_back(idx); // conflict — carry to next level
                            continue;
                        }
                        const std::uint64_t key = PackCell(c.X, c.Y, c.Z, Dim);
                        if (!claimed.insert(key).second)
                        {
                            next.push_back(idx); // cell already taken this level
                            continue;
                        }
                        // Accept.
                        result.Order.push_back(idx);
                        globalMap[key].push_back(idx);
                    }
                    remaining.swap(next);
                }

                const std::uint32_t acceptedThisLevel =
                    static_cast<std::uint32_t>(result.Order.size()) - levelStart;
                result.LevelOffsets.push_back(static_cast<std::uint32_t>(result.Order.size()));
                result.Diag.LevelCounts.push_back(acceptedThisLevel);
                result.Diag.LevelRadii.push_back(rL);

                if (cfg.ShuffleWithinLevels && acceptedThisLevel > 1)
                {
                    SplitMix64 rng(static_cast<std::uint64_t>(cfg.ShuffleSeed) +
                                   0x9e3779b9ull * (static_cast<std::uint64_t>(L) + 1u));
                    for (std::uint32_t i = acceptedThisLevel - 1; i > 0; --i)
                    {
                        const std::uint32_t j = rng.Bounded(i + 1);
                        std::swap(result.Order[levelStart + i], result.Order[levelStart + j]);
                    }
                }
            }

            result.Diag.AcceptedCount = static_cast<std::uint32_t>(result.Order.size());

            // ── Splat radii + per-level measured min-distance ──
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
                const float invH = 1.0f / cellH;

                // Hash of the whole prefix up to the end of this level (origin 0).
                CellMap prefixMap;
                for (std::uint32_t rank = 0; rank < le; ++rank)
                {
                    const std::uint32_t idx = result.Order[rank];
                    const CellCoord c = PointCell(px[idx], py[idx], pz[idx], invH, 0, 0, 0, Dim);
                    prefixMap[PackCell(c.X, c.Y, c.Z, Dim)].push_back(idx);
                }

                float levelMinDist = std::numeric_limits<float>::max();
                constexpr int kMaxShell = 64;
                for (std::uint32_t rank = ls; rank < le; ++rank)
                {
                    const std::uint32_t idx = result.Order[rank];
                    const float qx = px[idx];
                    const float qy = py[idx];
                    const float qz = pz[idx];
                    const CellCoord c = PointCell(qx, qy, qz, invH, 0, 0, 0, Dim);

                    float bestD2 = std::numeric_limits<float>::max();
                    bool found = false;
                    for (int shell = 0; shell <= kMaxShell; ++shell)
                    {
                        const int zlo = (Dim == 3) ? -shell : 0;
                        const int zhi = (Dim == 3) ? shell : 0;
                        for (int dz = zlo; dz <= zhi; ++dz)
                            for (int dy = -shell; dy <= shell; ++dy)
                                for (int dx = -shell; dx <= shell; ++dx)
                                {
                                    const int ad = std::max({std::abs(dx), std::abs(dy), (Dim == 3) ? std::abs(dz) : 0});
                                    if (ad != shell)
                                        continue;
                                    const auto it = prefixMap.find(PackCell(c.X + dx, c.Y + dy, c.Z + dz, Dim));
                                    if (it == prefixMap.end())
                                        continue;
                                    for (const std::uint32_t nidx : it->second)
                                    {
                                        if (nidx == idx)
                                            continue;
                                        const float ex = px[nidx] - qx;
                                        const float ey = py[nidx] - qy;
                                        const float ez = (Dim == 3) ? (pz[nidx] - qz) : 0.0f;
                                        const float d2 = ex * ex + ey * ey + ez * ez;
                                        if (d2 < bestD2)
                                        {
                                            bestD2 = d2;
                                            found = true;
                                        }
                                    }
                                }
                        if (found)
                        {
                            const float minOutside = static_cast<float>(shell) * cellH;
                            if (bestD2 <= minOutside * minOutside)
                                break;
                        }
                    }
                    const float d = found ? std::sqrt(bestD2) : 0.0f;
                    result.SplatRadii[rank] = d;
                    if (found)
                        levelMinDist = std::min(levelMinDist, d);
                }
                result.Diag.LevelMinDistance.push_back(
                    (levelMinDist == std::numeric_limits<float>::max()) ? 0.0f : levelMinDist);
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

    Result Compute(std::span<const glm::vec3> points, const Config& configIn)
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
            return ComputeImpl<3>(px.data(), py.data(), pz.data(), n, cfg, diag);
        return ComputeImpl<2>(px.data(), py.data(), nullptr, n, cfg, diag);
    }

    float MinPairwiseDistance(std::span<const glm::vec3> points, std::span<const std::uint32_t> order,
                              std::uint32_t count, std::uint32_t dimension, float radius)
    {
        const float sentinel = std::numeric_limits<float>::max();
        if (count < 2 || radius <= 0.0f)
            return sentinel;
        const int dim = (dimension == 3) ? 3 : 2;
        const float invCell = 1.0f / radius;
        auto px = [&](std::uint32_t i) { return points[i].x; };
        auto py = [&](std::uint32_t i) { return points[i].y; };
        auto pz = [&](std::uint32_t i) { return (dim == 3) ? points[i].z : 0.0f; };

        CellMap map;
        for (std::uint32_t r = 0; r < count; ++r)
        {
            const std::uint32_t idx = order[r];
            const CellCoord c = PointCell(px(idx), py(idx), pz(idx), invCell, 0, 0, 0, dim);
            map[PackCell(c.X, c.Y, c.Z, dim)].push_back(idx);
        }

        float best = sentinel;
        const int zlo = (dim == 3) ? -1 : 0;
        const int zhi = (dim == 3) ? 1 : 0;
        for (std::uint32_t r = 0; r < count; ++r)
        {
            const std::uint32_t idx = order[r];
            const float qx = px(idx);
            const float qy = py(idx);
            const float qz = pz(idx);
            const CellCoord c = PointCell(qx, qy, qz, invCell, 0, 0, 0, dim);
            for (int dz = zlo; dz <= zhi; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const auto it = map.find(PackCell(c.X + dx, c.Y + dy, c.Z + dz, dim));
                        if (it == map.end())
                            continue;
                        for (const std::uint32_t nidx : it->second)
                        {
                            if (nidx == idx)
                                continue;
                            const float ex = px(nidx) - qx;
                            const float ey = py(nidx) - qy;
                            const float ez = pz(nidx) - qz;
                            const float d2 = ex * ex + ey * ey + ez * ez;
                            best = std::min(best, d2);
                        }
                    }
        }
        return (best == sentinel) ? sentinel : std::sqrt(best);
    }
}
