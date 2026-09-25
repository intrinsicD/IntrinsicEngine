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
    enum class PropertyFilter : std::uint8_t { Averaging, SpectralHeat, Taubin, Bilateral, Implicit };
    enum class PropertyLaplacian : std::uint8_t { RandomWalk, Combinatorial, LumpedMass };
    enum class PropertyWeight : std::uint8_t { Uniform, Gaussian, InverseDistance, Cotangent, MeshUniform };
    // Implicit solver: one sparse Cholesky factorization per run, or the preconditioned-CG reference.
    enum class PropertySolver : std::uint8_t { Direct, ConjugateGradient };

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
    };
}
