module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

module Geometry.ModalAnalysis;

import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;
import Geometry.DEC;
import Geometry.Sparse;

namespace Geometry::ModalAnalysis
{
    namespace
    {
        bool Finite(const glm::dvec3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
        glm::dvec3 Position(const HalfedgeMesh::Mesh& mesh, VertexHandle v) { return glm::dvec3(mesh.Position(v)); }

        // Positions of every live vertex must be finite; the mesh must have live triangles only.
        ModalStatus ValidateMesh(const HalfedgeMesh::Mesh& mesh)
        {
            if (mesh.VerticesSize() == 0 || mesh.FaceCount() == 0) return ModalStatus::EmptyMesh;
            for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
            {
                const VertexHandle v{static_cast<PropertyIndex>(i)};
                if (!mesh.IsDeleted(v) && !Finite(Position(mesh, v))) return ModalStatus::NonFinitePositions;
            }
            for (std::size_t f = 0; f < mesh.FacesSize(); ++f)
            {
                const FaceHandle face{static_cast<PropertyIndex>(f)};
                MeshUtils::TriangleFaceView view{};
                if (!mesh.IsDeleted(face) && !MeshUtils::TryGetTriangleFaceView(mesh, face, view))
                    return ModalStatus::NonTriangularFace;
            }
            return ModalStatus::Success;
        }

        // Adds ω g gᵀ for a gradient over `vertices` into the 3#V system.
        template<std::size_t N>
        void AddOuterProduct(Sparse::SparseBuilder& builder, const std::array<std::size_t, N>& vertices,
                             const std::array<glm::dvec3, N>& gradient, double weight)
        {
            for (std::size_t i = 0; i < N; ++i)
                for (std::size_t j = 0; j < N; ++j)
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 3; ++c)
                            builder.Add(3 * vertices[i] + std::size_t(r), 3 * vertices[j] + std::size_t(c),
                                        weight * gradient[i][r] * gradient[j][c]);
        }

        bool ValidSpectrum(const ModalSpectrum& s)
        {
            const std::size_t k = s.Eigenvalues.size();
            if (k == 0 || s.Rows == 0 || s.Components == 0 || s.SkipModes >= k ||
                s.Eigenvectors.size() != k * s.Rows * s.Components)
                return false;
            return std::ranges::all_of(s.Eigenvalues, [](double x) { return std::isfinite(x); }) &&
                   std::ranges::all_of(s.Eigenvectors, [](double x) { return std::isfinite(x); });
        }

        // Accumulates S_t over the used modes; returns Σ_k e^{−λ_k t} (the L¹ norm of S_t).
        double Signature(const ModalSpectrum& s, double t, std::vector<double>& out)
        {
            const std::size_t stride = s.Rows * s.Components;
            out.assign(s.Rows, 0.0);
            double total = 0.0;
            for (std::size_t j = s.SkipModes; j < s.Eigenvalues.size(); ++j)
            {
                const double w = std::exp(-s.Eigenvalues[j] * t);
                total += w;
                const double* mode = s.Eigenvectors.data() + j * stride;
                for (std::size_t v = 0; v < s.Rows; ++v)
                {
                    double squared = 0.0;
                    for (std::size_t c = 0; c < s.Components; ++c) squared += mode[v * s.Components + c] * mode[v * s.Components + c];
                    out[v] += w * squared;
                }
            }
            return total;
        }
    }

    ModifiedDirichletResult BuildModifiedDirichletMatrix(const HalfedgeMesh::Mesh& mesh)
    {
        ModifiedDirichletResult result;
        result.VertexCount = mesh.VerticesSize();
        result.Status = ValidateMesh(mesh);
        if (!result.Succeeded()) return result;

        // Area-weighted normals: the unnormalized face normal has length 2·area.
        std::vector<glm::dvec3> normals(mesh.VerticesSize(), glm::dvec3(0.0));
        for (std::size_t f = 0; f < mesh.FacesSize(); ++f)
        {
            MeshUtils::TriangleFaceView tri{};
            if (!MeshUtils::TryGetTriangleFaceView(mesh, FaceHandle{static_cast<PropertyIndex>(f)}, tri)) continue;
            const glm::dvec3 p0(tri.P0), p1(tri.P1), p2(tri.P2);
            const glm::dvec3 n = glm::cross(p1 - p0, p2 - p0);
            for (const auto v : {tri.V0, tri.V1, tri.V2}) normals[v.Index] += n;
        }
        for (std::size_t i = 0; i < normals.size(); ++i)
        {
            const VertexHandle v{static_cast<PropertyIndex>(i)};
            const double length = glm::length(normals[i]);
            if (length > std::numeric_limits<double>::min() && std::isfinite(length)) normals[i] /= length;
            else
            {
                normals[i] = glm::dvec3(0.0);
                if (!mesh.IsDeleted(v) && !mesh.IsIsolated(v)) ++result.FallbackNormalCount;
            }
        }
        // A = S with off-diagonal entries scaled by ⟨N_i, N_j⟩; the diagonal keeps ⟨N_i, N_i⟩ = 1.
        result.Matrix = DEC::BuildLaplacian(mesh);
        for (std::size_t i = 0; i < result.Matrix.Rows; ++i)
            for (auto k = result.Matrix.RowOffsets[i]; k < result.Matrix.RowOffsets[i + 1]; ++k)
                if (const auto j = result.Matrix.ColIndices[k]; j != i)
                    result.Matrix.Values[k] *= glm::dot(normals[i], normals[j]);
        return result;
    }

    double DihedralAngle(const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c, const glm::dvec3& d)
    {
        const glm::dvec3 e = b - a;
        const glm::dvec3 n1 = glm::cross(b - a, c - a), n2 = glm::cross(a - b, d - b);
        const double l1 = glm::length(n1), l2 = glm::length(n2), le = glm::length(e);
        if (!(l1 > 0.0) || !(l2 > 0.0) || !(le > 0.0)) return 0.0;
        return std::atan2(glm::dot(glm::cross(n1 / l1, n2 / l2), e / le), glm::dot(n1 / l1, n2 / l2));
    }

    std::optional<std::array<glm::dvec3, 4>> DihedralAngleGradient(const glm::dvec3& a, const glm::dvec3& b,
                                                                  const glm::dvec3& c, const glm::dvec3& d)
    {
        // Heights h_c = 2A_1/‖e‖ and h_d = 2A_2/‖e‖ over the hinge: ∇_c θ = −n_1/h_c, ∇_d θ = −n_2/h_d;
        // the hinge vertices take the translation- and rotation-invariant split by the
        // projections of c and d onto the edge (Bridson et al. 2003; Wardetzky et al. 2007).
        const glm::dvec3 e = b - a;
        const double e2 = glm::dot(e, e);
        const glm::dvec3 N1 = glm::cross(b - a, c - a), N2 = glm::cross(a - b, d - b);
        const double l1 = glm::dot(N1, N1), l2 = glm::dot(N2, N2);
        if (!(e2 > 0.0) || !(l1 > 0.0) || !(l2 > 0.0) || !std::isfinite(e2 + l1 + l2)) return std::nullopt;
        const double le = std::sqrt(e2);
        // −n/h = −N/‖N‖ · ‖e‖/‖N‖ = −‖e‖ N / ‖N‖².
        const glm::dvec3 gc = -le * N1 / l1, gd = -le * N2 / l2;
        const double ac = glm::dot(c - a, e) / e2, ad = glm::dot(d - a, e) / e2;
        return std::array{-((1.0 - ac) * gc + (1.0 - ad) * gd), -(ac * gc + ad * gd), gc, gd};
    }

    std::optional<std::array<glm::dvec3, 3>> TriangleAreaGradient(const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c)
    {
        const glm::dvec3 N = glm::cross(b - a, c - a);
        const double l = glm::length(N);
        if (!(l > 0.0) || !std::isfinite(l)) return std::nullopt;
        const glm::dvec3 n = N / l;
        return std::array{0.5 * glm::cross(n, c - b), 0.5 * glm::cross(n, a - c), 0.5 * glm::cross(n, b - a)};
    }

    ThinShellHessianResult BuildThinShellHessian(const HalfedgeMesh::Mesh& mesh, const ThinShellWeights& weights)
    {
        ThinShellHessianResult result;
        result.VertexCount = mesh.VerticesSize();
        const auto valid = [](double w) { return std::isfinite(w) && w >= 0.0; };
        if (!valid(weights.Flexural) || !valid(weights.Length) || !valid(weights.Area) ||
            weights.Flexural + weights.Length + weights.Area <= 0.0)
        {
            result.Status = ModalStatus::InvalidParameters;
            return result;
        }
        result.Status = ValidateMesh(mesh);
        if (!result.Succeeded()) return result;

        const std::size_t n = mesh.VerticesSize();
        Sparse::SparseBuilder builder(3 * n, 3 * n);
        builder.Reserve(144 * mesh.EdgeCount() + 36 * mesh.EdgeCount() + 81 * mesh.FaceCount());
        const auto area = [](const glm::dvec3& p, const glm::dvec3& q, const glm::dvec3& r) {
            return 0.5 * glm::length(glm::cross(q - p, r - p));
        };
        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            if (mesh.IsDeleted(EdgeHandle{static_cast<PropertyIndex>(ei)})) continue;
            const HalfedgeHandle h0{static_cast<PropertyIndex>(2 * ei)};
            const HalfedgeHandle h1 = mesh.OppositeHalfedge(h0);
            const VertexHandle va = mesh.FromVertex(h0), vb = mesh.ToVertex(h0);
            const glm::dvec3 a = Position(mesh, va), b = Position(mesh, vb);
            const double length = glm::length(b - a);
            if (!(length > 0.0)) { ++result.SkippedDegenerateCount; continue; }
            if (weights.Length > 0.0)
            {
                const glm::dvec3 u = (b - a) / length;
                AddOuterProduct<2>(builder, {va.Index, vb.Index}, {-u, u}, weights.Length / length);
                ++result.EdgeCount;
            }
            if (weights.Flexural <= 0.0 || mesh.IsBoundary(h0) || mesh.IsBoundary(h1)) continue;
            const VertexHandle vc = mesh.ToVertex(mesh.NextHalfedge(h0)), vd = mesh.ToVertex(mesh.NextHalfedge(h1));
            const glm::dvec3 c = Position(mesh, vc), d = Position(mesh, vd);
            const auto gradient = DihedralAngleGradient(a, b, c, d);
            if (!gradient) { ++result.SkippedDegenerateCount; continue; }
            const double hingeArea = area(a, b, c) + area(b, a, d);
            AddOuterProduct<4>(builder, {va.Index, vb.Index, vc.Index, vd.Index}, *gradient,
                               weights.Flexural * 3.0 * length * length / hingeArea);
            ++result.HingeCount;
        }
        if (weights.Area > 0.0)
            for (std::size_t f = 0; f < mesh.FacesSize(); ++f)
            {
                MeshUtils::TriangleFaceView tri{};
                if (!MeshUtils::TryGetTriangleFaceView(mesh, FaceHandle{static_cast<PropertyIndex>(f)}, tri)) continue;
                const glm::dvec3 p0(tri.P0), p1(tri.P1), p2(tri.P2);
                const auto gradient = TriangleAreaGradient(p0, p1, p2);
                if (!gradient) { ++result.SkippedDegenerateCount; continue; }
                AddOuterProduct<3>(builder, {tri.V0.Index, tri.V1.Index, tri.V2.Index}, *gradient,
                                   weights.Area / area(p0, p1, p2));
                ++result.TriangleCount;
            }
        auto built = builder.Build();
        if (!built.Valid) { result.Status = ModalStatus::NonFinitePositions; return result; }
        result.Hessian = std::move(built.Matrix);
        return result;
    }

    Sparse::DiagonalMatrix ExpandMass(const Sparse::DiagonalMatrix& mass, std::size_t components)
    {
        Sparse::DiagonalMatrix out{mass.Size * components, {}};
        out.Diagonal.reserve(out.Size);
        for (const double m : mass.Diagonal)
            for (std::size_t c = 0; c < components; ++c) out.Diagonal.push_back(m);
        return out;
    }

    ScaleRange DefaultScaleRange(const ModalSpectrum& spectrum)
    {
        double first = std::numeric_limits<double>::infinity(), last = 0.0;
        for (std::size_t j = spectrum.SkipModes; j < spectrum.Eigenvalues.size(); ++j)
            if (const double l = spectrum.Eigenvalues[j]; l > 0.0 && std::isfinite(l))
            {
                first = std::min(first, l);
                last = std::max(last, l);
            }
        if (!(last > 0.0)) return {};
        const double scale = 4.0 * std::numbers::ln10;
        return {scale / last, scale / first};
    }

    double ScaleAt(const ScaleRange& range, double s)
    {
        s = std::clamp(s, 0.0, 1.0);
        return std::exp((1.0 - s) * std::log(range.Min) + s * std::log(range.Max));
    }

    ModalSignatureResult ComputeModalSignature(const ModalSpectrum& spectrum, double t)
    {
        ModalSignatureResult result;
        if (!ValidSpectrum(spectrum) || !(t > 0.0) || !std::isfinite(t))
        {
            result.Status = ModalStatus::InvalidParameters;
            return result;
        }
        (void)Signature(spectrum, t, result.Values);
        result.UsedModes = spectrum.Eigenvalues.size() - spectrum.SkipModes;
        return result;
    }

    ModalSignatureResult ComputeModalDistance(const ModalSpectrum& spectrum, std::size_t source, const ScaleRange& range,
                                              std::size_t samples)
    {
        ModalSignatureResult result;
        if (!ValidSpectrum(spectrum) || source >= spectrum.Rows || !range.Valid() || !(range.Max > range.Min) ||
            !std::isfinite(range.Max) || samples == 0)
        {
            result.Status = ModalStatus::InvalidParameters;
            return result;
        }
        const double lo = std::log(range.Min), step = (std::log(range.Max) - lo) / double(samples);
        std::vector<double> signature;
        result.Values.assign(spectrum.Rows, 0.0);
        for (std::size_t m = 0; m < samples; ++m)
        {
            const double t = std::exp(lo + (double(m) + 0.5) * step);
            const double norm = Signature(spectrum, t, signature);
            if (!(norm > 0.0) || !std::isfinite(norm)) continue;
            for (std::size_t v = 0; v < spectrum.Rows; ++v)
            {
                const double difference = (signature[v] - signature[source]) / norm;
                result.Values[v] += difference * difference * step;
            }
        }
        for (double& value : result.Values) value = std::sqrt(value);
        result.UsedModes = spectrum.Eigenvalues.size() - spectrum.SkipModes;
        return result;
    }

    std::string DebugName(ModalStatus status)
    {
        switch (status)
        {
        case ModalStatus::Success: return "Success";
        case ModalStatus::EmptyMesh: return "EmptyMesh";
        case ModalStatus::NonFinitePositions: return "NonFinitePositions";
        case ModalStatus::NonTriangularFace: return "NonTriangularFace";
        case ModalStatus::InvalidParameters: return "InvalidParameters";
        }
        return "Unknown";
    }
}
