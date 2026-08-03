module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/matrix.hpp>

module Geometry.Parameterization.Optimize;

import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Linalg;
import Geometry.Sparse;

namespace Geometry::Parameterization
{
    namespace
    {
        constexpr std::size_t kInvalidIndex =
            std::numeric_limits<std::size_t>::max();

        [[nodiscard]] bool IsFinite(const double value) noexcept
        {
            return std::isfinite(value);
        }

        [[nodiscard]] bool IsFinite(const glm::dvec2 value) noexcept
        {
            return IsFinite(value.x) && IsFinite(value.y);
        }

        [[nodiscard]] bool IsFinite(const glm::dvec3 value) noexcept
        {
            return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
        }

        [[nodiscard]] double Element(
            const glm::dmat2& matrix,
            const std::size_t row,
            const std::size_t column) noexcept
        {
            return matrix[column][row];
        }

        void SetElement(
            glm::dmat2& matrix,
            const std::size_t row,
            const std::size_t column,
            const double value) noexcept
        {
            matrix[column][row] = value;
        }

        [[nodiscard]] glm::dmat2 MatrixFromRows(
            const double m00,
            const double m01,
            const double m10,
            const double m11) noexcept
        {
            glm::dmat2 result{0.0};
            SetElement(result, 0u, 0u, m00);
            SetElement(result, 0u, 1u, m01);
            SetElement(result, 1u, 0u, m10);
            SetElement(result, 1u, 1u, m11);
            return result;
        }

        [[nodiscard]] bool IsFinite(const glm::dmat2& matrix) noexcept
        {
            return IsFinite(Element(matrix, 0u, 0u))
                && IsFinite(Element(matrix, 0u, 1u))
                && IsFinite(Element(matrix, 1u, 0u))
                && IsFinite(Element(matrix, 1u, 1u));
        }

        [[nodiscard]] double Determinant(const glm::dmat2& matrix) noexcept
        {
            return Element(matrix, 0u, 0u) * Element(matrix, 1u, 1u)
                - Element(matrix, 0u, 1u) * Element(matrix, 1u, 0u);
        }

        [[nodiscard]] double FrobeniusSquared(const glm::dmat2& matrix) noexcept
        {
            double result = 0.0;
            for (std::size_t row = 0u; row < 2u; ++row)
            {
                for (std::size_t column = 0u; column < 2u; ++column)
                {
                    const double value = Element(matrix, row, column);
                    result += value * value;
                }
            }
            return result;
        }

        [[nodiscard]] glm::dmat2 Inverse(
            const glm::dmat2& matrix,
            const double determinant) noexcept
        {
            const double inverseDeterminant = 1.0 / determinant;
            return MatrixFromRows(
                Element(matrix, 1u, 1u) * inverseDeterminant,
                -Element(matrix, 0u, 1u) * inverseDeterminant,
                -Element(matrix, 1u, 0u) * inverseDeterminant,
                Element(matrix, 0u, 0u) * inverseDeterminant);
        }

        [[nodiscard]] double SignedUvArea(
            const glm::dvec2 a,
            const glm::dvec2 b,
            const glm::dvec2 c) noexcept
        {
            const glm::dvec2 e1 = b - a;
            const glm::dvec2 e2 = c - a;
            return 0.5 * (e1.x * e2.y - e1.y * e2.x);
        }

        [[nodiscard]] bool ValidateReferenceAndUvs(
            const OptimizationReference& reference,
            const std::span<const glm::dvec2> uvs,
            OptimizationStatus& status)
        {
            if (!reference.Succeeded())
            {
                status = reference.Status;
                return false;
            }
            if (uvs.size() != reference.VertexStorageCount)
            {
                status = OptimizationStatus::SizeMismatch;
                return false;
            }
            for (const OptimizationTriangleReference& face : reference.Faces)
            {
                if (!face.Active)
                    continue;
                for (const std::size_t vertex : face.Vertices)
                {
                    if (vertex >= uvs.size() || !IsFinite(uvs[vertex]))
                    {
                        status = OptimizationStatus::NonFiniteInput;
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] glm::dmat2 ComputeJacobian(
            const OptimizationTriangleReference& face,
            const std::span<const glm::dvec2> uvs) noexcept
        {
            glm::dmat2 jacobian{0.0};
            for (std::size_t corner = 0u; corner < 3u; ++corner)
            {
                const glm::dvec2 uv = uvs[face.Vertices[corner]];
                const glm::dvec2 gradient = face.Gradients[corner];
                SetElement(
                    jacobian,
                    0u,
                    0u,
                    Element(jacobian, 0u, 0u) + uv.x * gradient.x);
                SetElement(
                    jacobian,
                    0u,
                    1u,
                    Element(jacobian, 0u, 1u) + uv.x * gradient.y);
                SetElement(
                    jacobian,
                    1u,
                    0u,
                    Element(jacobian, 1u, 0u) + uv.y * gradient.x);
                SetElement(
                    jacobian,
                    1u,
                    1u,
                    Element(jacobian, 1u, 1u) + uv.y * gradient.y);
            }
            return jacobian;
        }

        [[nodiscard]] Linalg::DenseMatrix ToDenseMatrix(
            const glm::dmat2& matrix)
        {
            Linalg::DenseMatrix result{2u, 2u};
            for (std::size_t row = 0u; row < 2u; ++row)
            {
                for (std::size_t column = 0u; column < 2u; ++column)
                    result(row, column) = Element(matrix, row, column);
            }
            return result;
        }

        [[nodiscard]] glm::dmat2 FromDenseMatrix(
            const Linalg::DenseMatrix& matrix)
        {
            return MatrixFromRows(
                matrix(0u, 0u),
                matrix(0u, 1u),
                matrix(1u, 0u),
                matrix(1u, 1u));
        }

        [[nodiscard]] bool IsHardSvdFailure(
            const Linalg::NumericStatus status) noexcept
        {
            switch (status)
            {
            case Linalg::NumericStatus::Success:
            case Linalg::NumericStatus::RankDeficient:
            case Linalg::NumericStatus::IllConditioned:
                return false;
            case Linalg::NumericStatus::InvalidInput:
            case Linalg::NumericStatus::NonFinite:
            case Linalg::NumericStatus::NoConvergence:
                return true;
            }
            return true;
        }

        [[nodiscard]] OptimizationStatus FitOneFace(
            const OptimizationTriangleReference& reference,
            const std::span<const glm::dvec2> uvs,
            const double singularEpsilon,
            FaceLocalFit& fit)
        {
            fit.Active = true;
            fit.Jacobian = ComputeJacobian(reference, uvs);
            fit.Determinant = Determinant(fit.Jacobian);
            if (!IsFinite(fit.Jacobian) || !IsFinite(fit.Determinant))
                return OptimizationStatus::NonFiniteInput;

            const Linalg::SVDResult svd =
                Linalg::ComputeSVD(ToDenseMatrix(fit.Jacobian), singularEpsilon);
            if (IsHardSvdFailure(svd.Diagnostics.Status)
                || svd.U.Rows != 2u || svd.U.Cols != 2u
                || svd.Vt.Rows != 2u || svd.Vt.Cols != 2u
                || svd.SingularValues.size() != 2u)
            {
                return OptimizationStatus::NumericalFailure;
            }

            if (!IsFinite(svd.SingularValues[0u])
                || !IsFinite(svd.SingularValues[1u])
                || svd.SingularValues[0u] <= singularEpsilon
                || svd.SingularValues[1u] <= singularEpsilon
                || std::abs(fit.Determinant) <= singularEpsilon)
            {
                return OptimizationStatus::DegenerateUv;
            }

            glm::dmat2 left = FromDenseMatrix(svd.U);
            const glm::dmat2 vt = FromDenseMatrix(svd.Vt);
            glm::dvec2 signedSingularValues{
                svd.SingularValues[0u],
                svd.SingularValues[1u]};
            const glm::dmat2 uncorrectedRotation = left * vt;
            if (Determinant(uncorrectedRotation) < 0.0)
            {
                left[1u] = -left[1u];
                signedSingularValues.y = -signedSingularValues.y;
            }

            fit.LeftSingularVectors = left;
            fit.SignedSingularValues = signedSingularValues;
            fit.Rotation = left * vt;
            if (!IsFinite(fit.LeftSingularVectors)
                || !IsFinite(fit.Rotation)
                || !IsFinite(fit.SignedSingularValues)
                || std::abs(Determinant(fit.Rotation) - 1.0) > 1.0e-8)
            {
                return OptimizationStatus::NumericalFailure;
            }
            return OptimizationStatus::Success;
        }

        [[nodiscard]] glm::dmat2 SymmetricDirichletWeight(
            const FaceLocalFit& fit,
            const double determinantEpsilon,
            OptimizationStatus& status)
        {
            if (fit.Determinant <= determinantEpsilon
                || fit.SignedSingularValues.x <= determinantEpsilon
                || fit.SignedSingularValues.y <= determinantEpsilon)
            {
                status = OptimizationStatus::NonInjectiveInput;
                return glm::dmat2{0.0};
            }

            const auto weightForSingularValue = [](const double sigma)
            {
                // Stable closed form of
                // (sigma-sigma^-3)/(sigma-1), including its limit 4 at 1.
                const double sigmaSquared = sigma * sigma;
                const double ratio =
                    (sigma + 1.0) * (sigmaSquared + 1.0)
                    / (sigmaSquared * sigma);
                return std::sqrt(ratio);
            };

            const double w0 = weightForSingularValue(
                fit.SignedSingularValues.x);
            const double w1 = weightForSingularValue(
                fit.SignedSingularValues.y);
            if (!IsFinite(w0) || !IsFinite(w1))
            {
                status = OptimizationStatus::NumericalFailure;
                return glm::dmat2{0.0};
            }

            glm::dmat2 diagonal{0.0};
            diagonal[0u][0u] = w0;
            diagonal[1u][1u] = w1;
            const glm::dmat2 weight =
                fit.LeftSingularVectors * diagonal
                * glm::transpose(fit.LeftSingularVectors);
            if (!IsFinite(weight))
            {
                status = OptimizationStatus::NumericalFailure;
                return glm::dmat2{0.0};
            }
            return weight;
        }

        [[nodiscard]] double SmallestPositiveRoot(
            const double a,
            const double b,
            const double c) noexcept
        {
            const double scale = std::max({
                std::abs(a), std::abs(b), std::abs(c), 1.0});
            const double epsilon =
                64.0 * std::numeric_limits<double>::epsilon() * scale;
            if (std::abs(a) <= epsilon)
            {
                if (std::abs(b) <= epsilon)
                    return 0.0;
                const double root = -c / b;
                return root > epsilon && IsFinite(root) ? root : 0.0;
            }

            double discriminant = b * b - 4.0 * a * c;
            const double discriminantTolerance =
                64.0 * std::numeric_limits<double>::epsilon()
                * std::max({b * b, std::abs(4.0 * a * c), 1.0});
            if (discriminant < -discriminantTolerance)
                return 0.0;
            discriminant = std::max(0.0, discriminant);
            const double squareRoot = std::sqrt(discriminant);
            const double q = -0.5 * (b + std::copysign(squareRoot, b));

            std::array<double, 2u> roots{0.0, 0.0};
            roots[0u] = q / a;
            roots[1u] = std::abs(q) > epsilon
                ? c / q
                : -b / (2.0 * a);

            double smallest = 0.0;
            for (const double root : roots)
            {
                if (!IsFinite(root) || root <= epsilon)
                    continue;
                if (smallest == 0.0 || root < smallest)
                    smallest = root;
            }
            return smallest;
        }
    } // namespace

    OptimizationReference PrepareOptimizationReference(
        const HalfedgeMesh::Mesh& mesh,
        const double areaEpsilon)
    {
        OptimizationReference result{};
        result.VertexStorageCount = mesh.VerticesSize();
        result.FaceStorageCount = mesh.FacesSize();
        result.ActiveVertices.assign(result.VertexStorageCount, 0u);
        result.Faces.resize(result.FaceStorageCount);

        if (!IsFinite(areaEpsilon) || areaEpsilon <= 0.0)
        {
            result.Status = OptimizationStatus::DegenerateReference;
            return result;
        }
        if (mesh.VertexCount() == 0u || mesh.FaceCount() == 0u)
        {
            result.Status = OptimizationStatus::EmptyInput;
            return result;
        }

        for (std::size_t faceIndex = 0u;
             faceIndex < result.FaceStorageCount;
             ++faceIndex)
        {
            const FaceHandle face{static_cast<PropertyIndex>(faceIndex)};
            if (mesh.IsDeleted(face))
                continue;
            if (mesh.Valence(face) != 3u)
            {
                result.Status = OptimizationStatus::NonTriangleFace;
                return result;
            }

            MeshUtils::TriangleFaceView triangle{};
            if (!MeshUtils::TryGetTriangleFaceView(mesh, face, triangle))
            {
                result.Status = OptimizationStatus::NonTriangleFace;
                return result;
            }
            const glm::dvec3 p0{triangle.P0};
            const glm::dvec3 p1{triangle.P1};
            const glm::dvec3 p2{triangle.P2};
            if (!IsFinite(p0) || !IsFinite(p1) || !IsFinite(p2))
            {
                result.Status = OptimizationStatus::NonFiniteInput;
                return result;
            }

            const glm::dvec3 edge1 = p1 - p0;
            const glm::dvec3 edge2 = p2 - p0;
            const double edge1Length = glm::length(edge1);
            const glm::dvec3 normal = glm::cross(edge1, edge2);
            const double doubleArea = glm::length(normal);
            if (!IsFinite(edge1Length) || !IsFinite(doubleArea)
                || edge1Length <= 2.0 * areaEpsilon
                || doubleArea <= 2.0 * areaEpsilon)
            {
                result.Status = OptimizationStatus::DegenerateReference;
                return result;
            }

            const glm::dvec3 sourceX = edge1 / edge1Length;
            const glm::dvec3 sourceNormal = normal / doubleArea;
            const glm::dvec3 sourceY = glm::cross(sourceNormal, sourceX);
            const glm::dvec2 local1{edge1Length, 0.0};
            const glm::dvec2 local2{
                glm::dot(edge2, sourceX),
                glm::dot(edge2, sourceY)};
            const double localDeterminant =
                local1.x * local2.y - local1.y * local2.x;
            if (!IsFinite(local2) || !IsFinite(localDeterminant)
                || localDeterminant <= 2.0 * areaEpsilon)
            {
                result.Status = OptimizationStatus::DegenerateReference;
                return result;
            }

            OptimizationTriangleReference& prepared = result.Faces[faceIndex];
            prepared.Active = true;
            prepared.Vertices = {
                triangle.V0.Index,
                triangle.V1.Index,
                triangle.V2.Index};
            prepared.Gradients[1u] = glm::dvec2{
                local2.y / localDeterminant,
                -local2.x / localDeterminant};
            prepared.Gradients[2u] = glm::dvec2{
                -local1.y / localDeterminant,
                local1.x / localDeterminant};
            prepared.Gradients[0u] =
                -prepared.Gradients[1u] - prepared.Gradients[2u];
            prepared.Area = 0.5 * doubleArea;

            for (const std::size_t vertex : prepared.Vertices)
            {
                if (vertex >= result.VertexStorageCount)
                {
                    result.Status = OptimizationStatus::DegenerateReference;
                    return result;
                }
                result.ActiveVertices[vertex] = 1u;
            }
            ++result.ActiveFaceCount;
        }

        if (result.ActiveFaceCount == 0u)
        {
            result.Status = OptimizationStatus::EmptyInput;
            return result;
        }
        result.Status = OptimizationStatus::Success;
        return result;
    }

    LocalFitResult FitLocalModels(
        const OptimizationReference& reference,
        const std::span<const glm::dvec2> uvs,
        const double singularEpsilon)
    {
        LocalFitResult result{};
        result.Faces.resize(reference.FaceStorageCount);
        if (!IsFinite(singularEpsilon) || singularEpsilon <= 0.0)
        {
            result.Status = OptimizationStatus::DegenerateUv;
            return result;
        }
        if (!ValidateReferenceAndUvs(reference, uvs, result.Status))
            return result;

        for (std::size_t faceIndex = 0u;
             faceIndex < reference.Faces.size();
             ++faceIndex)
        {
            if (!reference.Faces[faceIndex].Active)
                continue;
            FaceLocalFit& fit = result.Faces[faceIndex];
            const OptimizationStatus faceStatus = FitOneFace(
                reference.Faces[faceIndex],
                uvs,
                singularEpsilon,
                fit);
            if (faceStatus != OptimizationStatus::Success)
            {
                result.Status = faceStatus;
                return result;
            }
            ++result.ActiveFaceCount;
            if (fit.Determinant < 0.0)
                ++result.ReflectedFaceCount;
        }
        result.Status = OptimizationStatus::Success;
        return result;
    }

    ArapEnergyResult EvaluateArapEnergy(
        const OptimizationReference& reference,
        const std::span<const glm::dvec2> uvs,
        const double singularEpsilon)
    {
        ArapEnergyResult result{};
        result.FaceEnergy.assign(reference.FaceStorageCount, 0.0);
        const LocalFitResult fits =
            FitLocalModels(reference, uvs, singularEpsilon);
        if (!fits.Succeeded())
        {
            result.Status = fits.Status;
            return result;
        }

        double faceEnergySum = 0.0;
        for (std::size_t faceIndex = 0u;
             faceIndex < reference.Faces.size();
             ++faceIndex)
        {
            if (!reference.Faces[faceIndex].Active)
                continue;
            const double faceEnergy = FrobeniusSquared(
                fits.Faces[faceIndex].Jacobian
                - fits.Faces[faceIndex].Rotation);
            const double weighted =
                reference.Faces[faceIndex].Area * faceEnergy;
            if (!IsFinite(faceEnergy) || !IsFinite(weighted))
            {
                result.Status = OptimizationStatus::NumericalFailure;
                return result;
            }
            result.FaceEnergy[faceIndex] = faceEnergy;
            result.TotalEnergy += weighted;
            faceEnergySum += faceEnergy;
            result.MaxFaceEnergy = std::max(result.MaxFaceEnergy, faceEnergy);
            ++result.ActiveFaceCount;
        }
        if (!IsFinite(result.TotalEnergy) || result.ActiveFaceCount == 0u)
        {
            result.Status = OptimizationStatus::NumericalFailure;
            return result;
        }
        result.MeanFaceEnergy =
            faceEnergySum / static_cast<double>(result.ActiveFaceCount);
        result.Status = OptimizationStatus::Success;
        return result;
    }

    SymmetricDirichletResult EvaluateSymmetricDirichlet(
        const OptimizationReference& reference,
        const std::span<const glm::dvec2> uvs,
        const double determinantEpsilon)
    {
        SymmetricDirichletResult result{};
        result.FaceEnergy.assign(reference.FaceStorageCount, 0.0);
        result.Gradient.assign(reference.VertexStorageCount, glm::dvec2{0.0});
        if (!IsFinite(determinantEpsilon) || determinantEpsilon <= 0.0)
        {
            result.Status = OptimizationStatus::NonInjectiveInput;
            return result;
        }
        if (!ValidateReferenceAndUvs(reference, uvs, result.Status))
            return result;

        result.MinimumDeterminant = std::numeric_limits<double>::max();
        result.MinimumSignedUvArea = std::numeric_limits<double>::max();
        double diagnosticSum = 0.0;
        for (std::size_t faceIndex = 0u;
             faceIndex < reference.Faces.size();
             ++faceIndex)
        {
            const OptimizationTriangleReference& face =
                reference.Faces[faceIndex];
            if (!face.Active)
                continue;

            const glm::dmat2 jacobian = ComputeJacobian(face, uvs);
            const double determinant = Determinant(jacobian);
            const double signedArea = SignedUvArea(
                uvs[face.Vertices[0u]],
                uvs[face.Vertices[1u]],
                uvs[face.Vertices[2u]]);
            if (!IsFinite(jacobian) || !IsFinite(determinant)
                || !IsFinite(signedArea))
            {
                result.Status = OptimizationStatus::NonFiniteInput;
                result.BarrierFace = faceIndex;
                return result;
            }
            result.MinimumDeterminant =
                std::min(result.MinimumDeterminant, determinant);
            result.MinimumSignedUvArea =
                std::min(result.MinimumSignedUvArea, signedArea);
            if (determinant <= determinantEpsilon
                || signedArea <= determinantEpsilon)
            {
                result.Status = OptimizationStatus::NonInjectiveInput;
                result.BarrierFace = faceIndex;
                return result;
            }

            const glm::dmat2 inverse = Inverse(jacobian, determinant);
            const double faceEnergy =
                FrobeniusSquared(jacobian) + FrobeniusSquared(inverse);
            const double diagnosticEnergy = 0.5 * faceEnergy;
            const double weightedEnergy = face.Area * faceEnergy;
            const glm::dmat2 inverseTranspose = glm::transpose(inverse);
            const glm::dmat2 jacobianGradient =
                2.0 * (jacobian
                    - inverseTranspose * inverse * inverseTranspose);
            if (!IsFinite(faceEnergy) || !IsFinite(diagnosticEnergy)
                || !IsFinite(weightedEnergy)
                || !IsFinite(jacobianGradient))
            {
                result.Status = OptimizationStatus::NumericalFailure;
                result.BarrierFace = faceIndex;
                return result;
            }

            result.FaceEnergy[faceIndex] = faceEnergy;
            result.TotalEnergy += weightedEnergy;
            diagnosticSum += diagnosticEnergy;
            result.MaxDiagnosticEnergy =
                std::max(result.MaxDiagnosticEnergy, diagnosticEnergy);
            for (std::size_t corner = 0u; corner < 3u; ++corner)
            {
                result.Gradient[face.Vertices[corner]] +=
                    face.Area * jacobianGradient * face.Gradients[corner];
            }
            ++result.ActiveFaceCount;
        }

        if (result.ActiveFaceCount == 0u || !IsFinite(result.TotalEnergy))
        {
            result.Status = OptimizationStatus::NumericalFailure;
            return result;
        }
        result.MeanDiagnosticEnergy =
            diagnosticSum / static_cast<double>(result.ActiveFaceCount);
        result.Status = OptimizationStatus::Success;
        return result;
    }

    ProxySystem AssembleProxySystem(
        const OptimizationReference& reference,
        const std::span<const glm::dvec2> currentUvs,
        const LocalFitResult& localFits,
        const ProxyEnergy energy,
        const double proximalWeight,
        const double determinantEpsilon)
    {
        ProxySystem result{};
        result.Energy = energy;
        if (!IsFinite(proximalWeight) || proximalWeight < 0.0
            || !IsFinite(determinantEpsilon) || determinantEpsilon <= 0.0)
        {
            return result;
        }
        if (!ValidateReferenceAndUvs(reference, currentUvs, result.Status))
            return result;
        if (!localFits.Succeeded()
            || localFits.Faces.size() != reference.FaceStorageCount)
        {
            result.Status = OptimizationStatus::InvalidProxyInput;
            return result;
        }

        const std::size_t vertexCount = reference.VertexStorageCount;
        result.Dimension = 2u * vertexCount;
        result.RightHandSide.assign(result.Dimension, 0.0);
        Sparse::SparseBuilder builder{result.Dimension, result.Dimension};
        builder.Reserve(reference.ActiveFaceCount * 144u + result.Dimension);

        for (std::size_t faceIndex = 0u;
             faceIndex < reference.Faces.size();
             ++faceIndex)
        {
            const OptimizationTriangleReference& face =
                reference.Faces[faceIndex];
            if (!face.Active)
                continue;
            const FaceLocalFit& fit = localFits.Faces[faceIndex];
            if (!fit.Active)
            {
                result.Status = OptimizationStatus::InvalidProxyInput;
                return result;
            }

            OptimizationStatus weightStatus = OptimizationStatus::Success;
            const glm::dmat2 weight = energy == ProxyEnergy::Arap
                ? glm::dmat2{1.0}
                : SymmetricDirichletWeight(
                    fit, determinantEpsilon, weightStatus);
            if (weightStatus != OptimizationStatus::Success)
            {
                result.Status = weightStatus;
                return result;
            }

            const glm::dmat2 weightedTarget = weight * fit.Rotation;
            const double squareRootArea = std::sqrt(face.Area);
            for (std::size_t residualRow = 0u;
                 residualRow < 2u;
                 ++residualRow)
            {
                for (std::size_t sourceColumn = 0u;
                     sourceColumn < 2u;
                     ++sourceColumn)
                {
                    std::array<std::size_t, 6u> indices{};
                    std::array<double, 6u> coefficients{};
                    std::size_t coefficientCount = 0u;
                    for (std::size_t outputRow = 0u;
                         outputRow < 2u;
                         ++outputRow)
                    {
                        for (std::size_t corner = 0u; corner < 3u; ++corner)
                        {
                            const std::size_t vertex = face.Vertices[corner];
                            indices[coefficientCount] = outputRow == 0u
                                ? vertex
                                : vertexCount + vertex;
                            coefficients[coefficientCount] = squareRootArea
                                * Element(weight, residualRow, outputRow)
                                * face.Gradients[corner][sourceColumn];
                            ++coefficientCount;
                        }
                    }

                    const double target = squareRootArea * Element(
                        weightedTarget, residualRow, sourceColumn);
                    if (!IsFinite(target))
                    {
                        result.Status = OptimizationStatus::NumericalFailure;
                        return result;
                    }
                    for (std::size_t i = 0u; i < coefficientCount; ++i)
                    {
                        const double coefficient = coefficients[i];
                        if (!IsFinite(coefficient))
                        {
                            result.Status = OptimizationStatus::NumericalFailure;
                            return result;
                        }
                        result.RightHandSide[indices[i]] += coefficient * target;
                        for (std::size_t j = 0u; j < coefficientCount; ++j)
                        {
                            builder.Add(
                                indices[i],
                                indices[j],
                                coefficient * coefficients[j]);
                        }
                    }
                }
            }
            ++result.ActiveFaceCount;
        }

        for (std::size_t vertex = 0u; vertex < vertexCount; ++vertex)
        {
            if (reference.ActiveVertices[vertex] == 0u)
            {
                builder.Add(vertex, vertex, 1.0);
                builder.Add(vertexCount + vertex, vertexCount + vertex, 1.0);
                continue;
            }
            if (proximalWeight == 0.0)
                continue;
            builder.Add(vertex, vertex, proximalWeight);
            builder.Add(
                vertexCount + vertex,
                vertexCount + vertex,
                proximalWeight);
            result.RightHandSide[vertex] +=
                proximalWeight * currentUvs[vertex].x;
            result.RightHandSide[vertexCount + vertex] +=
                proximalWeight * currentUvs[vertex].y;
        }

        const Sparse::SparseBuildResult built = builder.Build();
        if (!built.Valid
            || !std::ranges::all_of(
                result.RightHandSide,
                [](const double value) { return IsFinite(value); }))
        {
            result.Status = OptimizationStatus::NumericalFailure;
            return result;
        }
        result.NormalMatrix = built.Matrix;
        result.Status = OptimizationStatus::Success;
        return result;
    }

    InjectiveStepResult MaxInjectiveStep(
        const OptimizationReference& reference,
        const std::span<const glm::dvec2> uvs,
        const std::span<const glm::dvec2> direction,
        const double tMax,
        const double areaEpsilon)
    {
        InjectiveStepResult result{};
        if (!IsFinite(tMax) || tMax <= 0.0
            || !IsFinite(areaEpsilon) || areaEpsilon <= 0.0)
        {
            result.Status = OptimizationStatus::InvalidProxyInput;
            return result;
        }
        if (!ValidateReferenceAndUvs(reference, uvs, result.Status))
            return result;
        if (direction.size() != reference.VertexStorageCount)
        {
            result.Status = OptimizationStatus::SizeMismatch;
            return result;
        }

        result.MaximumStep = tMax;
        for (std::size_t faceIndex = 0u;
             faceIndex < reference.Faces.size();
             ++faceIndex)
        {
            const OptimizationTriangleReference& face =
                reference.Faces[faceIndex];
            if (!face.Active)
                continue;
            for (const std::size_t vertex : face.Vertices)
            {
                if (!IsFinite(direction[vertex]))
                {
                    result.Status = OptimizationStatus::NonFiniteInput;
                    return result;
                }
            }

            const glm::dvec2 e1 =
                uvs[face.Vertices[1u]] - uvs[face.Vertices[0u]];
            const glm::dvec2 e2 =
                uvs[face.Vertices[2u]] - uvs[face.Vertices[0u]];
            const glm::dvec2 d1 =
                direction[face.Vertices[1u]]
                - direction[face.Vertices[0u]];
            const glm::dvec2 d2 =
                direction[face.Vertices[2u]]
                - direction[face.Vertices[0u]];
            const double c = e1.x * e2.y - e1.y * e2.x;
            if (!IsFinite(c) || 0.5 * c <= areaEpsilon)
            {
                result.Status = OptimizationStatus::NonInjectiveInput;
                result.LimitingFace = faceIndex;
                result.MaximumStep = 0.0;
                return result;
            }
            const double b = e1.x * d2.y + d1.x * e2.y
                - e1.y * d2.x - d1.y * e2.x;
            const double a = d1.x * d2.y - d1.y * d2.x;
            if (!IsFinite(a) || !IsFinite(b))
            {
                result.Status = OptimizationStatus::NonFiniteInput;
                return result;
            }

            const double root = SmallestPositiveRoot(a, b, c);
            if (root > 0.0 && root <= result.MaximumStep)
            {
                result.MaximumStep = root;
                result.LimitingFace = faceIndex;
                result.LimitedByTriangle = true;
            }
        }
        result.Status = OptimizationStatus::Success;
        return result;
    }

    InjectiveLineSearchResult FindInjectiveDirichletStep(
        const OptimizationReference& reference,
        const std::span<const glm::dvec2> uvs,
        const std::span<const glm::dvec2> direction,
        const InjectiveLineSearchParams& params)
    {
        InjectiveLineSearchResult result{};
        if (!IsFinite(params.MaximumStep) || params.MaximumStep <= 0.0
            || !IsFinite(params.SafetyFactor)
            || params.SafetyFactor <= 0.0 || params.SafetyFactor >= 1.0
            || !IsFinite(params.DecreaseFactor)
            || params.DecreaseFactor <= 0.0 || params.DecreaseFactor >= 1.0
            || !IsFinite(params.ArmijoCoefficient)
            || params.ArmijoCoefficient <= 0.0
            || params.ArmijoCoefficient >= 1.0
            || !IsFinite(params.MinimumStep) || params.MinimumStep <= 0.0
            || params.MaxIterations == 0u
            || !IsFinite(params.DeterminantEpsilon)
            || params.DeterminantEpsilon <= 0.0)
        {
            result.Status = OptimizationStatus::InvalidProxyInput;
            return result;
        }

        const SymmetricDirichletResult initial =
            EvaluateSymmetricDirichlet(
                reference, uvs, params.DeterminantEpsilon);
        if (!initial.Succeeded())
        {
            result.Status = initial.Status;
            return result;
        }
        if (direction.size() != initial.Gradient.size())
        {
            result.Status = OptimizationStatus::SizeMismatch;
            return result;
        }

        double directionalDerivative = 0.0;
        for (std::size_t vertex = 0u; vertex < direction.size(); ++vertex)
        {
            if (!IsFinite(direction[vertex]))
            {
                result.Status = OptimizationStatus::NonFiniteInput;
                return result;
            }
            directionalDerivative +=
                glm::dot(initial.Gradient[vertex], direction[vertex]);
        }
        result.InitialEnergy = initial.TotalEnergy;
        result.AcceptedEnergy = initial.TotalEnergy;
        result.InitialDirectionalDerivative = directionalDerivative;
        if (!IsFinite(directionalDerivative) || directionalDerivative >= 0.0)
        {
            result.Status = OptimizationStatus::NoDescentDirection;
            return result;
        }

        const InjectiveStepResult boundary = MaxInjectiveStep(
            reference,
            uvs,
            direction,
            params.MaximumStep,
            0.5 * params.DeterminantEpsilon);
        if (!boundary.Succeeded())
        {
            result.Status = boundary.Status;
            return result;
        }
        result.InjectiveBoundary = boundary.MaximumStep;
        result.LimitingFace = boundary.LimitingFace;
        double step = boundary.LimitedByTriangle
            ? std::min(1.0, params.SafetyFactor * boundary.MaximumStep)
            : std::min(1.0, boundary.MaximumStep);
        std::vector<glm::dvec2> candidate(uvs.begin(), uvs.end());

        for (std::size_t iteration = 0u;
             iteration < params.MaxIterations && step >= params.MinimumStep;
             ++iteration)
        {
            for (std::size_t vertex = 0u; vertex < candidate.size(); ++vertex)
                candidate[vertex] = uvs[vertex] + step * direction[vertex];
            const SymmetricDirichletResult evaluated =
                EvaluateSymmetricDirichlet(
                    reference, candidate, params.DeterminantEpsilon);
            result.Iterations = iteration + 1u;
            if (evaluated.Succeeded()
                && evaluated.TotalEnergy
                    <= initial.TotalEnergy
                        + params.ArmijoCoefficient
                            * step * directionalDerivative)
            {
                result.Status = OptimizationStatus::Success;
                result.Step = step;
                result.AcceptedEnergy = evaluated.TotalEnergy;
                return result;
            }
            step *= params.DecreaseFactor;
        }

        result.Status = OptimizationStatus::NoAcceptableStep;
        return result;
    }
} // namespace Geometry::Parameterization
