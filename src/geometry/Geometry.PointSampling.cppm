// Unified progressive point sampling (GEOM-111): orders the points of a point set so that
// every prefix of the order is a usable subsample, with a selectable, deterministic method.
// It is the single sampling entry point for consumers that pick points (registration
// subsampling and landmarks, consolidation seeds, the editor subsampling operation) and the
// CPU parity reference for GPU ports.
//
// Methods (CPU ports of the operator's sampling research; GPL-derived code is not used):
//   - Random:        seeded uniform permutation (portable: mt19937_64 with Lemire bounds).
//   - FarthestPoint: exact farthest-point ("largest hole") order through a hole sieve: points
//                    are Morton-sorted into leaf blocks under a static binary tree whose nodes
//                    keep their AABB, the largest clearance of their unselected points and its
//                    winner. Adding a sample updates only nodes whose AABB lies within their
//                    largest clearance, so the order and every clearance equal a brute-force
//                    float64 scan (tie: smallest index) while visiting a fraction of the pairs.
//                    Optional importance weights w_i > 0 pick the largest w_i * clearance_i.
//   - ProgressivePoisson: phase-parallel progressive Poisson-disk sampling (METHOD-012
//                    reference, src/geometry/ProgressivePoisson): a hierarchy of grid levels
//                    with radius r_L = r_0 / 2^L; every prefix ending at a level boundary is a
//                    Poisson-disk set at that level's radius. It accepts a subset M <= N, so
//                    its order can be shorter than requested.
//   - CoupledSieve:  eta-relaxed farthest-point batches over the same sieve (GEOM-113; CPU
//                    reference of the CUDA coupled sieve): a batch admits, in decreasing
//                    batch-start priority, candidates whose priority recomputed against the
//                    batch's earlier picks is >= eta^2 U (U: the batch's exact winner); the
//                    first rejection ends the batch. eta = 1 (with a one-point cap) is exact FPS.
//   - FlatGreedy:    beta-greedy batches (CPU reference of the CUDA flat greedy): each batch
//                    is an independent set, in the radius U / beta conflict graph, of the
//                    points whose clearance is >= U / beta (U: the largest clearance), chosen
//                    by seeded priority rounds (random, clearance or sampled coverage gain);
//                    every emitted point keeps clearance >= U / beta. beta = 1 is exact FPS.
//   - LazyGreedy:    the CUDA lazy greedy's batch rule (beta-admissible points, one per grid
//                    cell of the conflict radius, cells visited in 2^3 seeded parity phases,
//                    random or void-density priority); the CPU evaluates clearances eagerly,
//                    where the GPU exposes candidates lazily from an LBVH. beta = 1 is exact FPS.
//   - Tournament:    the LBVH tournament baseline on the Morton tree: each internal node keeps
//                    the child winner nearer its box center and emits the loser; losers are
//                    ordered by node box diagonal, largest first, after the root winner.
//   - SampleElimination: Yuksel's weighted sample elimination (EG 2015), progressive variant:
//                    eliminate the heaviest point until `count` remain, then halve repeatedly
//                    with a growing radius; later-eliminated points come first.
// Batch methods complete whole batches; the order is the same for every requested count.
// Coordinates are processed in double precision; inputs whose squared extent would overflow
// are refused.
module;

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.PointSampling;

export namespace Geometry::PointSampling
{
    enum class Method : std::uint8_t
    {
        Random = 0,
        FarthestPoint,
        ProgressivePoisson,
        CoupledSieve,
        FlatGreedy,
        SampleElimination,
        LazyGreedy,
        Tournament,
    };

    enum class GreedyPriority : std::uint8_t { Random = 0, Clearance, CoverageGain };
    enum class LazyPriority : std::uint8_t { Random = 0, VoidDensity };

    enum class Status : std::uint8_t
    {
        Success = 0,
        EmptyInput,
        NonFiniteInput,
        InvalidParameters,   // weights, first index or leaf size out of range
        UnsupportedMagnitude // squared coordinate extent would overflow double
    };

    [[nodiscard]] std::string_view ToString(Method value) noexcept;
    [[nodiscard]] std::string_view ToString(Status value) noexcept;

    // Structure-of-arrays view of 3-D points in double precision (2-D inputs use Z = 0).
    struct PointView
    {
        std::span<const double> X{}, Y{}, Z{};
        [[nodiscard]] std::size_t Size() const noexcept { return X.size(); }
    };

    // ProgressivePoisson cell policy and within-level ordering (GEOM-112; the CUDA sampler's
    // CellSelectionPolicy and WithinLevelOrdering).
    enum class PoissonCellSelection : std::uint8_t { Bounded = 0, Exhaustive, BestOfCandidates, FeaturePriority };
    enum class PoissonOrdering : std::uint8_t { RandomShuffle = 0, SpatiallyBalanced };
    // Named profiles: Fast (Bounded, no retries), Balanced (1 retry on 4 coarse levels),
    // Quality (2 retries), Hapds (Exhaustive).
    enum class PoissonProfile : std::uint8_t { Fast = 0, Balanced, Quality, Hapds };

    // ProgressivePoisson knobs (METHOD-012 / GEOM-112 `Config`, one-to-one).
    struct PoissonSettings
    {
        std::uint32_t Dimension{3u};      // 2 ignores z
        std::uint32_t GridWidth{4u};      // cells per side at level 0
        std::uint32_t MaxLevels{16u};
        float RadiusAlpha{-1.0f};         // r_L = alpha * cell; outside (0, 1) selects sqrt(d) / 2
        bool RandomizeGridOrigin{true};
        std::uint32_t GridOriginSeed{1337u};
        bool ShuffleWithinLevels{true};
        std::uint32_t ShuffleSeed{0x51ed270bu};
        PoissonCellSelection Selection{PoissonCellSelection::Exhaustive};
        std::uint32_t MaxCellRetries{1u};
        std::uint32_t RepairCoarseLevels{4u};
        std::uint32_t ExhaustiveCoarseLevels{0u};
        std::uint32_t CandidateBudget{4u};
        bool RandomizePhaseOrder{false};
        std::uint32_t PhaseOrderSeed{0x2f1a9c53u};
        bool PriorityTwoBands{false};
        float PriorityBandThreshold{0.0f};
        PoissonOrdering Ordering{PoissonOrdering::RandomShuffle};
        bool ComputeSplatRadii{true};
    };
    [[nodiscard]] PoissonSettings WithProfile(PoissonSettings base, PoissonProfile profile) noexcept;

    struct Params
    {
        Method Method{Method::FarthestPoint};
        // Random: permutation seed.
        std::uint64_t Seed{0u};
        // FarthestPoint: the first sample, and optional importance weights (empty: all 1).
        std::uint32_t FirstIndex{0u};
        std::span<const double> Weights{};
        // FarthestPoint: points per Morton leaf block.
        std::uint32_t LeafSize{32u};
        PoissonSettings Poisson{};
        // ProgressivePoisson FeaturePriority: one finite score per point (higher preferred).
        std::span<const float> PriorityScores{};
        // CoupledSieve: relaxation eta in (0, 1] and candidates per batch in [1, 32] (eta = 1
        // needs a cap of 1). Uses FirstIndex, Weights and LeafSize like FarthestPoint.
        double Eta{0.95};
        std::uint32_t CandidateCap{32u};
        // FlatGreedy: approximation beta >= 1, MIS priority, batch emission order, caps and seed
        // (FirstIndex is the seed point).
        double Beta{1.1};
        GreedyPriority BatchPriority{GreedyPriority::Random};
        GreedyPriority BatchOrdering{GreedyPriority::Clearance};
        std::uint32_t MaxBatchCandidates{262144u};
        std::uint32_t MaxMisRounds{64u};
        std::uint32_t GainSamples{128u};
        std::uint32_t GreedySeed{0x6d2b79f5u};
        // LazyGreedy: batch priority (Beta, MaxBatchCandidates, GreedySeed and FirstIndex shared).
        LazyPriority LazyBatchPriority{LazyPriority::Random};
        // SampleElimination: weight exponent, weight-limiting beta and gamma, the weight radius
        // (0 derives 2 r_max from the bounding box of the ManifoldDimension largest extents),
        // and the sampled manifold's dimension (2 for surfaces in 3-D, 3 for volumes).
        double EliminationAlpha{8.0};
        double EliminationBeta{0.65};
        double EliminationGamma{1.5};
        bool WeightLimiting{true};
        double EliminationRadius{0.0};
        std::uint32_t ManifoldDimension{3u};
    };

    struct Result
    {
        Status State{Status::Success};
        // Original indices; any prefix is the subsample of that size.
        std::vector<std::uint32_t> Order{};
        // FarthestPoint: the selection key of each sample when it was chosen, i.e. its squared
        // distance to the earlier samples (times its weight when weighted); +inf for the first.
        // A zero means the remaining points duplicate earlier samples.
        std::vector<double> Clearance{};
        // FarthestPoint: point pairs evaluated (a brute-force scan needs about N * count).
        std::uint64_t DistancePairs{0u};
        // ProgressivePoisson: first rank of each level (back() = accepted count, before the
        // count cut), each sample's level spacing and the coarsest radius r_0.
        std::vector<std::uint32_t> LevelOffsets{};
        std::vector<float> SplatRadii{};
        float BaseRadius{0.0f};
        // CoupledSieve / FlatGreedy: first rank of each batch; back() == Order.size(). Batches
        // are computed whole and the order cut at the requested count, so the last batch can be
        // partial and every count yields a prefix of the same order.
        std::vector<std::uint32_t> BatchOffsets{};

        [[nodiscard]] bool Succeeded() const noexcept { return State == Status::Success; }
    };

    // The first min(count, N) samples of the method's order.
    [[nodiscard]] Result Order(PointView points, const Params& params, std::size_t count);
    [[nodiscard]] Result Order(std::span<const glm::vec3> points, const Params& params, std::size_t count);

    // Incremental exact farthest-point order (the hole sieve behind Method::FarthestPoint);
    // Extend continues where the previous call stopped, so prefixes are computed once.
    class FarthestPointSieve
    {
    public:
        [[nodiscard]] Status Build(PointView points, std::uint32_t firstIndex = 0u, std::span<const double> weights = {},
                                   std::uint32_t leafSize = 32u);
        // Emits samples until min(count, N) exist; returns the number emitted.
        std::size_t Extend(std::size_t count);
        // Eta-relaxed batches (Method::CoupledSieve) until at least min(count, N) exist.
        std::size_t ExtendRelaxed(std::size_t count, double eta, std::uint32_t cap);
        [[nodiscard]] std::span<const std::uint32_t> BatchOffsets() const noexcept { return m_BatchOffsets; }

        [[nodiscard]] std::span<const std::uint32_t> Order() const noexcept { return m_Order; }
        [[nodiscard]] std::span<const double> Clearance() const noexcept { return m_Clearance; }
        [[nodiscard]] std::uint64_t DistancePairs() const noexcept { return m_Pairs; }
        [[nodiscard]] std::size_t Size() const noexcept { return m_X.size(); }

    private:
        void RefreshLeaf(std::size_t node);
        void RefreshNode(std::size_t node);
        void Select(std::size_t position);
        void Update(std::size_t position);

        // Points in Morton order; m_Ids maps a position to its original index.
        std::vector<double> m_X{}, m_Y{}, m_Z{}, m_Weight{};
        std::vector<std::uint32_t> m_Ids{}, m_PositionOf{};
        std::vector<double> m_Clear{}; // squared distance to the nearest sample (+inf before the first)
        std::vector<std::uint8_t> m_Selected{};
        // Heap-ordered tree: root 1, leaves at m_Leaves + leaf. Nodes keep their AABB, the largest
        // clearance of unselected points (pruning, -1 when none), the largest selection key
        // (weight times clearance) and the smallest original index attaining it (-1 when none).
        std::size_t m_LeafSize{32u}, m_Leaves{0u};
        std::vector<double> m_Lo{}, m_Hi{}, m_MaxClear{}, m_MaxKey{};
        std::vector<std::int64_t> m_Winner{};
        std::vector<std::size_t> m_Stack{}, m_Visited{};
        std::vector<std::uint32_t> m_Order{};
        std::vector<double> m_Clearance{};
        std::vector<std::uint32_t> m_BatchOffsets{};
        std::uint32_t m_First{0u};
        bool m_Weighted{false};
        std::uint64_t m_Pairs{0u};
    };
}

// Shared between this module's implementation units (not exported).
namespace Geometry::PointSampling::Detail
{
    [[nodiscard]] Status ValidatePoints(PointView points);
    [[nodiscard]] double SquaredDistance(double ax, double ay, double az, double bx, double by, double bz) noexcept;
    [[nodiscard]] std::uint32_t MixU32(std::uint32_t x) noexcept;
    [[nodiscard]] Result FlatGreedyOrder(PointView points, const Params& params, std::size_t count);
    [[nodiscard]] Result SampleEliminationOrder(PointView points, const Params& params, std::size_t count);
    [[nodiscard]] Result LazyGreedyOrder(PointView points, const Params& params, std::size_t count);
    [[nodiscard]] Result TournamentOrder(PointView points, const Params& params, std::size_t count);
}
