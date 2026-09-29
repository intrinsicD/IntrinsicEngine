// Accelerated Coherent Point Drift building blocks (METHOD-049), validated against the
// METHOD-015 reference in Geometry.Registration.CoherentPointDrift.
//
// E-step: the sufficient statistics P1 = P 1, Pt1 = P^T 1 and PX = P X of the responsibilities
// P(m | x_n) of the Gaussian-mixture-plus-uniform model, with the reference's log-sum-exp
// shift, evaluated in parallel with results that do not depend on the thread count:
//   - Dense:     exact, O(N M) kernel evaluations, parallel.
//   - Truncated: per target row keeps the sources within r_n^2 = d_min,n^2 + 2 sigma^2
//                ln(M / tol). The dropped kernel mass is then at most tol times the kept
//                mass, so every row denominator has relative error <= tol (reported, not
//                assumed). Neighborhoods come from kd-trees; pays off once sigma is small
//                against the point-set extent.
//   - FastGauss: improved fast Gauss transform (Yang et al. 2003) for both passes, with
//                clusters, expansion order and cutoff chosen from the Raykar et al. (2005)
//                bound so every denominator and P1 entry has relative error <= tol; falls
//                back to dense when no plan meets the bound. Pays off for wide kernels.
//   - Auto:      truncated while its radius stays below the extent, otherwise the fast Gauss
//                transform when its planned cost is well below the dense cost, else dense.
//   - Nystrom:   approximate (METHOD-053): K ~= K(Y, Z) K(Z, Z)^+ K(Z, X) on farthest-point
//                landmarks Z of both sets, O((2M + N) L) kernel terms. Its error has no a-priori
//                bound; it is measured on exact sampled rows (Sums::SampledError, an estimate)
//                and an iteration whose estimate exceeds Settings::NystromErrorLimit is redone
//                exactly with the Auto choice between truncated and dense. Skipped (exact) where
//                its planned cost is not below half the dense cost, i.e. for small inputs.
//   - Vulkan:    the dense two-pass form on a device through Settings::External (METHOD-056;
//                fp32 kernel terms, fp64 sums, so not exact). Iterations where Auto would truncate
//                stay on the CPU truncated path; rows whose first-order fp32 error estimate
//                exceeds kExternalErrorLimit are evaluated exactly on the CPU and merged (all of
//                them when they are the majority; the estimate is the reported bound); without
//                an evaluator, or when it fails, the iteration runs with the exact Auto
//                choice between truncated and dense.
// Rows are processed in fixed blocks (their count depends only on N and M) that scatter into
// per-block partial sums reduced in block order, so each kernel term is evaluated once; above
// Settings::PartialBudgetBytes a two-pass form (target pass for the denominators, source pass
// for P1/PX) is used instead. Both are thread-count independent.
//
// Low-rank kernel: k leading eigenpairs of the nonrigid Gram matrix G_ij =
// exp(-|y_i - y_j|^2 / (2 beta^2)) from a Nystroem approximation on farthest-point
// landmarks, with an a-posteriori relative error measured on sampled exact rows.
module;

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

export module Geometry.Registration.CoherentPointDrift.EStep;

export import Geometry.PointSampling;

export namespace Geometry::CoherentPointDrift
{
    enum class EStepPolicy : std::uint8_t
    {
        Reference = 0, // METHOD-015 streamed single-threaded pass (canonical)
        Dense,
        Truncated,
        Auto,
        FastGauss, // improved fast Gauss transform with a computed error bound (wide kernels)
        Nystrom,   // landmark low-rank kernel, sampled error estimate, exact fallback (wide kernels)
        Vulkan,    // dense two-pass form on a device via Settings::External, exact CPU fallback
    };

    [[nodiscard]] std::string_view ToString(EStepPolicy value) noexcept;
    // Backend identity reported in results ("cpu_reference", "cpu_dense_parallel", ...).
    [[nodiscard]] std::string_view BackendId(EStepPolicy value) noexcept;
}

export namespace Geometry::CoherentPointDrift::EStep
{
    // Structure-of-arrays view of 3-D points (normalized coordinates).
    struct PointSet
    {
        std::span<const double> X{}, Y{}, Z{};
        [[nodiscard]] std::size_t Size() const noexcept { return X.size(); }
    };

    // External (device) evaluation of the dense two-pass form, in the frame of shifted source
    // log-weights (every LogWeights entry <= 0, all 0 when unweighted). With
    // a_nm = LogWeights_m - |x_n - y_m|^2 / (2 sigma^2) it writes, per target n,
    //   LogDenominator_n = log(sum_m exp(a_nm) + exp(LogOutlier)),  Pt1_n = sum_m exp(a_nm - LogDenominator_n),
    // and per source m
    //   P1_m = sum_n exp(a_nm - LogDenominator_n),  PX_m = sum_n exp(a_nm - LogDenominator_n) x_n.
    // Returns false when it cannot (no device, refused shape, device failure); the iteration then
    // runs on the CPU. Called on the thread that evaluates.
    struct ExternalRequest
    {
        PointSet Target{}, Moved{};
        std::uint64_t TargetGeneration{0u}; // differs whenever the target differs
        double Sigma2{0.0};
        double LogOutlier{0.0}; // -inf: no uniform component
        std::span<const double> LogWeights{};
        // Nonzero: the row is evaluated on the CPU; the evaluator leaves it out of P1/PX and may
        // write any finite LogDenominator and Pt1 for it (they are overwritten). Empty: none.
        std::span<const std::uint32_t> SkipRows{};
        std::span<double> LogDenominator{}, Pt1{}, P1{}, PXx{}, PXy{}, PXz{};
    };
    using ExternalEvaluator = std::function<bool(const ExternalRequest&)>;
    // Vulkan: rows whose first-order fp32 error estimate (reported as their error bound) exceeds
    // this are evaluated exactly on the CPU (e.g. rows far from every source under a narrow
    // kernel); when they are the majority, the whole iteration runs the CPU dense pass.
    inline constexpr double kExternalErrorLimit = 2.0e-5;

    struct Settings
    {
        EStepPolicy Policy{EStepPolicy::Dense};
        // Truncated: bound on each row denominator's relative error, in (0, 1).
        double Tolerance{1.0e-6};
        std::uint32_t Threads{0u}; // 0: hardware concurrency
        // Memory for per-row-block partial sums (4 doubles per source point and block). When the
        // fixed block count does not fit, a two-pass evaluation (twice the kernel terms, no
        // partial sums) is used instead.
        std::size_t PartialBudgetBytes{std::size_t{128} << 20};
        // Nystrom: landmarks (half from each set) and the largest accepted sampled relative
        // error of the row denominators and P1 entries.
        std::uint32_t NystromLandmarks{256u};
        double NystromErrorLimit{1.0e-3};
        // Nystrom: how the landmarks are chosen from each set (default exact farthest point).
        PointSampling::Params NystromSampling{};
        // Vulkan: the device evaluator (empty: every iteration falls back to the CPU).
        ExternalEvaluator External{};
    };

    struct Sums
    {
        std::vector<double> P1{}, Pt1{}, PXx{}, PXy{}, PXz{};
        double LogDenominatorSum{0.0}; // sum_n log(sum_m exp(-|x_n - y_m|^2 / 2 sigma^2) + c)
        double Matched{0.0};           // sum_n Pt1_n
        EStepPolicy Used{EStepPolicy::Dense};
        // Max relative error bound of any row denominator (truncated: <= tol; the P1 entries then
        // carry the same relative bound for kept pairs plus an absolute error <= N tol / M from
        // dropped ones); fast Gauss adds the a-posteriori relative bound of the P1 entries (<= 2 tol
        // total; PX is bounded only absolutely, by that bound times max |x| P1). 0: exact.
        double ErrorBound{0.0};
        // Nystrom: largest relative error of the sampled exact rows (an estimate, not a bound;
        // 0 when the iteration ran exactly).
        double SampledError{0.0};
        std::uint64_t KernelEvaluations{0u};
        // Vulkan: the external evaluator was wanted (Auto would not truncate) but was absent or failed.
        bool ExternalFallback{false};
        // Vulkan: rows of a device iteration evaluated exactly on the CPU (too narrow for fp32).
        std::uint32_t ExternalCpuRows{0u};
    };

    [[nodiscard]] std::uint32_t ResolveThreads(std::uint32_t requested) noexcept;

    // While alive on a thread, the parallel loops started from it (E-step rows, kernel builds)
    // poll `cancelled` before each chunk and skip the remaining chunks once it returns true, so
    // a long evaluation stops within one chunk. The results are then incomplete: callers must
    // check the predicate afterwards and discard them. Nested scopes restore the outer one.
    class CancellationScope
    {
    public:
        explicit CancellationScope(const std::function<bool()>* cancelled) noexcept;
        ~CancellationScope();
        CancellationScope(const CancellationScope&) = delete;
        CancellationScope& operator=(const CancellationScope&) = delete;
    private:
        const std::function<bool()>* m_Previous;
    };
    // body(begin, end) over [0, count) in fixed chunks of `grain`; each index is handled by exactly
    // one call, so per-index results do not depend on the thread count.
    void ParallelRange(std::size_t count, std::size_t grain, std::uint32_t threads,
                       const std::function<void(std::size_t, std::size_t)>& body);

    class Evaluator
    {
    public:
        Evaluator();
        ~Evaluator();
        Evaluator(Evaluator&&) noexcept;
        Evaluator& operator=(Evaluator&&) noexcept;

        // The target is fixed for the evaluator's lifetime (its spatial index is cached).
        void SetTarget(PointSet target);
        // logOutlier = log c of the uniform component (-inf for no outliers). Optional finite
        // source log-weights w_m multiply each source's kernel term by exp(w_m) (Bayesian CPD's
        // mixing and variance factors); empty means unweighted. Returns false when every row's
        // mass went to the outlier term or a value is not finite.
        [[nodiscard]] bool Evaluate(PointSet moved, double sigma2, double logOutlier, const Settings& settings,
                                    Sums& out, std::span<const double> sourceLogWeights = {});

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    struct LowRankKernel
    {
        std::uint32_t Rank{0u};
        std::uint32_t Landmarks{0u};
        std::vector<double> Basis{};       // points x Rank, column-major, orthonormal columns
        std::vector<double> Eigenvalues{}; // descending, positive
        // sqrt(sum |g - g~|^2 / sum |g|^2) over sampled exact kernel rows.
        double EstimatedRelativeError{0.0};
        // Nystroem extension to any point y: basis row(y) = [k(y, p_l) for l in LandmarkIndices] * Extension
        // (Landmarks x Rank, column-major); for the input points it reproduces Basis.
        std::vector<std::uint32_t> LandmarkIndices{};
        std::vector<double> Extension{};
    };

    // G ~= Basis diag(Eigenvalues) Basis^T for G_ij = exp(-|p_i - p_j|^2 / (2 beta^2)).
    // Returns false for empty input, a non-positive beta or rank, or a numerically empty kernel.
    [[nodiscard]] bool BuildLowRankGaussianKernel(PointSet points, double beta, std::uint32_t rank,
                                                  std::uint32_t threads, LowRankKernel& out,
                                                  const PointSampling::Params& landmarkSampling = {});

    // The first `count` points of `params`' progressive order, cut before the first duplicate
    // of an earlier sample (zero clearance) for the farthest-point family.
    [[nodiscard]] std::vector<std::uint32_t> SamplePoints(PointSet points, std::size_t count,
                                                          const PointSampling::Params& params);
}
