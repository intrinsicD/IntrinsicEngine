// Harmonic-field parameters shared by geometry kernels and editor configuration.
module;

#include <cstdint>

export module Geometry.HarmonicField.Types;

export namespace Geometry::HarmonicField
{
    // Order k minimizes x^T L (M^-1 L)^(k-1) x with L = D - W and row masses M: harmonic
    // (Dirichlet energy), biharmonic (Laplacian energy) and triharmonic (curvature-variation
    // energy). Mass only affects orders above one.
    enum class FieldOrder : std::uint8_t { Harmonic = 1, Biharmonic = 2, Triharmonic = 3 };
    // A connected component without any hard or soft constraint has no unique solution: fail,
    // keep its input values, or solve the pure-Neumann problem grounded to zero mass-weighted mean
    // (its source is first projected onto the compatible range).
    enum class UnconstrainedPolicy : std::uint8_t { Fail, KeepInput, ZeroMean };

    struct Params
    {
        FieldOrder Order{FieldOrder::Harmonic};
        UnconstrainedPolicy Unconstrained{UnconstrainedPolicy::Fail};
    };
}
