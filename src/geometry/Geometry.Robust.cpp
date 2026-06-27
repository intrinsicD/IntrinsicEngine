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
    }

    std::optional<double> Rho(RobustKernel kernel, double residual, double scale) noexcept
    {
        if (!ValidInputs(residual, scale)) return std::nullopt;
        const double u = residual / scale;
        const double a = std::abs(u);

        switch (kernel)
        {
        case RobustKernel::L2:
            return 0.5 * u * u;
        case RobustKernel::L1:
            return a;
        case RobustKernel::Huber:
            return (a <= kHuberC) ? 0.5 * u * u : kHuberC * (a - 0.5 * kHuberC);
        case RobustKernel::Tukey:
        {
            const double c2 = kTukeyC * kTukeyC;
            if (a >= kTukeyC) return c2 / 6.0;
            const double t = 1.0 - (u / kTukeyC) * (u / kTukeyC);
            return (c2 / 6.0) * (1.0 - t * t * t);
        }
        case RobustKernel::Welsch:
        {
            const double c2 = kWelschC * kWelschC;
            return 0.5 * c2 * (1.0 - std::exp(-(u * u) / c2));
        }
        case RobustKernel::Lorentzian:
            return std::log(1.0 + 0.5 * u * u);
        case RobustKernel::Cauchy:
        {
            const double c2 = kCauchyC * kCauchyC;
            return 0.5 * c2 * std::log(1.0 + (u * u) / c2);
        }
        }
        return std::nullopt;
    }

    std::optional<double> Psi(RobustKernel kernel, double residual, double scale) noexcept
    {
        if (!ValidInputs(residual, scale)) return std::nullopt;
        const double u = residual / scale;
        const double a = std::abs(u);

        switch (kernel)
        {
        case RobustKernel::L2:
            return u;
        case RobustKernel::L1:
            return (u > 0.0) ? 1.0 : (u < 0.0 ? -1.0 : 0.0);
        case RobustKernel::Huber:
            return (a <= kHuberC) ? u : kHuberC * ((u > 0.0) ? 1.0 : -1.0);
        case RobustKernel::Tukey:
        {
            if (a >= kTukeyC) return 0.0;
            const double t = 1.0 - (u / kTukeyC) * (u / kTukeyC);
            return u * t * t;
        }
        case RobustKernel::Welsch:
        {
            const double c2 = kWelschC * kWelschC;
            return u * std::exp(-(u * u) / c2);
        }
        case RobustKernel::Lorentzian:
            return u / (1.0 + 0.5 * u * u);
        case RobustKernel::Cauchy:
        {
            const double c2 = kCauchyC * kCauchyC;
            return u / (1.0 + (u * u) / c2);
        }
        }
        return std::nullopt;
    }

    std::optional<double> Weight(RobustKernel kernel, double residual, double scale) noexcept
    {
        if (!ValidInputs(residual, scale)) return std::nullopt;
        const double u = residual / scale;
        const double a = std::abs(u);

        switch (kernel)
        {
        case RobustKernel::L2:
            return 1.0;
        case RobustKernel::L1:
            return 1.0 / std::max(a, kSmallU);
        case RobustKernel::Huber:
            return (a <= kHuberC) ? 1.0 : kHuberC / a;
        case RobustKernel::Tukey:
        {
            if (a >= kTukeyC) return 0.0;
            const double t = 1.0 - (u / kTukeyC) * (u / kTukeyC);
            return t * t;
        }
        case RobustKernel::Welsch:
        {
            const double c2 = kWelschC * kWelschC;
            return std::exp(-(u * u) / c2);
        }
        case RobustKernel::Lorentzian:
            return 1.0 / (1.0 + 0.5 * u * u);
        case RobustKernel::Cauchy:
        {
            const double c2 = kCauchyC * kCauchyC;
            return 1.0 / (1.0 + (u * u) / c2);
        }
        }
        return std::nullopt;
    }
}
