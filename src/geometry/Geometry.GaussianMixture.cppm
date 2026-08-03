module;

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.GaussianMixture;

export import Geometry.FixedPoint.Anderson;

export namespace Geometry::GaussianMixture
{
    enum class FitStatus : std::uint8_t
    {
        Success = 0,
        EmptyInput,
        InvalidComponentCount,
        InvalidParameters,
        NonFiniteInput,
        NumericalFailure,
    };

    enum class AccelerationPolicy : std::uint8_t
    {
        None = 0,
        Anderson,
    };

    // Three-dimensional Gaussian used by the engine's point-set methods.
    // Coordinates and covariance are expressed in world-unit doubles;
    // Covariance must be finite, symmetric, and positive definite.
    struct MultivariateGaussian
    {
        glm::dvec3 Mean{0.0};
        glm::dmat3 Covariance{1.0};
    };

    struct Model
    {
        std::vector<double> Weights{};
        std::vector<MultivariateGaussian> Components{};
    };

    struct FitParams
    {
        std::uint32_t MaxIterations{100u};
        // Stop when likelihood improvement is at most this fraction of
        // (1 + |current likelihood|). Zero requests exact stagnation.
        double RelativeTolerance{1.0e-6};
        // Positive diagonal jitter added to every EM covariance update.
        double CovarianceFloor{1.0e-6};
        std::uint32_t Seed{42u};
        AccelerationPolicy Acceleration{AccelerationPolicy::None};
        FixedPoint::AndersonParams Anderson{};
    };

    struct FitDiagnostics
    {
        FitStatus Status{FitStatus::InvalidParameters};
        std::uint32_t Iterations{0u};
        bool Converged{false};
        double InitialLogLikelihood{0.0};
        double FinalLogLikelihood{0.0};
        std::uint32_t AcceleratedSteps{0u};
        std::uint32_t SafeguardedSteps{0u};
        std::uint32_t RegularizedCovariances{0u};
        // Accepted post-update likelihoods; InitialLogLikelihood is stored
        // separately and this history has one element per completed iteration.
        std::vector<double> LogLikelihoodHistory{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == FitStatus::Success;
        }
    };

    struct FitResult
    {
        Model Mixture{};
        FitDiagnostics Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Diagnostics.Succeeded();
        }
    };

    // Log N(x | mean, covariance). Invalid/non-SPD covariance and non-finite
    // input return nullopt; no NaN/Inf value is published.
    [[nodiscard]] std::optional<double> LogPdf(
        const MultivariateGaussian& gaussian,
        const glm::dvec3& point) noexcept;

    // Deterministic sample for an explicit seed. The same Gaussian/seed pair
    // returns bitwise-identical output within one supported toolchain.
    [[nodiscard]] std::optional<glm::dvec3> Sample(
        const MultivariateGaussian& gaussian,
        std::uint64_t seed) noexcept;

    // Posterior component probabilities for one point. Model weights are
    // normalized internally but must be finite and strictly positive.
    [[nodiscard]] std::optional<std::vector<double>> Responsibilities(
        const Model& mixture,
        const glm::dvec3& point);

    [[nodiscard]] std::optional<double> LogLikelihood(
        const Model& mixture,
        std::span<const glm::vec3> points);

    // Deterministic 3D Gaussian-mixture EM. Initialization uses the shared
    // Geometry.KMeans seeded KMeansPlusPlus policy. Every covariance receives
    // CovarianceFloor * I during the M-step, so coincident samples remain
    // finite. Anderson proposals share the same EM map and are accepted only
    // when model invariants hold and likelihood is no worse than the plain EM
    // candidate. Invalid input returns an explicit status and an empty model.
    [[nodiscard]] FitResult FitEM(
        std::span<const glm::vec3> points,
        std::uint32_t componentCount,
        const FitParams& params = {});
}
