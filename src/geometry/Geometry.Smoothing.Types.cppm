// Filter parameters and status shared by geometry algorithms and editor configuration.
module;

#include <cstdint>
#include <cstddef>

export module Geometry.Smoothing.Types;

export namespace Geometry::Smoothing
{
    // Success means the mesh was processed; every other value leaves it unmodified.
    enum class DenoiseStatus : std::uint8_t
    {
        Success,
        EmptyMesh,
        NonManifoldInput,
        DegenerateGeometry,
        NonFiniteInput,
        InvalidParams,
    };
    // VariationalFit is solved by Geometry::HarmonicField::FitProperty, not FilterProperty.
    enum class PropertyFilter : std::uint8_t { Averaging, SpectralHeat, Taubin, Bilateral, Implicit, VariationalFit };
    enum class PropertyLaplacian : std::uint8_t { RandomWalk, Combinatorial, LumpedMass };
    enum class PropertyWeight : std::uint8_t { Uniform, Gaussian, InverseDistance, Cotangent, MeshUniform };
    // Implicit solver: one sparse Cholesky factorization per run, or the preconditioned-CG reference.
    enum class PropertySolver : std::uint8_t { Direct, ConjugateGradient };
    // Variational-fit penalty rho(r) of a residual norm r: r^2; Huber (r^2 up to delta, then
    // 2 delta r - delta^2); or L1 smoothed below delta (r, and r^2/(2 delta) + delta/2 below).
    // L1 on the smoothness term is total variation.
    enum class FitPenalty : std::uint8_t { Quadratic, Huber, L1 };
    // Fixed data weight, or the weight whose mass-weighted RMS residual equals NoiseLevel.
    enum class FitFidelity : std::uint8_t { FixedWeight, NoiseLevel };
    // Optional per-channel bound |u - f| <= radius: one radius for every row, or one per row.
    enum class FitBound : std::uint8_t { None, Uniform, PerRow };

    struct PropertyFilterParams
    {
        PropertyFilter Method{PropertyFilter::Averaging};
        PropertyLaplacian Laplacian{PropertyLaplacian::RandomWalk};
        std::uint32_t Iterations{1};
        double Lambda{0.5};
        double Mu{-0.53};
        double HeatTime{1.0};
        double RangeSigma{1.0};
        double TimeStep{1.0};
        double SolverTolerance{1e-8};
        std::uint32_t MaxSolverIterations{2000};
        PropertySolver Solver{PropertySolver::Direct};
        // Variational fit: minimize sum_edges w rho_s(|u_a - u_b|) + FitWeight sum_rows m rho_d(|u - f|).
        FitPenalty SmoothnessPenalty{FitPenalty::Quadratic};
        FitPenalty DataPenalty{FitPenalty::Quadratic};
        FitFidelity Fidelity{FitFidelity::FixedWeight};
        double FitWeight{1.0};
        double NoiseLevel{0.1};    // property units
        double PenaltyDelta{0.01}; // property units
        FitBound Bound{FitBound::None};
        double BoundRadius{0.1};   // property units
        std::uint32_t MaxFitIterations{1000};
        double FitTolerance{1e-6}; // relative change between reweighting iterations
    };
}
