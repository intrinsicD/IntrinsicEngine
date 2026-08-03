module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module Geometry.FixedPoint.Anderson;

export namespace Geometry::FixedPoint
{
    enum class AndersonStepStatus : std::uint8_t
    {
        Plain = 0,
        Accelerated,
        Safeguarded,
        InvalidInput,
        NumericalFailure,
    };

    struct AndersonParams
    {
        std::size_t Window{4u};
        double Damping{1.0};
        double Regularization{1.0e-10};
        double SafeguardFactor{1.0};
    };

    struct AndersonStepResult;

    // Caller-owned history for deterministic, windowed Anderson mixing.
    // The utility stores only fixed-point outputs and residuals; it owns no
    // objective, convergence, or domain-specific validity policy.
    struct AndersonState
    {
        AndersonParams Params{};

    private:
        std::vector<std::vector<double>> MappedHistory{};
        std::vector<std::vector<double>> ResidualHistory{};

        friend AndersonStepResult AndersonStep(
            AndersonState& state,
            std::span<const double> current,
            std::span<const double> mapped);
        friend void ResetAnderson(AndersonState& state) noexcept;
    };

    struct AndersonStepResult
    {
        AndersonStepStatus Status{AndersonStepStatus::InvalidInput};
        std::vector<double> Value{};
        double PlainResidualNorm{0.0};
        double MixedResidualNorm{0.0};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == AndersonStepStatus::Plain ||
                   Status == AndersonStepStatus::Accelerated ||
                   Status == AndersonStepStatus::Safeguarded;
        }

        [[nodiscard]] bool UsedAcceleration() const noexcept
        {
            return Status == AndersonStepStatus::Accelerated;
        }
    };

    // Propose one fixed-point step from x and g(x). The constrained least-
    // squares mix minimizes the residual history with coefficients summing to
    // one. Damping blends the mixed proposal back toward g(x). A proposal whose
    // mixed residual exceeds SafeguardFactor * ||g(x)-x|| is rejected in favor
    // of the plain step. Domain owners must still validate their invariants.
    [[nodiscard]] AndersonStepResult AndersonStep(
        AndersonState& state,
        std::span<const double> current,
        std::span<const double> mapped);

    void ResetAnderson(AndersonState& state) noexcept;
}
