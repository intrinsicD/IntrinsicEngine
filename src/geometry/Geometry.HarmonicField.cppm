// Constrained harmonic, biharmonic and triharmonic interpolation and Poisson solves of 1..N-channel
// signals on a nonnegative weighted graph, plus variational (robust, total-variation, bounded) fitting
// and random-walker label propagation built on it.
module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

export module Geometry.HarmonicField;

export import Geometry.Smoothing;
export import Geometry.HarmonicField.Types;

export namespace Geometry::HarmonicField
{
    // Soft constraint: adds Weight * |x_row - target_row|^2 to the energy.
    struct SoftConstraint { std::size_t Row{}; double Weight{}; };

    struct Diagnostics
    {
        std::size_t Rows{}, FreeRows{}, HardRows{}, SoftRows{};
        std::size_t Components{}, UnconstrainedComponents{};
        // Unconstrained components solved under UnconstrainedPolicy::ZeroMean.
        std::size_t GroundedComponents{};
        // max over channels of |A x - b|_inf / max(|b|_inf, 1) on the solved rows.
        double MaxRelativeResidual{};
        // Grounded components only: max over components and channels of |sum b| / sum |b|, the
        // part of the source removed to make the pure-Neumann problem solvable (0 = compatible).
        double MaxCompatibilityDefect{};
    };

    struct Result
    {
        bool Success{};
        std::string Diagnostic{};
        std::vector<double> Values{};
        // Rows of unconstrained components kept under UnconstrainedPolicy::KeepInput; empty if none.
        std::vector<bool> KeptInput{};
        Diagnostics Stats{};
    };

    [[nodiscard]] bool ValidateParams(const Params&) noexcept;

    // Minimizes x^T A x + sum_soft w_i |x_i - t_i|^2 - 2 b^T x subject to the hard rows, i.e.
    // solves (A + W) x = W t + b on every non-hard row. values: row-major rows x channels. Hard
    // rows keep their input values exactly; soft rows pull toward their input values; other input
    // values are ignored unless their component is kept under UnconstrainedPolicy::KeepInput.
    // source: optional right-hand side b with the shape of values (for -Delta u = f on a mesh,
    // b = M f with lumped areas); ignored on hard and kept rows. Masses, when given, need one
    // positive finite value per row; empty means unit mass. Failure never returns partial values.
    [[nodiscard]] Result Solve(std::span<const double> values, std::size_t channels,
                               std::span<const Smoothing::PropertyEdge> edges, const Params& params,
                               std::span<const std::size_t> hardRows,
                               std::span<const SoftConstraint> softRows = {},
                               std::span<const double> mass = {},
                               std::span<const double> source = {});

    struct FitDiagnostics
    {
        // Reweighting or ADMM iterations of the final data weight; triangular solves or
        // harmonic Solves over the whole run.
        std::size_t Iterations{}, Solves{};
        std::size_t Factorizations{};
        // (row, channel) bounds active at the solution.
        std::size_t ActiveBounds{};
        // Data weight used; the chosen one under FitFidelity::NoiseLevel.
        double FitWeight{};
        // sqrt(sum m |u - f|^2 / sum m) over rows that are not fixed.
        double RmsResidual{};
        double Energy{};
        // NoiseLevel only: the target lies outside the residuals reachable in the weight bracket.
        bool NoiseTargetClamped{};
        // ADMM only: final max-norm primal and dual residuals.
        double PrimalResidual{}, DualResidual{};
    };

    struct FitResult
    {
        bool Success{};
        std::string Diagnostic{};
        std::vector<double> Values{};
        FitDiagnostics Stats{};
    };

    // Variational fit of a 1..N-channel signal f (params.Method == VariationalFit):
    //   minimize sum_edges w rho_s(|u_a - u_b|) + lambda sum_rows m rho_d(|u_i - f_i|)
    //   subject to u = f on fixed rows and, if params.Bound is set, |u_ic - f_ic| <= radius_i.
    // Norms are Euclidean over channels. Row masses m follow params.Laplacian: weighted degree
    // (random walk; 1 for isolated rows), 1 (combinatorial) or lumpedMass. With quadratic
    // penalties and no bound this is one implicit diffusion step (M + L / lambda) u = M f.
    // FitSolver::Reweighted minimizes non-quadratic penalties by iteratively reweighted least
    // squares and bounds by a primal-dual active set per channel; every step is a harmonic Solve.
    // FitSolver::Admm splits D u, the data residual and the bound residual off, factors L + kM once
    // and applies closed-form proximal steps; it also accepts delta = 0 (exact L1). The final ADMM
    // iterate is projected onto the fixed rows and bounds. ADMM alone offers Euclidean bounds and
    // the second-order (non-local TGV) smoothness. boundRadii: one nonnegative radius per row,
    // required for FitBound::PerRow. positions: three finite coordinates per row, required for
    // FitOrder::Second. Failure never returns values.
    [[nodiscard]] FitResult FitProperty(std::span<const double> values, std::size_t channels,
                                        std::span<const Smoothing::PropertyEdge> edges,
                                        const Smoothing::PropertyFilterParams& params,
                                        std::span<const std::size_t> fixedRows = {},
                                        std::span<const double> lumpedMass = {},
                                        std::span<const double> boundRadii = {},
                                        std::span<const double> positions = {});

    struct LabelResult
    {
        bool Success{};
        std::string Diagnostic{};
        // Winning label per row; rows kept under KeepInput retain their input label.
        std::vector<std::int32_t> Labels{};
        // Winning probability per row (1 at seeds; 0 for kept unconstrained rows).
        std::vector<double> Confidence{};
        std::vector<std::int32_t> SeedLabels{}; // distinct seed labels, ascending
        // Row-major rows x SeedLabels.size() fields, one per seed label: harmonic rows are
        // probabilities that sum to one (skinning/blending weights); zero for kept rows.
        std::vector<double> Weights{};
        Diagnostics Stats{};
    };

    // Random-walker segmentation: rows whose label differs from `unlabeled` are seeds. One
    // harmonic (or biharmonic) field per seed label shares one factorization; each row takes the
    // label with the largest value, ties resolved to the smallest label. ZeroMean is rejected
    // because a component without seeds has no label information.
    [[nodiscard]] LabelResult PropagateLabels(std::span<const std::int32_t> labels, std::int32_t unlabeled,
                                              std::span<const Smoothing::PropertyEdge> edges,
                                              const Params& params, std::span<const double> mass = {});
}
