module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Geometry.Parameterization.Diagnostics;

import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;

namespace Geometry::Parameterization
{
    namespace
    {
        [[nodiscard]] bool IsFinite(const glm::vec2 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] double SignedAreaUv(const glm::vec2 a, const glm::vec2 b, const glm::vec2 c) noexcept
        {
            return 0.5 * static_cast<double>(
                (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y));
        }

        [[nodiscard]] double ReciprocalSymmetricDistortion(const double ratio) noexcept
        {
            if (ratio <= 0.0 || !std::isfinite(ratio))
            {
                return std::numeric_limits<double>::infinity();
            }
            return std::max(ratio, 1.0 / ratio);
        }

        struct SingularValues
        {
            double Max{0.0};
            double Min{0.0};
        };

        [[nodiscard]] SingularValues ComputeSingularValues(
            const MeshUtils::TriangleFaceView& tri,
            const glm::vec2 ua,
            const glm::vec2 ub,
            const glm::vec2 uc,
            const double detLocal) noexcept
        {
            const glm::vec3 e1 = tri.P1 - tri.P0;
            const glm::vec3 e2 = tri.P2 - tri.P0;
            const glm::vec3 normal = glm::cross(e1, e2);
            const glm::vec3 sAxis = glm::normalize(e1);
            const glm::vec3 tAxis = glm::normalize(glm::cross(normal, e1));

            const double s1 = static_cast<double>(glm::dot(e1, sAxis));
            const double t1 = static_cast<double>(glm::dot(e1, tAxis));
            const double s2 = static_cast<double>(glm::dot(e2, sAxis));
            const double t2 = static_cast<double>(glm::dot(e2, tAxis));

            const glm::vec2 duv1 = ub - ua;
            const glm::vec2 duv2 = uc - ua;
            const double invDet = 1.0 / detLocal;

            const double du1 = static_cast<double>(duv1.x);
            const double du2 = static_cast<double>(duv2.x);
            const double dv1 = static_cast<double>(duv1.y);
            const double dv2 = static_cast<double>(duv2.y);

            const double j00 = (du1 * t2 - du2 * t1) * invDet;
            const double j01 = (-du1 * s2 + du2 * s1) * invDet;
            const double j10 = (dv1 * t2 - dv2 * t1) * invDet;
            const double j11 = (-dv1 * s2 + dv2 * s1) * invDet;

            const double a = j00 * j00 + j10 * j10;
            const double b = j00 * j01 + j10 * j11;
            const double c = j01 * j01 + j11 * j11;
            const double disc = std::sqrt(std::max(0.0, (a - c) * (a - c) + 4.0 * b * b));
            const double sigma0 = std::sqrt(std::max(0.0, (a + c + disc) * 0.5));
            const double sigma1 = std::sqrt(std::max(0.0, (a + c - disc) * 0.5));

            return SingularValues{std::max(sigma0, sigma1), std::min(sigma0, sigma1)};
        }

        void FinalizeStatus(ParameterizationDiagnostics& diagnostics, const std::span<const glm::vec2> uvs)
        {
            if (diagnostics.VertexStorageCount == 0u || diagnostics.LiveFaceCount == 0u)
            {
                diagnostics.Status = ParameterizationDiagnosticsStatus::EmptyInput;
                return;
            }
            if (uvs.size() < diagnostics.VertexStorageCount)
            {
                diagnostics.Status = ParameterizationDiagnosticsStatus::MissingUvCoordinates;
                return;
            }
            if (diagnostics.EvaluatedFaceCount == 0u)
            {
                diagnostics.Status = ParameterizationDiagnosticsStatus::NoEvaluatedFaces;
                return;
            }
            diagnostics.Status = diagnostics.HasInvalidInput()
                ? ParameterizationDiagnosticsStatus::PartialInvalidInput
                : ParameterizationDiagnosticsStatus::Success;
        }
    } // namespace

    const char* ToString(const ParameterizationDiagnosticsStatus status) noexcept
    {
        switch (status)
        {
            case ParameterizationDiagnosticsStatus::Success:               return "success";
            case ParameterizationDiagnosticsStatus::EmptyInput:            return "empty_input";
            case ParameterizationDiagnosticsStatus::MissingUvCoordinates:  return "missing_uv_coordinates";
            case ParameterizationDiagnosticsStatus::PartialInvalidInput:   return "partial_invalid_input";
            case ParameterizationDiagnosticsStatus::NoEvaluatedFaces:      return "no_evaluated_faces";
        }
        return "unknown";
    }

    ParameterizationDiagnostics EvaluateParameterizationDiagnostics(
        const HalfedgeMesh::Mesh& mesh,
        const std::span<const glm::vec2> uvs,
        const ParameterizationDiagnosticsOptions& options)
    {
        ParameterizationDiagnostics diagnostics{};
        diagnostics.VertexStorageCount = mesh.VerticesSize();
        diagnostics.FaceStorageCount = mesh.FacesSize();
        diagnostics.FaceConformalDistortion.assign(
            diagnostics.FaceStorageCount,
            std::numeric_limits<float>::quiet_NaN());

        if (mesh.VertexCount() == 0u || mesh.FaceCount() == 0u)
        {
            FinalizeStatus(diagnostics, uvs);
            return diagnostics;
        }
        if (uvs.size() < mesh.VerticesSize())
        {
            diagnostics.LiveFaceCount = mesh.FaceCount();
            diagnostics.SkippedFaceCount = mesh.FaceCount();
            FinalizeStatus(diagnostics, uvs);
            return diagnostics;
        }

        double conformalSum = 0.0;
        double conformalErrorSum = 0.0;
        double conformalErrorSquaredSum = 0.0;
        double areaRatioSum = 0.0;
        double areaDistortionSum = 0.0;
        double areaErrorSum = 0.0;
        double dirichletSum = 0.0;
        double dirichletExcessSum = 0.0;
        double stretchSum = 0.0;
        double stretchErrorSum = 0.0;

        diagnostics.MinAreaRatio = std::numeric_limits<double>::max();

        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            const FaceHandle face{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(face))
            {
                ++diagnostics.DeletedFaceCount;
                continue;
            }

            ++diagnostics.LiveFaceCount;
            if (mesh.Valence(face) != 3u)
            {
                ++diagnostics.NonTriangleFaceCount;
                ++diagnostics.SkippedFaceCount;
                continue;
            }

            MeshUtils::TriangleFaceView tri{};
            if (!MeshUtils::TryGetTriangleFaceView(mesh, face, tri))
            {
                ++diagnostics.NonTriangleFaceCount;
                ++diagnostics.SkippedFaceCount;
                continue;
            }

            if (!IsFinite(tri.P0) || !IsFinite(tri.P1) || !IsFinite(tri.P2))
            {
                ++diagnostics.NonFinitePositionFaceCount;
                ++diagnostics.SkippedFaceCount;
                continue;
            }

            const glm::vec2 uv0 = uvs[tri.V0.Index];
            const glm::vec2 uv1 = uvs[tri.V1.Index];
            const glm::vec2 uv2 = uvs[tri.V2.Index];
            if (!IsFinite(uv0) || !IsFinite(uv1) || !IsFinite(uv2))
            {
                ++diagnostics.NonFiniteUvFaceCount;
                ++diagnostics.SkippedFaceCount;
                continue;
            }

            const double area3d = MeshUtils::TriangleArea(tri.P0, tri.P1, tri.P2);
            if (area3d <= options.DegeneratePositionAreaEpsilon)
            {
                ++diagnostics.DegeneratePositionFaceCount;
                ++diagnostics.SkippedFaceCount;
                continue;
            }

            const double uvAreaSigned = SignedAreaUv(uv0, uv1, uv2);
            const double uvArea = std::abs(uvAreaSigned);
            if (uvArea <= options.DegenerateUvAreaEpsilon)
            {
                ++diagnostics.DegenerateUvFaceCount;
                ++diagnostics.SkippedFaceCount;
                continue;
            }
            if (uvAreaSigned < 0.0)
            {
                ++diagnostics.FlippedElementCount;
            }

            const glm::vec3 e1 = tri.P1 - tri.P0;
            const glm::vec3 e2 = tri.P2 - tri.P0;
            const glm::vec3 normal = glm::cross(e1, e2);
            const glm::vec3 sAxis = glm::normalize(e1);
            const glm::vec3 tAxis = glm::normalize(glm::cross(normal, e1));
            const double s1 = static_cast<double>(glm::dot(e1, sAxis));
            const double t1 = static_cast<double>(glm::dot(e1, tAxis));
            const double s2 = static_cast<double>(glm::dot(e2, sAxis));
            const double t2 = static_cast<double>(glm::dot(e2, tAxis));
            const double detLocal = s1 * t2 - s2 * t1;
            if (std::abs(detLocal)
                <= 2.0 * options.DegeneratePositionAreaEpsilon)
            {
                ++diagnostics.DegeneratePositionFaceCount;
                ++diagnostics.SkippedFaceCount;
                continue;
            }

            const SingularValues sv = ComputeSingularValues(tri, uv0, uv1, uv2, detLocal);
            if (sv.Min <= options.SingularValueEpsilon || sv.Max <= options.SingularValueEpsilon)
            {
                ++diagnostics.DegenerateUvFaceCount;
                ++diagnostics.SkippedFaceCount;
                continue;
            }

            const double conformal = sv.Max / sv.Min;
            diagnostics.FaceConformalDistortion[fi] = static_cast<float>(conformal);
            const double conformalError = std::abs(conformal - 1.0);
            const double areaRatio = uvArea / area3d;
            const double areaDistortion = ReciprocalSymmetricDistortion(areaRatio);
            const double areaError = std::abs(areaDistortion - 1.0);
            const double invMaxSq = 1.0 / (sv.Max * sv.Max);
            const double invMinSq = 1.0 / (sv.Min * sv.Min);
            const double dirichlet = 0.5 * (sv.Max * sv.Max + sv.Min * sv.Min + invMaxSq + invMinSq);
            const double dirichletExcess = std::abs(dirichlet - 2.0);
            const double stretch = sv.Max;
            const double stretchError = std::abs(stretch - 1.0);

            conformalSum += conformal;
            conformalErrorSum += conformalError;
            conformalErrorSquaredSum += conformalError * conformalError;
            diagnostics.MaxConformalDistortion = std::max(diagnostics.MaxConformalDistortion, conformal);
            diagnostics.MaxConformalError = std::max(diagnostics.MaxConformalError, conformalError);

            areaRatioSum += areaRatio;
            diagnostics.MinAreaRatio = std::min(diagnostics.MinAreaRatio, areaRatio);
            diagnostics.MaxAreaRatio = std::max(diagnostics.MaxAreaRatio, areaRatio);
            areaDistortionSum += areaDistortion;
            diagnostics.MaxAreaDistortion = std::max(diagnostics.MaxAreaDistortion, areaDistortion);
            areaErrorSum += areaError;
            diagnostics.MaxAreaError = std::max(diagnostics.MaxAreaError, areaError);

            dirichletSum += dirichlet;
            diagnostics.MaxSymmetricDirichletEnergy = std::max(diagnostics.MaxSymmetricDirichletEnergy, dirichlet);
            dirichletExcessSum += dirichletExcess;
            diagnostics.MaxSymmetricDirichletExcess = std::max(diagnostics.MaxSymmetricDirichletExcess, dirichletExcess);

            stretchSum += stretch;
            diagnostics.MaxStretch = std::max(diagnostics.MaxStretch, stretch);
            stretchErrorSum += stretchError;
            diagnostics.MaxStretchError = std::max(diagnostics.MaxStretchError, stretchError);

            ++diagnostics.EvaluatedFaceCount;
        }

        if (diagnostics.EvaluatedFaceCount > 0u)
        {
            const double invCount = 1.0 / static_cast<double>(diagnostics.EvaluatedFaceCount);
            diagnostics.MeanConformalDistortion = conformalSum * invCount;
            diagnostics.MeanConformalError = conformalErrorSum * invCount;
            diagnostics.RootMeanSquareConformalError =
                std::sqrt(conformalErrorSquaredSum * invCount);
            diagnostics.MeanAreaRatio = areaRatioSum * invCount;
            diagnostics.MeanAreaDistortion = areaDistortionSum * invCount;
            diagnostics.MeanAreaError = areaErrorSum * invCount;
            diagnostics.MeanSymmetricDirichletEnergy = dirichletSum * invCount;
            diagnostics.MeanSymmetricDirichletExcess = dirichletExcessSum * invCount;
            diagnostics.MeanStretch = stretchSum * invCount;
            diagnostics.MeanStretchError = stretchErrorSum * invCount;
        }
        else
        {
            diagnostics.MinAreaRatio = 0.0;
        }

        double boundaryRatioSum = 0.0;
        double boundaryDistortionSum = 0.0;
        diagnostics.MinBoundaryLengthRatio = std::numeric_limits<double>::max();

        const auto loops = MeshUtils::CollectBoundaryLoops(mesh);
        diagnostics.BoundaryLoopCount = loops.size();
        for (const MeshUtils::BoundaryLoopData& loop : loops)
        {
            for (const HalfedgeHandle halfedge : loop.Halfedges)
            {
                if (!mesh.IsValid(halfedge) || mesh.IsDeleted(mesh.Edge(halfedge)))
                {
                    ++diagnostics.SkippedBoundaryEdgeCount;
                    continue;
                }

                const VertexHandle v0 = mesh.FromVertex(halfedge);
                const VertexHandle v1 = mesh.ToVertex(halfedge);
                if (!mesh.IsValid(v0) || !mesh.IsValid(v1) || mesh.IsDeleted(v0) || mesh.IsDeleted(v1))
                {
                    ++diagnostics.SkippedBoundaryEdgeCount;
                    continue;
                }
                if (!IsFinite(mesh.Position(v0)) || !IsFinite(mesh.Position(v1)) || !IsFinite(uvs[v0.Index]) || !IsFinite(uvs[v1.Index]))
                {
                    ++diagnostics.SkippedBoundaryEdgeCount;
                    continue;
                }

                const double length3d = static_cast<double>(glm::distance(mesh.Position(v0), mesh.Position(v1)));
                const double lengthUv = static_cast<double>(glm::distance(uvs[v0.Index], uvs[v1.Index]));
                if (length3d <= options.BoundaryPositionLengthEpsilon
                    || lengthUv <= options.BoundaryUvLengthEpsilon)
                {
                    ++diagnostics.SkippedBoundaryEdgeCount;
                    continue;
                }

                const double ratio = lengthUv / length3d;
                const double distortion = ReciprocalSymmetricDistortion(ratio);
                boundaryRatioSum += ratio;
                boundaryDistortionSum += distortion;
                diagnostics.MinBoundaryLengthRatio = std::min(diagnostics.MinBoundaryLengthRatio, ratio);
                diagnostics.MaxBoundaryLengthRatio = std::max(diagnostics.MaxBoundaryLengthRatio, ratio);
                diagnostics.MaxBoundaryLengthDistortion = std::max(diagnostics.MaxBoundaryLengthDistortion, distortion);
                ++diagnostics.BoundaryEdgeCount;
            }
        }

        if (diagnostics.BoundaryEdgeCount > 0u)
        {
            const double invCount = 1.0 / static_cast<double>(diagnostics.BoundaryEdgeCount);
            diagnostics.MeanBoundaryLengthRatio = boundaryRatioSum * invCount;
            diagnostics.MeanBoundaryLengthDistortion = boundaryDistortionSum * invCount;
        }
        else
        {
            diagnostics.MinBoundaryLengthRatio = 0.0;
        }

        FinalizeStatus(diagnostics, uvs);
        return diagnostics;
    }
} // namespace Geometry::Parameterization
