module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.Parameterization.Optimize;

import Geometry.HalfedgeMesh;
export import Geometry.Sparse;

export namespace Geometry::Parameterization
{
    /// Stable fail-closed outcomes for the stateless optimization kernels.
    /// Callers may consume numerical payloads only when Succeeded() is true;
    /// malformed, singular, reflected, or non-finite inputs never yield a
    /// successful record containing NaN or infinity.
    enum class OptimizationStatus : std::uint8_t
    {
        Success = 0,
        EmptyInput,
        SizeMismatch,
        NonTriangleFace,
        DegenerateReference,
        DegenerateUv,
        NonFiniteInput,
        NonInjectiveInput,
        NumericalFailure,
        InvalidProxyInput,
        NoDescentDirection,
        NoAcceptableStep,
    };

    /// A source-triangle finite-element record. Faces remain aligned to the
    /// mesh's face-storage indices; deleted faces have Active=false. Gradients
    /// are the local 2D barycentric gradients and Area is positive 3D area.
    struct OptimizationTriangleReference
    {
        bool Active{false};
        std::array<std::size_t, 3u> Vertices{};
        std::array<glm::dvec2, 3u> Gradients{};
        double Area{0.0};
    };

    struct OptimizationReference
    {
        OptimizationStatus Status{OptimizationStatus::EmptyInput};
        std::size_t VertexStorageCount{0u};
        std::size_t FaceStorageCount{0u};
        std::size_t ActiveFaceCount{0u};
        std::vector<std::uint8_t> ActiveVertices{};
        std::vector<OptimizationTriangleReference> Faces{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == OptimizationStatus::Success;
        }
    };

    /// Precompute deterministic local source frames and barycentric gradients.
    /// Live non-triangles, non-finite positions, and source triangles whose
    /// area is <= areaEpsilon fail closed. Deleted storage slots are retained.
    /// Every consuming kernel revalidates this public record's cardinalities,
    /// active-domain markers, indices, areas, and gradients before indexing it.
    [[nodiscard]] OptimizationReference PrepareOptimizationReference(
        const HalfedgeMesh::Mesh& mesh,
        double areaEpsilon = 1.0e-12);

    /// Per-face signed-SVD state. Matrices use the conventional map
    /// J=[uv1-uv0,uv2-uv0]*[x1-x0,x2-x0]^-1. Rotation is always in SO(2):
    /// R=U*diag(1,det(U*V^T))*V^T. A reflected J therefore carries a negative
    /// second signed singular value instead of an improper rotation.
    struct FaceLocalFit
    {
        bool Active{false};
        glm::dmat2 Jacobian{0.0};
        glm::dmat2 Rotation{1.0};
        glm::dmat2 LeftSingularVectors{1.0};
        glm::dvec2 SignedSingularValues{0.0};
        double Determinant{0.0};
    };

    struct LocalFitResult
    {
        OptimizationStatus Status{OptimizationStatus::EmptyInput};
        std::size_t ActiveFaceCount{0u};
        std::size_t ReflectedFaceCount{0u};
        std::vector<FaceLocalFit> Faces{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == OptimizationStatus::Success;
        }
    };

    /// Fit the face-storage-aligned signed SVD and closest proper rotation.
    /// Singular/non-finite UV maps return an explicit failure status.
    [[nodiscard]] LocalFitResult FitLocalModels(
        const OptimizationReference& reference,
        std::span<const glm::dvec2> uvs,
        double singularEpsilon = 1.0e-12);

    struct ArapEnergyResult
    {
        OptimizationStatus Status{OptimizationStatus::EmptyInput};
        std::size_t ActiveFaceCount{0u};
        double TotalEnergy{0.0};
        double MeanFaceEnergy{0.0};
        double MaxFaceEnergy{0.0};
        std::vector<double> FaceEnergy{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == OptimizationStatus::Success;
        }
    };

    /// Evaluate sum_f A_f ||J_f-R_f||_F^2 using the proper local rotations.
    [[nodiscard]] ArapEnergyResult EvaluateArapEnergy(
        const OptimizationReference& reference,
        std::span<const glm::dvec2> uvs,
        double singularEpsilon = 1.0e-12);

    struct SymmetricDirichletResult
    {
        OptimizationStatus Status{OptimizationStatus::EmptyInput};
        std::size_t ActiveFaceCount{0u};
        std::size_t BarrierFace{std::numeric_limits<std::size_t>::max()};
        double TotalEnergy{0.0};
        double MeanDiagnosticEnergy{0.0};
        double MaxDiagnosticEnergy{0.0};
        double MinimumDeterminant{0.0};
        double MinimumSignedUvArea{0.0};
        std::vector<double> FaceEnergy{};
        std::vector<glm::dvec2> Gradient{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == OptimizationStatus::Success;
        }
    };

    /// Evaluate the full SLIM objective
    ///   sum_f A_f (||J_f||_F^2 + ||J_f^-1||_F^2)
    /// and its analytic vertex gradient. det(J)<=determinantEpsilon is an
    /// orientation barrier failure. Mean/MaxDiagnosticEnergy contain 0.5 times
    /// the unweighted face value and match GEOM-018 reporting on valid faces.
    [[nodiscard]] SymmetricDirichletResult EvaluateSymmetricDirichlet(
        const OptimizationReference& reference,
        std::span<const glm::dvec2> uvs,
        double determinantEpsilon = 1.0e-12);

    enum class ProxyEnergy : std::uint8_t
    {
        Arap = 0,
        SymmetricDirichlet,
    };

    /// Normal-equation system for blocked coordinates
    /// x=[u_0..u_n-1,v_0..v_n-1]. The proxy builder assembles but does not
    /// factor or solve. Inactive storage vertices receive identity rows.
    struct ProxySystem
    {
        OptimizationStatus Status{OptimizationStatus::InvalidProxyInput};
        ProxyEnergy Energy{ProxyEnergy::Arap};
        std::size_t Dimension{0u};
        std::size_t ActiveFaceCount{0u};
        Sparse::SparseMatrix NormalMatrix{};
        std::vector<double> RightHandSide{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == OptimizationStatus::Success;
        }
    };

    /// Assemble the area-weighted ARAP (W=I) or SLIM gradient-matching proxy.
    /// A finite non-negative proximalWeight adds lambda*I and
    /// lambda*currentUvs to the normal equations; the method owner chooses it.
    /// The public local-fit record is revalidated against currentUvs, including
    /// its Jacobian, determinant, orthogonal factors, and signed SVD identity.
    /// Quantity-relative checks preserve sign distinctions down to the
    /// supported determinant threshold instead of imposing a unit-scale floor.
    [[nodiscard]] ProxySystem AssembleProxySystem(
        const OptimizationReference& reference,
        std::span<const glm::dvec2> currentUvs,
        const LocalFitResult& localFits,
        ProxyEnergy energy,
        double proximalWeight = 0.0,
        double determinantEpsilon = 1.0e-12);

    struct InjectiveStepResult
    {
        OptimizationStatus Status{OptimizationStatus::EmptyInput};
        double MaximumStep{0.0};
        bool LimitedByTriangle{false};
        std::size_t LimitingFace{std::numeric_limits<std::size_t>::max()};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == OptimizationStatus::Success;
        }
    };

    /// Return the first positive root of every triangle's signed-area
    /// quadratic with ratio-preserving scale-safe arithmetic, minimized and
    /// capped by finite positive tMax. The returned root is the degeneracy
    /// boundary; callers must step strictly inside it.
    [[nodiscard]] InjectiveStepResult MaxInjectiveStep(
        const OptimizationReference& reference,
        std::span<const glm::dvec2> uvs,
        std::span<const glm::dvec2> direction,
        double tMax = 1.0,
        double areaEpsilon = 1.0e-12);

    struct InjectiveLineSearchParams
    {
        double MaximumStep{1.0};
        double SafetyFactor{0.8};
        double DecreaseFactor{0.5};
        double ArmijoCoefficient{1.0e-4};
        double MinimumStep{1.0e-12};
        std::size_t MaxIterations{50u};
        double DeterminantEpsilon{1.0e-12};
    };

    struct InjectiveLineSearchResult
    {
        OptimizationStatus Status{OptimizationStatus::EmptyInput};
        double Step{0.0};
        double InjectiveBoundary{0.0};
        double InitialEnergy{0.0};
        double AcceptedEnergy{0.0};
        double InitialDirectionalDerivative{0.0};
        std::size_t Iterations{0u};
        std::size_t LimitingFace{std::numeric_limits<std::size_t>::max()};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == OptimizationStatus::Success;
        }
    };

    /// Orientation-aware bounded Armijo search. It starts at
    /// min(1,0.8*alpha_max) when a triangle limits the step, never evaluates
    /// at the degeneracy root, and accepts only a finite energy decrease.
    /// Positive element orientation establishes local injectivity only; this
    /// helper does not detect boundary overlap or claim global bijectivity.
    [[nodiscard]] InjectiveLineSearchResult FindInjectiveDirichletStep(
        const OptimizationReference& reference,
        std::span<const glm::dvec2> uvs,
        std::span<const glm::dvec2> direction,
        const InjectiveLineSearchParams& params = {});
} // namespace Geometry::Parameterization
