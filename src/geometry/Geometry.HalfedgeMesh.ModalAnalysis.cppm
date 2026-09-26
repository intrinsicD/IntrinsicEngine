// Modal shape analysis beyond the Laplacian (Hildebrandt, Schulz, von Tycowicz and Polthier,
// "Modal shape analysis beyond Laplacian", CAGD 29(5), 2012): the modified Dirichlet energy
// E_D^N, the rest-state Hessian of the discrete-shells deformation energy (Grinspun et al.
// 2003) and the multi-scale modal signatures and distances built from their eigenpairs.
// Operators are assembled over mesh vertex slots; deleted and isolated vertices keep empty rows.
module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.ModalAnalysis;

import Geometry.HalfedgeMesh;
import Geometry.Sparse;

export namespace Geometry::ModalAnalysis
{
    enum class ModalStatus : std::uint8_t
    {
        Success = 0,
        EmptyMesh,
        NonFinitePositions,
        NonTriangularFace,
        InvalidParameters,
    };

    // ---------------------------------------------------------------------------------------
    // Modified Dirichlet energy E_D^N(u) = ½ uᵀ A u, the Dirichlet energy of the normal
    // variation u·N (paper eq. 12): A_ij = ⟨N_i, N_j⟩ S_ij with S the exact (unclamped) cotan
    // stiffness matrix and N_i the area-weighted unit vertex normal. A = S on planar meshes;
    // otherwise its rows no longer sum to zero and the extra term approximates
    // ½∫u²(κ1² + κ2²) (paper eq. 11). #V × #V, symmetric positive semidefinite.
    // ---------------------------------------------------------------------------------------
    struct ModifiedDirichletResult
    {
        ModalStatus Status{ModalStatus::Success};
        Sparse::SparseMatrix Matrix{};
        std::size_t VertexCount{0};
        std::size_t FallbackNormalCount{0}; // vertices without a nondegenerate incident face
        [[nodiscard]] bool Succeeded() const noexcept { return Status == ModalStatus::Success; }
    };
    [[nodiscard]] ModifiedDirichletResult BuildModifiedDirichletMatrix(const HalfedgeMesh::Mesh& mesh);

    // ---------------------------------------------------------------------------------------
    // Discrete shells (Grinspun et al. 2003) as the general deformation energy of paper eq. 13,
    // E(x) = ½ Σ ω_i(x̄)(f_i(x) − f_i(x̄))², with
    //   flexural  f = θ_e (dihedral angle), ω = Flexural · 3‖ē‖² / Ā_e   (interior edges),
    //   length    f = ‖e‖,                   ω = Length / ‖ē‖             (every edge),
    //   area      f = A_t,                   ω = Area / Ā_t               (every triangle).
    // At the rest state x̄ the Hessian is ∂²E = Σ ω_i ∇f_i ∇f_iᵀ (Lemma 1): only first
    // derivatives are needed. The result is 3#V × 3#V with coordinate c of vertex v at row
    // 3v + c; it is symmetric positive semidefinite with the six rigid motions in its nullspace.
    // ---------------------------------------------------------------------------------------
    struct ThinShellWeights
    {
        double Flexural{1.0};
        double Length{1.0};
        double Area{1.0};
    };
    struct ThinShellHessianResult
    {
        ModalStatus Status{ModalStatus::Success};
        Sparse::SparseMatrix Hessian{};
        std::size_t VertexCount{0};
        std::size_t HingeCount{0};             // interior edges contributing a flexural term
        std::size_t EdgeCount{0};              // edges contributing a length term
        std::size_t TriangleCount{0};          // triangles contributing an area term
        std::size_t SkippedDegenerateCount{0}; // zero-length edges and zero-area triangles/hinges
        [[nodiscard]] bool Succeeded() const noexcept { return Status == ModalStatus::Success; }
    };
    [[nodiscard]] ThinShellHessianResult BuildThinShellHessian(const HalfedgeMesh::Mesh& mesh,
                                                               const ThinShellWeights& weights = {});

    // Elementary terms of the discrete shells, exposed for verification. A hinge is the edge
    // from a to b with apex c of triangle (a, b, c) and apex d of the opposite triangle
    // (b, a, d); θ is the signed bending angle (0 when flat). Gradients are ordered like the
    // arguments; nullopt for degenerate input.
    [[nodiscard]] double DihedralAngle(const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c, const glm::dvec3& d);
    [[nodiscard]] std::optional<std::array<glm::dvec3, 4>> DihedralAngleGradient(
        const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c, const glm::dvec3& d);
    [[nodiscard]] std::optional<std::array<glm::dvec3, 3>> TriangleAreaGradient(
        const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c);

    // Replicates a lumped vertex mass per coordinate: the L² product on vector fields T_x̄X.
    [[nodiscard]] Sparse::DiagonalMatrix ExpandMass(const Sparse::DiagonalMatrix& mass, std::size_t components);

    // ---------------------------------------------------------------------------------------
    // Modal signatures (paper §6). Eigenpairs come from SolveSymmetricGeneralizedEigen, so each
    // mode is L²(M)-normalized. `Rows` is the number of samples; each mode stores
    // Rows·Components values, component c of sample v at v·components + c.
    //   signature  S_t(v) = Σ_j e^{−λ_j t} ‖Φ_j(v)‖²                       (eqs. 27, 30)
    //   distance   δ(v, s) = (∫_{t1}^{t2} ((S_t(v) − S_t(s)) / Σ_k e^{−λ_k t})² d log t)^{½}  (eq. 31)
    // The first `skipModes` eigenpairs are ignored (e.g. the six rigid motions of the
    // thin-shell Hessian, or the constant Laplace mode). With the cotan Laplacian this is the
    // heat kernel signature of Sun et al. (2009).
    // ---------------------------------------------------------------------------------------
    struct ModalSpectrum
    {
        std::span<const double> Eigenvalues{};
        std::span<const double> Eigenvectors{};
        std::size_t Rows{0};
        std::size_t Components{1};
        std::size_t SkipModes{0};
    };
    struct ScaleRange
    {
        double Min{0.0};
        double Max{0.0};
        [[nodiscard]] bool Valid() const noexcept { return Min > 0.0 && Max >= Min; }
    };
    // Sun et al.'s range [4 ln 10 / λ_last, 4 ln 10 / λ_first] over the used modes. Invalid when
    // fewer than one used mode has a positive eigenvalue.
    [[nodiscard]] ScaleRange DefaultScaleRange(const ModalSpectrum& spectrum);
    // t = Min^{1−s} · Max^{s} for s ∈ [0, 1].
    [[nodiscard]] double ScaleAt(const ScaleRange& range, double s);

    struct ModalSignatureResult
    {
        ModalStatus Status{ModalStatus::Success};
        std::vector<double> Values{};
        std::size_t UsedModes{0};
        [[nodiscard]] bool Succeeded() const noexcept { return Status == ModalStatus::Success; }
    };
    [[nodiscard]] ModalSignatureResult ComputeModalSignature(const ModalSpectrum& spectrum, double t);
    // Multi-scale distance of every sample to `source`, integrated with the midpoint rule over
    // `samples` uniform intervals of [log t1, log t2].
    [[nodiscard]] ModalSignatureResult ComputeModalDistance(const ModalSpectrum& spectrum, std::size_t source,
                                                            const ScaleRange& range, std::size_t samples = 32);

    [[nodiscard]] std::string DebugName(ModalStatus status);
}
