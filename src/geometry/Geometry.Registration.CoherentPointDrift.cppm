// Coherent Point Drift point-set registration (Myronenko & Song, TPAMI 2010): the source
// points are the centroids of an isotropic Gaussian mixture with a uniform outlier
// component, fitted to the target points by EM. One shared E-step feeds four M-steps:
// rigid (rotation, optional uniform scale, translation), affine, nonrigid (a
// displacement field regularized by a Gaussian kernel, "motion coherence"), and Bayesian
// (Hirose, TPAMI 2021: a similarity transform of a Gaussian-process deformation, fitted by
// variational Bayes with per-point mixing weights and posterior variances).
//
// The default is the CPU reference backend (METHOD-015): explicit O(N*M) E-step per
// iteration with O(N+M) memory (the responsibility matrix is never stored), deterministic
// and single-threaded. The nonrigid M-step solves a dense M x M system per iteration
// (O(M^3)), so it is limited to kMaxNonrigidSourcePoints source points.
//
// Optimized backends (METHOD-049, Geometry.Registration.CoherentPointDrift.EStep) are
// opt-in through Params::EStep and Params::LowRank: a parallel dense E-step, a truncated
// E-step with a computed per-row error bound, an automatic choice between them, and a
// low-rank (Nystroem eigenpair) nonrigid M-step that lifts the source-size limit to
// kMaxLowRankSourcePoints. Results report the backend and the error bounds.
module;

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.Registration.CoherentPointDrift;

export import Geometry.Registration.CoherentPointDrift.EStep;

export namespace Geometry::CoherentPointDrift
{
    enum class Variant : std::uint8_t
    {
        Rigid = 0, // default
        Affine,
        Nonrigid,
        Bayesian, // BCPD: T(y) = s R (y + v(y)) + t
    };

    enum class Status : std::uint8_t
    {
        Success = 0,
        EmptyInput,
        NonFiniteInput,
        InvalidParameters,
        TooLarge,        // nonrigid source beyond kMaxNonrigidSourcePoints (kMaxLowRankSourcePoints with LowRank)
        SingularSystem,  // affine source covariance or nonrigid system not invertible
        NumericalFailure // all mass on the outlier component, or a non-finite update
    };

    enum class Termination : std::uint8_t
    {
        None = 0,     // not started, still running (step mode) or failed
        Converged,    // relative objective change below Params::Tolerance
        SigmaFloor,   // sigma^2 reached Params::Sigma2Floor (points coincide)
        IterationCap, // Params::MaxIterations reached before convergence
    };

    inline constexpr std::uint32_t kMaxNonrigidSourcePoints = 8192; // full kernel (nonrigid and Bayesian)
    inline constexpr std::uint32_t kMaxLowRankSourcePoints = 1'000'000;
    // Nystrom E-step landmarks: the landmark Gram matrix is eigendecomposed every iteration.
    inline constexpr std::uint32_t kMaxNystromLandmarks = 4096;
    inline constexpr std::string_view kBackendId = "cpu_reference";

    // Units: with NormalizeInputs (default) both point sets are centered on their own
    // means and divided by one shared scale (the larger RMS radius), so Beta, Lambda,
    // InitialSigma2 and Sigma2Floor are dimensionless. Without it they are in world
    // units (squared for the sigma values). Results are always in world units.
    struct Params
    {
        Variant Method{Variant::Rigid};
        // Weight w of the uniform outlier component, in [0, 1).
        double OutlierWeight{0.0};
        std::uint32_t MaxIterations{150u};
        // Stop when |L_k - L_{k-1}| <= Tolerance * |L_k| for the objective L (negative
        // log-likelihood, plus the coherence term for nonrigid). Zero runs to the cap.
        double Tolerance{1.0e-5};
        // Starting sigma^2; zero derives it from the data (mean squared pair distance).
        double InitialSigma2{0.0};
        // Lower bound for sigma^2; reaching it ends the run (Termination::SigmaFloor).
        double Sigma2Floor{1.0e-10};
        bool NormalizeInputs{true};
        // Rigid: estimate a uniform scale (the paper's similarity model).
        bool EstimateScale{true};
        // Rigid: allow det(R) = -1.
        bool AllowReflection{false};
        // Nonrigid: Gaussian kernel width (beta) and coherence weight (lambda).
        double Beta{2.0};
        double Lambda{3.0};
        // E-step backend (Reference keeps the METHOD-015 path) and, for Truncated/Auto, the
        // bound on each row denominator's relative error.
        EStepPolicy EStep{EStepPolicy::Reference};
        double EStepTolerance{1.0e-6};
        // Worker threads for the optimized E-step and kernel setup; 0 uses all cores.
        std::uint32_t Threads{0u};
        // Nystrom E-step: landmarks (half from each set) and the largest accepted sampled
        // relative error; iterations above it run exactly (truncated or dense).
        std::uint32_t NystromLandmarks{256u};
        double NystromErrorLimit{1.0e-3};
        // Nonrigid/Bayesian: 0 solves with the full Gram matrix; k > 0 uses its k leading eigenpairs.
        std::uint32_t LowRank{0u};
        // Bayesian: OutlierWeight is omega, Beta and Lambda the kernel width and deformation
        // prior; Gamma scales the data-derived initial sigma^2, Kappa is the Dirichlet
        // concentration of the mixing weights (infinity keeps them equal), and
        // SubsampleSource > 0 registers that many farthest-point samples of the source and
        // interpolates their deformation to every source point (Gaussian-process mean).
        double Gamma{1.0};
        double Kappa{std::numeric_limits<double>::infinity()};
        std::uint32_t SubsampleSource{0u};
        // Bayesian: register against that many farthest-point samples of the target (0 = all).
        // Subsample both sets together (BCPD++): source samples against the full target leave
        // unmatched target points that bias the fit unless OutlierWeight > 0.
        std::uint32_t SubsampleTarget{0u};
        // Bayesian: also use the deformation's posterior variances in the E-step weights and in
        // the scale and sigma^2 updates (the paper's full variational update). Off by default,
        // like Hirose's reference implementation: with smooth kernels these terms bias the scale
        // downward and can collapse it (see paper.md).
        bool PosteriorVarianceTerms{false};
        // How subsamples (SubsampleSource/Target) and kernel landmarks (LowRank, Nystrom E-step)
        // are chosen (RUNTIME-289); the default is exact farthest point from point 0.
        PointSampling::Params SubsampleSampling{};
        PointSampling::Params LandmarkSampling{};
        // Vulkan E-step: the device evaluator (METHOD-056); empty runs every iteration on the CPU.
        EStep::ExternalEvaluator EStepExternal{};
    };

    struct IterationTrace
    {
        std::uint32_t Iteration{0u};           // 0-based EM iteration
        double Sigma2{0.0};                    // world units^2, after this iteration's M-step
        double NegativeLogLikelihood{0.0};     // data term at the parameters entering this iteration
        double Objective{0.0};                 // NLL plus the nonrigid coherence term
        double MatchedWeight{0.0};             // Np = sum of inlier responsibilities
        glm::dmat4 Transform{1.0};             // rigid/affine map (Bayesian: its similarity) after the update; identity for nonrigid
        EStepPolicy EStep{EStepPolicy::Reference}; // policy that evaluated this iteration's E-step
        double EStepErrorBound{0.0};           // max relative row-denominator error (0: exact)
        double EStepSampledError{0.0};         // Nystrom: sampled relative error (estimate, not a bound)
        std::uint64_t KernelEvaluations{0u};
    };

    // Called once per completed iteration; runs observed and unobserved are identical.
    using IterationObserver = std::function<void(const IterationTrace&)>;

    struct Result
    {
        Status State{Status::InvalidParameters};
        Termination Stop{Termination::None};
        Variant Method{Variant::Rigid};
        std::uint32_t Iterations{0u};
        double Sigma2{0.0};                // world units^2
        double NegativeLogLikelihood{0.0}; // at the last E-step
        double MatchedWeight{0.0};
        // Rigid and Bayesian: Linear = Scale * Rotation (Bayesian: the similarity part, applied
        // after the deformation); affine: the fitted matrix. Nonrigid: identity.
        glm::dmat3 Linear{1.0};
        glm::dmat3 Rotation{1.0};
        double Scale{1.0};
        glm::dvec3 Translation{0.0};
        glm::dmat4 Transform{1.0};         // y -> Linear * y + Translation (rigid/affine; Bayesian: the similarity alone)
        // T(y_m) in world units for every source point, in source order (all variants).
        std::vector<glm::dvec3> TransformedSource{};
        std::vector<double> ObjectiveHistory{};
        std::vector<double> Sigma2History{};
        // Backend that ran every iteration (BackendId of that policy); "cpu_auto" when Auto mixed
        // policies, "cpu_nystrom" when Nystrom approximated some iterations and ran the rest
        // exactly ("cpu_auto" when it approximated none), likewise "gpu_vulkan_fp32_dense" when the
        // device ran some iterations, "cpu_mixed" when an explicit policy fell back (fast Gauss to dense).
        std::string_view Backend{kBackendId};
        std::string_view RequestedBackend{kBackendId}; // BackendId(Params::EStep)
        double EStepErrorBound{0.0};           // max over all iterations
        double EStepSampledError{0.0};         // Nystrom: max sampled error over accepted iterations
        std::uint64_t KernelEvaluations{0u};   // total over all iterations
        // Vulkan: iterations meant for the device that ran on the CPU (no evaluator or it failed).
        std::uint32_t EStepFallbacks{0u};
        // Vulkan: iterations the device evaluated (the rest ran truncated, too narrow for fp32
        // terms, or fell back).
        std::uint32_t EStepDeviceIterations{0u};
        // Low-rank nonrigid: eigenpairs used and the kernel's sampled relative error.
        std::uint32_t KernelRank{0u};
        double KernelApproximationError{0.0};

        [[nodiscard]] bool Succeeded() const noexcept { return State == Status::Success; }
    };

    [[nodiscard]] std::string_view ToString(Variant value) noexcept;
    [[nodiscard]] std::string_view ToString(Status value) noexcept;
    [[nodiscard]] std::string_view ToString(Termination value) noexcept;

    // Incremental form of Register for inspection (init, single steps, run to the end).
    class Solver
    {
    public:
        Solver();
        ~Solver();
        Solver(Solver&&) noexcept;
        Solver& operator=(Solver&&) noexcept;

        // Validates and prepares the problem; the target stays fixed, the source moves.
        [[nodiscard]] Status Initialize(std::span<const glm::vec3> target, std::span<const glm::vec3> source,
                                        const Params& params);
        // One EM iteration; false once the run has ended (converged, floor, cap or failure).
        bool Step(const IterationObserver& observer = {});
        // Steps until the run ends.
        void Run(const IterationObserver& observer = {});
        [[nodiscard]] bool Finished() const noexcept;
        // The current estimate in world units (valid after Initialize succeeded).
        [[nodiscard]] Result Current() const;

    private:
        struct State;
        std::unique_ptr<State> m_State;
    };

    // Registers `source` (moving) onto `target` (fixed).
    [[nodiscard]] Result Register(std::span<const glm::vec3> target, std::span<const glm::vec3> source,
                                  const Params& params, const IterationObserver& observer = {});
}
