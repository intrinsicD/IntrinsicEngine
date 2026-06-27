module;

#include <algorithm>
#include <cmath>
#include <optional>

module Geometry.Robust;

namespace Geometry::Robust
{
    namespace
    {
        // 95%-efficiency tuning constants (relative to a unit-Gaussian residual).
        constexpr double kHuberC = 1.345;
        constexpr double kTukeyC = 4.685;
        constexpr double kWelschC = 2.985;
        constexpr double kCauchyC = 2.385;

        // Regularization for the u->0 weight limit of kernels whose weight would
        // otherwise diverge (L1). Keeps Weight finite and fail-closed.
        constexpr double kSmallU = 1e-12;

        [[nodiscard]] bool ValidInputs(double residual, double scale) noexcept
        {
            return std::isfinite(residual) && std::isfinite(scale) && scale > 0.0;
        }

        // Final validation: the contract forbids NaN/Inf, so a result that
        // overflowed (e.g. u*u for a huge u from a tiny scale) fails closed.
        [[nodiscard]] std::optional<double> Finite(double value) noexcept
        {
            if (!std::isfinite(value)) return std::nullopt;
            return value;
        }
    }

    std::optional<double> Rho(RobustKernel kernel, double residual, double scale) noexcept
    {
        if (!ValidInputs(residual, scale)) return std::nullopt;
        const double u = residual / scale;
        if (!std::isfinite(u)) return std::nullopt; // tiny scale -> overflow
        const double a = std::abs(u);

        double out = 0.0;
        switch (kernel)
        {
        case RobustKernel::L2:
            out = 0.5 * u * u;
            break;
        case RobustKernel::L1:
            out = a;
            break;
        case RobustKernel::Huber:
            out = (a <= kHuberC) ? 0.5 * u * u : kHuberC * (a - 0.5 * kHuberC);
            break;
        case RobustKernel::Tukey:
        {
            const double c2 = kTukeyC * kTukeyC;
            if (a >= kTukeyC)
            {
                out = c2 / 6.0;
            }
            else
            {
                const double t = 1.0 - (u / kTukeyC) * (u / kTukeyC);
                out = (c2 / 6.0) * (1.0 - t * t * t);
            }
            break;
        }
        case RobustKernel::Welsch:
        {
            const double c2 = kWelschC * kWelschC;
            out = 0.5 * c2 * (1.0 - std::exp(-(u * u) / c2));
            break;
        }
        case RobustKernel::Lorentzian:
            out = std::log(1.0 + 0.5 * u * u);
            break;
        case RobustKernel::Cauchy:
        {
            const double c2 = kCauchyC * kCauchyC;
            out = 0.5 * c2 * std::log(1.0 + (u * u) / c2);
            break;
        }
        default:
            return std::nullopt;
        }
        return Finite(out);
    }

    std::optional<double> Psi(RobustKernel kernel, double residual, double scale) noexcept
    {
        if (!ValidInputs(residual, scale)) return std::nullopt;
        const double u = residual / scale;
        if (!std::isfinite(u)) return std::nullopt;
        const double a = std::abs(u);

        double out = 0.0;
        switch (kernel)
        {
        case RobustKernel::L2:
            out = u;
            break;
        case RobustKernel::L1:
            out = (u > 0.0) ? 1.0 : (u < 0.0 ? -1.0 : 0.0);
            break;
        case RobustKernel::Huber:
            out = (a <= kHuberC) ? u : kHuberC * ((u > 0.0) ? 1.0 : -1.0);
            break;
        case RobustKernel::Tukey:
        {
            if (a >= kTukeyC)
            {
                out = 0.0;
            }
            else
            {
                const double t = 1.0 - (u / kTukeyC) * (u / kTukeyC);
                out = u * t * t;
            }
            break;
        }
        case RobustKernel::Welsch:
        {
            const double c2 = kWelschC * kWelschC;
            out = u * std::exp(-(u * u) / c2);
            break;
        }
        case RobustKernel::Lorentzian:
            out = u / (1.0 + 0.5 * u * u);
            break;
        case RobustKernel::Cauchy:
        {
            const double c2 = kCauchyC * kCauchyC;
            out = u / (1.0 + (u * u) / c2);
            break;
        }
        default:
            return std::nullopt;
        }
        return Finite(out);
    }

    std::optional<double> Weight(RobustKernel kernel, double residual, double scale) noexcept
    {
        if (!ValidInputs(residual, scale)) return std::nullopt;
        const double u = residual / scale;
        if (!std::isfinite(u)) return std::nullopt;
        const double a = std::abs(u);

        double out = 0.0;
        switch (kernel)
        {
        case RobustKernel::L2:
            out = 1.0;
            break;
        case RobustKernel::L1:
            out = 1.0 / std::max(a, kSmallU);
            break;
        case RobustKernel::Huber:
            out = (a <= kHuberC) ? 1.0 : kHuberC / a;
            break;
        case RobustKernel::Tukey:
        {
            if (a >= kTukeyC)
            {
                out = 0.0;
            }
            else
            {
                const double t = 1.0 - (u / kTukeyC) * (u / kTukeyC);
                out = t * t;
            }
            break;
        }
        case RobustKernel::Welsch:
        {
            const double c2 = kWelschC * kWelschC;
            out = std::exp(-(u * u) / c2);
            break;
        }
        case RobustKernel::Lorentzian:
            out = 1.0 / (1.0 + 0.5 * u * u);
            break;
        case RobustKernel::Cauchy:
        {
            const double c2 = kCauchyC * kCauchyC;
            out = 1.0 / (1.0 + (u * u) / c2);
            break;
        }
        default:
            return std::nullopt;
        }
        return Finite(out);
    }
}
