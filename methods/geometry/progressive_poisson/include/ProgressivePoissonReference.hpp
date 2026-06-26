#pragma once

/// @file ProgressivePoissonReference.hpp
/// @brief CPU reference backend for progressive Poisson-disk subsampling.
///
/// Method: geometry.progressive_poisson (METHOD-012). Canonical truth for the
/// "GPU-Accelerated Progressive Poisson Disk Sampling via Phase-Parallel Spatial
/// Hashing" draft. Given an unordered point set in R^d (d in {2,3}), computes a
/// progressive ordering of an accepted subset M <= N such that every prefix
/// [0,k) is a Poisson-disk sampling at the radius of its hierarchy level.
///
/// This package is hermetic: only the C++ standard library is used. No engine
/// layers (core/geometry/runtime/graphics) are imported. The GPU backend and
/// CPU/GPU parity are out of scope here (METHOD-013).

#include <cstdint>
#include <span>
#include <vector>

namespace Intrinsic::Methods::Geometry::ProgressivePoissonReference
{
    inline constexpr const char* kMethodId = "geometry.progressive_poisson";
    inline constexpr const char* kBackendId = "cpu_reference";

    /// Terminal validation result for a reference run.
    enum class ValidationCode : std::uint8_t
    {
        Valid,            ///< Ran to completion (includes the empty-input case).
        InvalidDimension, ///< Config.Dimension not in {2,3}.
        NonFiniteInput,   ///< An input coordinate was NaN/Inf.
        DimensionMismatch ///< 3D requested but no Z channel supplied (or vice versa).
    };

    /// Sampler knobs. Mirrors the reference `SamplerConfig`
    /// (code/progressive_poisson.h) one-to-one so every paper knob is reachable.
    struct Config
    {
        std::uint32_t Dimension = 3;        ///< Spatial dimension (2 or 3).
        std::uint32_t GridWidth = 4;        ///< Cells per side at level 0 (clamped to >= 1).
        std::uint32_t MaxLevels = 16;       ///< Max hierarchy depth (clamped to >= 1).
        float HashLoadFactor = 0.25f;       ///< Reserved for backend parity; unused by the CPU map.
        float RadiusAlpha = -1.0f;          ///< r_L = alpha * cell; any value outside (0,1) selects sqrt(d)/2.
        bool RandomizeGridOrigin = true;    ///< Per-level grid-origin jitter for structured inputs.
        std::uint32_t GridOriginSeed = 1337u;
        bool ShuffleWithinLevels = true;    ///< Permute each level's segment so mid-level prefixes densify uniformly.
        std::uint32_t ShuffleSeed = 0x51ed270bu;
    };

    /// Non-owning SoA view of the input points. `Z` may be empty for 2D.
    /// `X`, `Y` (and `Z` when present) must have equal length = N.
    struct PointSet
    {
        std::span<const float> X{};
        std::span<const float> Y{};
        std::span<const float> Z{};
    };

    struct Diagnostics
    {
        ValidationCode Code = ValidationCode::Valid;
        std::uint32_t InputCount = 0;
        std::uint32_t AcceptedCount = 0;
        float UsedAlpha = 0.0f;             ///< Effective radius_alpha after defaulting.
        bool ClampedGridWidth = false;      ///< GridWidth was 0 and clamped to 1.
        bool ClampedMaxLevels = false;      ///< MaxLevels was 0 and clamped to 1.
        bool AlphaDefaulted = false;        ///< RadiusAlpha was out of (0,1); sqrt(d)/2 used.
        std::vector<std::uint32_t> LevelCounts;   ///< Accepted points per level.
        std::vector<float> LevelRadii;            ///< r_L per level.
        std::vector<float> LevelMinDistance;      ///< Measured min pairwise distance of the prefix at each level boundary.
    };

    /// Result of a reference run. `LevelOffsets.back() == Order.size()` always,
    /// including the empty case (then `LevelOffsets == {0}`).
    struct Result
    {
        std::vector<std::uint32_t> Order{};        ///< order[i] indexes the accepted input point at rank i.
        std::vector<std::uint32_t> LevelOffsets{}; ///< First rank of each level; back() = accepted count M.
        std::vector<float> SplatRadii{};           ///< Per-point introduction-level NN spacing; size == Order.size().
        float BaseRadius = 0.0f;                   ///< r_0 (coarsest); r_L = BaseRadius / 2^L.
        Diagnostics Diag{};
    };

    /// Compute the progressive Poisson-disk ordering (CPU reference).
    /// Deterministic for a fixed (points, config). Fails closed with an explicit
    /// diagnostic code (and an empty ordering) on invalid input.
    [[nodiscard]] Result Compute(const PointSet& points, const Config& config);

    /// Convenience: measured minimum pairwise distance over the prefix
    /// `order[0..count)`, using a uniform spatial hash at cell size `radius`.
    /// Returns a large sentinel when fewer than two points are present.
    [[nodiscard]] float MinPairwiseDistance(const PointSet& points,
                                            std::span<const std::uint32_t> order,
                                            std::uint32_t count,
                                            std::uint32_t dimension,
                                            float radius);
}
