module;

#include <optional>

export module Geometry.Robust;

// Geometry.Robust — robust M-estimator loss kernels for IRLS / robust fitting.
//
// Each kernel is expressed on the scaled residual u = r / scale:
//   Rho    — the loss   ρ(u)
//   Psi    — influence   ψ(u) = ρ'(u)
//   Weight — IRLS weight w(u) = ψ(u)/u, with the finite u→0 limit applied.
//
// Numeric/fail-closed contract (GEOM-005 / GEOM-007): a non-finite residual,
// or a non-finite/non-positive scale, returns std::nullopt (never NaN/Inf).
export namespace Geometry::Robust
{
    enum class RobustKernel
    {
        L2,        // ordinary least squares (no downweighting)
        L1,        // absolute residual
        Huber,     // quadratic near 0, linear in the tail
        Tukey,     // bisquare; redescending, zero weight past the cutoff
        Welsch,    // exponential redescending
        Lorentzian,// log(1 + u^2/2)
        Cauchy,    // log(1 + (u/c)^2)
    };

    [[nodiscard]] std::optional<double> Rho(RobustKernel kernel, double residual, double scale) noexcept;
    [[nodiscard]] std::optional<double> Psi(RobustKernel kernel, double residual, double scale) noexcept;
    [[nodiscard]] std::optional<double> Weight(RobustKernel kernel, double residual, double scale) noexcept;
}
