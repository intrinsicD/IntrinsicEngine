// Coherent Point Drift point-set registration (Myronenko & Song, TPAMI 2010): the source
// points are the centroids of an isotropic Gaussian mixture with a uniform outlier
// component, fitted to the target points by EM. One shared E-step feeds three M-steps:
// rigid (rotation, optional uniform scale, translation), affine, and nonrigid (a
// displacement field regularized by a Gaussian kernel, "motion coherence").
//
// This is the CPU reference backend: explicit O(N*M) E-step per iteration with O(N+M)
// memory (the responsibility matrix is never stored), deterministic and single-threaded.
// The nonrigid M-step solves a dense M x M system per iteration (O(M^3)), so it is
// limited to kMaxNonrigidSourcePoints source points.
module;

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.Registration.CoherentPointDrift;

export namespace Geometry::CoherentPointDrift
{
    enum class Variant : std::uint8_t
    {
        Rigid = 0, // default
        Affine,
        Nonrigid,
    };

    enum class Status : std::uint8_t
    {
        Success = 0,
        EmptyInput,
        NonFiniteInput,
        InvalidParameters,
        TooLarge,        // nonrigid source beyond kMaxNonrigidSourcePoints
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

    inline constexpr std::uint32_t kMaxNonrigidSourcePoints = 8192;
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
    };

    struct IterationTrace
    {
        std::uint32_t Iteration{0u};           // 0-based EM iteration
        double Sigma2{0.0};                    // world units^2, after this iteration's M-step
        double NegativeLogLikelihood{0.0};     // data term at the parameters entering this iteration
        double Objective{0.0};                 // NLL plus the nonrigid coherence term
        double MatchedWeight{0.0};             // Np = sum of inlier responsibilities
        glm::dmat4 Transform{1.0};             // rigid/affine source->target after the update; identity for nonrigid
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
        // Rigid: Linear = Scale * Rotation; affine: the fitted matrix. Nonrigid: identity.
        glm::dmat3 Linear{1.0};
        glm::dmat3 Rotation{1.0};
        double Scale{1.0};
        glm::dvec3 Translation{0.0};
        glm::dmat4 Transform{1.0};         // y -> Linear * y + Translation (rigid/affine)
        // T(y_m) in world units for every source point, in source order (all variants).
        std::vector<glm::dvec3> TransformedSource{};
        std::vector<double> ObjectiveHistory{};
        std::vector<double> Sigma2History{};
        std::string_view Backend{kBackendId};

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
