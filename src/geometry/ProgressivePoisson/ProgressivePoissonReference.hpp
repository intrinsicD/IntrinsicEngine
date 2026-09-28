#pragma once

/// @file ProgressivePoissonReference.hpp
/// @brief CPU reference backend for progressive Poisson-disk subsampling.
///
/// Method: geometry.progressive_poisson (METHOD-012). Canonical truth for the
/// "GPU-Accelerated Progressive Poisson Disk Sampling via Phase-Parallel Spatial
/// Hashing" draft. Given an unordered set of points in R^d (d in {2,3}), computes
/// a progressive ordering of an accepted subset M <= N such that every prefix
/// [0,k) is a Poisson-disk sampling at the radius of its hierarchy level.
///
/// Input is a plain span of points: `std::span<const glm::vec3>`. It is NOT tied
/// to any container — pass `PointCloud::Positions()`, a mesh's vertex positions,
/// or any contiguous `glm::vec3` property buffer directly. For 2D sampling, set
/// `Config::Dimension = 2`; the z component of each point is then ignored.
///
/// It is hermetic: it uses only the standard library and `glm` (a math primitive,
/// not an engine layer). It is compiled into the geometry layer (GEOM-111), where
/// `Geometry.PointSampling` offers it as `Method::ProgressivePoisson`; the method
/// package `methods/geometry/progressive_poisson` keeps its docs and manifest. The
/// GPU backend and CPU/GPU parity are out of scope here (METHOD-013).

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

namespace Intrinsic::Methods::Geometry::ProgressivePoissonReference
{
    inline constexpr const char* kMethodId = "geometry.progressive_poisson";
    inline constexpr const char* kBackendId = "cpu_reference";

    /// Terminal validation result for a reference run.
    enum class ValidationCode : std::uint8_t
    {
        Valid,            ///< Ran to completion (includes the empty-input case).
        InvalidDimension, ///< Config.Dimension not in {2,3}.
        NonFiniteInput,   ///< A used input coordinate was NaN/Inf.
        InvalidConfig     ///< Policy misuse: priority scores missing/non-finite/unexpected,
                          ///< two bands or coarse exhaustive levels with the wrong policy,
                          ///< candidate budget outside [1, 32], malformed cached result.
    };

    /// How a level cell chooses among its candidates in one phase (GEOM-112; mirrors the
    /// CUDA `CellSelectionPolicy`). The CPU reference fixes the candidate list order to the
    /// order of the remaining points, where the GPU arbitrates it.
    enum class CellSelection : std::uint8_t
    {
        Bounded = 0,          ///< Test at most 1 + retries candidates (retries only on the
                              ///< RepairCoarseLevels coarsest levels); a cell can stay empty.
        Exhaustive = 1,       ///< Test candidates until the first feasible one (HAPDS saturation).
        BestOfCandidates = 2, ///< Among the first CandidateBudget feasible candidates, the largest
                              ///< squared clearance to earlier points in the 3^d neighborhood,
                              ///< capped at the squared cell size (ties: earliest).
        FeaturePriority = 3   ///< Among all feasible candidates, the highest priority score
                              ///< (ties: lowest input index); needs PriorityScores.
    };

    /// Permutation applied within each completed level (any subset of a level prefix keeps
    /// the minimum distance).
    enum class WithinLevelOrdering : std::uint8_t
    {
        RandomShuffle = 0,    ///< Seeded Fisher-Yates (SplitMix64).
        SpatiallyBalanced = 1 ///< Stable Morton order of the normalized position (ties: index),
                              ///< emitted in bit-reversed rank order.
    };

    /// Sampler knobs. Mirrors the reference `SamplerConfig`
    /// (code/progressive_poisson.h) one-to-one so every paper knob is reachable.
    struct Config
    {
        std::uint32_t Dimension = 3;        ///< Spatial dimension (2 or 3); 2 ignores point.z.
        std::uint32_t GridWidth = 4;        ///< Cells per side at level 0 (clamped to >= 1).
        std::uint32_t MaxLevels = 16;       ///< Max hierarchy depth (clamped to >= 1).
        float HashLoadFactor = 0.25f;       ///< Reserved for backend parity; unused by the CPU map.
        float RadiusAlpha = -1.0f;          ///< r_L = alpha * cell; any value outside (0,1) selects sqrt(d)/2.
        bool RandomizeGridOrigin = true;    ///< Per-level grid-origin jitter for structured inputs.
        std::uint32_t GridOriginSeed = 1337u;
        bool ShuffleWithinLevels = true;    ///< Permute each level's segment so mid-level prefixes densify uniformly.
        std::uint32_t ShuffleSeed = 0x51ed270bu;
        // GEOM-112: the remaining CUDA sampler options.
        CellSelection Selection = CellSelection::Exhaustive;
        std::uint32_t MaxCellRetries = 1;        ///< Bounded: extra contenders per cell (clamped to 8).
        std::uint32_t RepairCoarseLevels = 4;    ///< Bounded: coarsest levels that use the retries.
        std::uint32_t ExhaustiveCoarseLevels = 0;///< Bounded/BestOfCandidates: coarsest levels run Exhaustive.
        std::uint32_t CandidateBudget = 4;       ///< BestOfCandidates: inspected candidates, [1, 32].
        bool RandomizePhaseOrder = false;        ///< Visit the 2^d phases of a level in a seeded permutation.
        std::uint32_t PhaseOrderSeed = 0x2f1a9c53u;
        bool PriorityTwoBands = false;           ///< FeaturePriority: scores >= threshold first, then the rest.
        float PriorityBandThreshold = 0.0f;
        WithinLevelOrdering Ordering = WithinLevelOrdering::RandomShuffle; ///< Used when ShuffleWithinLevels.
        bool ComputeSplatRadii = true;           ///< false: order only (no radii, no per-level min distance).
    };

    /// Named profiles of the CUDA sampler (Fast: Bounded 0/0, Balanced: Bounded 1/4,
    /// Quality: Bounded 2/4, HAPDS: Exhaustive), applied to a base config.
    enum class Profile : std::uint8_t { Fast = 0, Balanced, Quality, Hapds };
    [[nodiscard]] Config WithProfile(Config base, Profile profile);

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
    /// including the empty case (then `LevelOffsets == {0}`). `Order` indexes into
    /// the caller's input span.
    struct Result
    {
        std::vector<std::uint32_t> Order{};        ///< order[i] indexes the accepted input point at rank i.
        std::vector<std::uint32_t> LevelOffsets{}; ///< First rank of each level; back() = accepted count M.
        std::vector<float> SplatRadii{};           ///< Per-point introduction-level NN spacing; size == Order.size().
        float BaseRadius = 0.0f;                   ///< r_0 (coarsest); r_L = BaseRadius / 2^L.
        Diagnostics Diag{};
    };

    /// Compute the progressive Poisson-disk ordering (CPU reference) over a span
    /// of points. Deterministic for a fixed (points, config). Fails closed with an
    /// explicit diagnostic code (and an empty ordering) on invalid input. The
    /// caller owns `points`; the result references it only by index.
    [[nodiscard]] Result Compute(std::span<const glm::vec3> points, const Config& config,
                                 std::span<const float> priorityScores = {});

    /// Re-permute every level segment of an existing result with `ordering` without
    /// re-running selection: same ids per level, same level offsets and base radius, and each
    /// id keeps its splat radius. `points` are the original input the result refers to.
    /// Fails closed (InvalidConfig, empty order) on a malformed cached result.
    [[nodiscard]] Result ReorderWithinLevels(const Result& cached, std::span<const glm::vec3> points,
                                             std::uint32_t dimension, WithinLevelOrdering ordering,
                                             std::uint32_t shuffleSeed = 0x51ed270bu);

    /// Exact measured minimum pairwise distance over the prefix `order[0..count)`.
    /// Builds a uniform grid sized to ~1 point/cell from the prefix's own bounding
    /// box and runs an expanding-shell nearest-neighbor search, so the result is
    /// correct for any separation (it is NOT limited to adjacent cells). Runs in
    /// O(count) expected time — no brute force. Returns
    /// `std::numeric_limits<float>::max()` when fewer than two points are present,
    /// and 0 when all points are coincident.
    [[nodiscard]] float MinPairwiseDistance(std::span<const glm::vec3> points,
                                            std::span<const std::uint32_t> order,
                                            std::uint32_t count,
                                            std::uint32_t dimension);
}
