#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>

import Geometry;
import Geometry.Parameterization.Optimize;

#include "Test_MeshBuilders.h"

namespace
{
    namespace Param = Geometry::Parameterization;

    constexpr double kTolerance = 1.0e-9;
    constexpr double kReportingTolerance = 1.0e-7;

    [[nodiscard]] std::vector<glm::dvec2> IdentityTriangleUvs()
    {
        return {
            glm::dvec2{0.0, 0.0},
            glm::dvec2{1.0, 0.0},
            glm::dvec2{0.0, 1.0},
        };
    }

    [[nodiscard]] double MatrixElement(
        const glm::dmat2& matrix,
        const std::size_t row,
        const std::size_t column)
    {
        return matrix[column][row];
    }

    [[nodiscard]] double SparseQuadraticForm(
        const Geometry::Sparse::SparseMatrix& matrix,
        const std::span<const double> values)
    {
        std::vector<double> multiplied(values.size(), 0.0);
        matrix.Multiply(values, multiplied);
        double result = 0.0;
        for (std::size_t i = 0u; i < values.size(); ++i)
            result += values[i] * multiplied[i];
        return result;
    }

    void ExpectSameBits(const double left, const double right)
    {
        EXPECT_EQ(
            std::bit_cast<std::uint64_t>(left),
            std::bit_cast<std::uint64_t>(right));
    }

    void ExpectSameBits(const glm::dvec2 left, const glm::dvec2 right)
    {
        ExpectSameBits(left.x, right.x);
        ExpectSameBits(left.y, right.y);
    }

    void ExpectSymmetricPsd(const Param::ProxySystem& proxy)
    {
        ASSERT_TRUE(proxy.Succeeded());
        const Geometry::Sparse::SparseDiagnostics diagnostics =
            Geometry::Sparse::AnalyzeSparseMatrix(proxy.NormalMatrix, 1.0e-10);
        EXPECT_TRUE(diagnostics.StructurallyValid());
        EXPECT_TRUE(diagnostics.IsSymmetric);
        EXPECT_LE(diagnostics.MaxSymmetryError, 1.0e-10);

        const std::vector<std::vector<double>> probes{
            std::vector<double>(proxy.Dimension, 1.0),
            {1.0, -2.0, 0.5, 3.0, -1.0, 2.0},
            {-0.25, 4.0, -3.0, 0.75, 2.5, -1.5},
        };
        for (const std::vector<double>& probe : probes)
        {
            ASSERT_EQ(probe.size(), proxy.Dimension);
            EXPECT_GE(
                SparseQuadraticForm(proxy.NormalMatrix, probe),
                -1.0e-10);
        }
    }

    [[nodiscard]] std::vector<double> BlockedCoordinates(
        const std::span<const glm::dvec2> uvs)
    {
        std::vector<double> result(2u * uvs.size(), 0.0);
        for (std::size_t vertex = 0u; vertex < uvs.size(); ++vertex)
        {
            result[vertex] = uvs[vertex].x;
            result[uvs.size() + vertex] = uvs[vertex].y;
        }
        return result;
    }
} // namespace

TEST(ParameterizationOptimize, ReferencePreparationIsStorageAlignedAndFailClosed)
{
    auto square = MakeTwoTriangleSquare();
    square.DeleteFace(Geometry::FaceHandle{0u});
    const Param::OptimizationReference prepared =
        Param::PrepareOptimizationReference(square);

    ASSERT_TRUE(prepared.Succeeded());
    EXPECT_EQ(prepared.VertexStorageCount, square.VerticesSize());
    EXPECT_EQ(prepared.FaceStorageCount, square.FacesSize());
    EXPECT_EQ(prepared.ActiveFaceCount, 1u);
    ASSERT_EQ(prepared.Faces.size(), square.FacesSize());
    EXPECT_FALSE(prepared.Faces[0u].Active);
    EXPECT_TRUE(prepared.Faces[1u].Active);
    EXPECT_GT(prepared.Faces[1u].Area, 0.0);

    const Geometry::HalfedgeMesh::Mesh empty;
    EXPECT_EQ(
        Param::PrepareOptimizationReference(empty).Status,
        Param::OptimizationStatus::EmptyInput);
    EXPECT_EQ(
        Param::PrepareOptimizationReference(MakeSingleQuad()).Status,
        Param::OptimizationStatus::NonTriangleFace);

    Geometry::HalfedgeMesh::Mesh degenerate;
    const auto d0 = degenerate.AddVertex({0.0f, 0.0f, 0.0f});
    const auto d1 = degenerate.AddVertex({1.0f, 0.0f, 0.0f});
    const auto d2 = degenerate.AddVertex({2.0f, 0.0f, 0.0f});
    (void)degenerate.AddTriangle(d0, d1, d2);
    EXPECT_EQ(
        Param::PrepareOptimizationReference(degenerate).Status,
        Param::OptimizationStatus::DegenerateReference);

    Geometry::HalfedgeMesh::Mesh nonFinite;
    const auto n0 = nonFinite.AddVertex({0.0f, 0.0f, 0.0f});
    const auto n1 = nonFinite.AddVertex({1.0f, 0.0f, 0.0f});
    const auto n2 = nonFinite.AddVertex({
        0.0f,
        std::numeric_limits<float>::quiet_NaN(),
        0.0f});
    (void)nonFinite.AddTriangle(n0, n1, n2);
    EXPECT_EQ(
        Param::PrepareOptimizationReference(nonFinite).Status,
        Param::OptimizationStatus::NonFiniteInput);
}

TEST(ParameterizationOptimize, ProperRotationFitRecoversRotationAndScale)
{
    const auto mesh = MakeRightTriangle();
    const Param::OptimizationReference reference =
        Param::PrepareOptimizationReference(mesh);
    ASSERT_TRUE(reference.Succeeded());

    constexpr double angle = 0.37;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const std::vector<glm::dvec2> rotated{
        glm::dvec2{0.0, 0.0},
        glm::dvec2{cosine, sine},
        glm::dvec2{-sine, cosine},
    };
    const Param::LocalFitResult rotationFit =
        Param::FitLocalModels(reference, rotated);

    ASSERT_TRUE(rotationFit.Succeeded());
    ASSERT_EQ(rotationFit.ActiveFaceCount, 1u);
    const Param::FaceLocalFit& rotation = rotationFit.Faces[0u];
    EXPECT_NEAR(MatrixElement(rotation.Rotation, 0u, 0u), cosine, kTolerance);
    EXPECT_NEAR(MatrixElement(rotation.Rotation, 0u, 1u), -sine, kTolerance);
    EXPECT_NEAR(MatrixElement(rotation.Rotation, 1u, 0u), sine, kTolerance);
    EXPECT_NEAR(MatrixElement(rotation.Rotation, 1u, 1u), cosine, kTolerance);
    EXPECT_NEAR(rotation.Determinant, 1.0, kTolerance);
    EXPECT_NEAR(rotation.SignedSingularValues.x, 1.0, kTolerance);
    EXPECT_NEAR(rotation.SignedSingularValues.y, 1.0, kTolerance);

    const std::vector<glm::dvec2> scaled{
        glm::dvec2{0.0, 0.0},
        glm::dvec2{2.0, 0.0},
        glm::dvec2{0.0, 2.0},
    };
    const Param::LocalFitResult scaleFit =
        Param::FitLocalModels(reference, scaled);
    ASSERT_TRUE(scaleFit.Succeeded());
    EXPECT_NEAR(MatrixElement(scaleFit.Faces[0u].Rotation, 0u, 0u), 1.0, kTolerance);
    EXPECT_NEAR(MatrixElement(scaleFit.Faces[0u].Rotation, 0u, 1u), 0.0, kTolerance);
    EXPECT_NEAR(MatrixElement(scaleFit.Faces[0u].Rotation, 1u, 0u), 0.0, kTolerance);
    EXPECT_NEAR(MatrixElement(scaleFit.Faces[0u].Rotation, 1u, 1u), 1.0, kTolerance);
    EXPECT_NEAR(scaleFit.Faces[0u].SignedSingularValues.x, 2.0, kTolerance);
    EXPECT_NEAR(scaleFit.Faces[0u].SignedSingularValues.y, 2.0, kTolerance);
}

TEST(ParameterizationOptimize, SymmetricDirichletMatchesReportingDiagnostics)
{
    const auto triangle = MakeRightTriangle();
    const Param::OptimizationReference triangleReference =
        Param::PrepareOptimizationReference(triangle);
    const Param::SymmetricDirichletResult identity =
        Param::EvaluateSymmetricDirichlet(
            triangleReference, IdentityTriangleUvs());
    ASSERT_TRUE(identity.Succeeded());
    EXPECT_NEAR(identity.FaceEnergy[0u], 4.0, kTolerance);
    EXPECT_NEAR(identity.TotalEnergy, 2.0, kTolerance);
    EXPECT_NEAR(identity.MeanDiagnosticEnergy, 2.0, kTolerance);
    EXPECT_NEAR(identity.MaxDiagnosticEnergy, 2.0, kTolerance);

    const auto square = MakeTwoTriangleSquare();
    const Param::OptimizationReference squareReference =
        Param::PrepareOptimizationReference(square);
    const std::vector<glm::dvec2> stretched{
        glm::dvec2{0.0, 0.0},
        glm::dvec2{2.0, 0.0},
        glm::dvec2{2.0, 1.0},
        glm::dvec2{0.0, 1.0},
    };
    const Param::SymmetricDirichletResult optimized =
        Param::EvaluateSymmetricDirichlet(squareReference, stretched);
    ASSERT_TRUE(optimized.Succeeded());
    EXPECT_NEAR(optimized.MeanDiagnosticEnergy, 3.125, kTolerance);
    EXPECT_NEAR(optimized.MaxDiagnosticEnergy, 3.125, kTolerance);

    const std::vector<glm::vec2> reportingUvs{
        glm::vec2{0.0f, 0.0f},
        glm::vec2{2.0f, 0.0f},
        glm::vec2{2.0f, 1.0f},
        glm::vec2{0.0f, 1.0f},
    };
    const Param::ParameterizationDiagnostics diagnostics =
        Param::EvaluateParameterizationDiagnostics(square, reportingUvs);
    ASSERT_EQ(
        diagnostics.Status,
        Param::ParameterizationDiagnosticsStatus::Success);
    EXPECT_NEAR(
        optimized.MeanDiagnosticEnergy,
        diagnostics.MeanSymmetricDirichletEnergy,
        kReportingTolerance);
    EXPECT_NEAR(
        optimized.MaxDiagnosticEnergy,
        diagnostics.MaxSymmetricDirichletEnergy,
        kReportingTolerance);
}

TEST(ParameterizationOptimize, ReflectionUsesProperRotationButTripsBarrier)
{
    const auto mesh = MakeRightTriangle();
    const Param::OptimizationReference reference =
        Param::PrepareOptimizationReference(mesh);
    const std::vector<glm::dvec2> reflected{
        glm::dvec2{0.0, 0.0},
        glm::dvec2{0.0, 1.0},
        glm::dvec2{1.0, 0.0},
    };
    const Param::LocalFitResult fit =
        Param::FitLocalModels(reference, reflected);
    ASSERT_TRUE(fit.Succeeded());
    EXPECT_EQ(fit.ReflectedFaceCount, 1u);
    EXPECT_LT(fit.Faces[0u].SignedSingularValues.y, 0.0);
    EXPECT_NEAR(
        glm::determinant(fit.Faces[0u].Rotation),
        1.0,
        kTolerance);

    const Param::SymmetricDirichletResult optimization =
        Param::EvaluateSymmetricDirichlet(reference, reflected);
    EXPECT_EQ(
        optimization.Status,
        Param::OptimizationStatus::NonInjectiveInput);
    EXPECT_EQ(optimization.BarrierFace, 0u);

    const std::vector<glm::vec2> reportingUvs{
        glm::vec2{0.0f, 0.0f},
        glm::vec2{0.0f, 1.0f},
        glm::vec2{1.0f, 0.0f},
    };
    const Param::ParameterizationDiagnostics diagnostics =
        Param::EvaluateParameterizationDiagnostics(mesh, reportingUvs);
    EXPECT_EQ(
        diagnostics.Status,
        Param::ParameterizationDiagnosticsStatus::Success);
    EXPECT_EQ(diagnostics.FlippedElementCount, 1u);
    EXPECT_TRUE(std::isfinite(diagnostics.MeanSymmetricDirichletEnergy));
}

TEST(ParameterizationOptimize, AnalyticDirichletGradientMatchesFiniteDifference)
{
    const auto mesh = MakeRightTriangle();
    const Param::OptimizationReference reference =
        Param::PrepareOptimizationReference(mesh);
    std::vector<glm::dvec2> uvs{
        glm::dvec2{0.1, 0.2},
        glm::dvec2{1.5, 0.1},
        glm::dvec2{0.2, 1.2},
    };
    const Param::SymmetricDirichletResult analytic =
        Param::EvaluateSymmetricDirichlet(reference, uvs);
    ASSERT_TRUE(analytic.Succeeded());

    constexpr double epsilon = 1.0e-6;
    for (std::size_t vertex = 0u; vertex < uvs.size(); ++vertex)
    {
        for (std::size_t coordinate = 0u; coordinate < 2u; ++coordinate)
        {
            std::vector<glm::dvec2> plus = uvs;
            std::vector<glm::dvec2> minus = uvs;
            plus[vertex][coordinate] += epsilon;
            minus[vertex][coordinate] -= epsilon;
            const Param::SymmetricDirichletResult plusResult =
                Param::EvaluateSymmetricDirichlet(reference, plus);
            const Param::SymmetricDirichletResult minusResult =
                Param::EvaluateSymmetricDirichlet(reference, minus);
            ASSERT_TRUE(plusResult.Succeeded());
            ASSERT_TRUE(minusResult.Succeeded());
            const double finiteDifference =
                (plusResult.TotalEnergy - minusResult.TotalEnergy)
                / (2.0 * epsilon);
            EXPECT_NEAR(
                analytic.Gradient[vertex][coordinate],
                finiteDifference,
                2.0e-5)
                << "vertex=" << vertex << " coordinate=" << coordinate;
        }
    }
}

TEST(ParameterizationOptimize, ProxySystemsArePsdAndMatchCurrentGradients)
{
    const auto mesh = MakeRightTriangle();
    const Param::OptimizationReference reference =
        Param::PrepareOptimizationReference(mesh);
    const std::vector<glm::dvec2> uvs{
        glm::dvec2{0.0, 0.0},
        glm::dvec2{2.0, 0.0},
        glm::dvec2{0.0, 1.0},
    };
    const Param::LocalFitResult localFits =
        Param::FitLocalModels(reference, uvs);
    ASSERT_TRUE(localFits.Succeeded());

    const Param::ProxySystem arap = Param::AssembleProxySystem(
        reference,
        uvs,
        localFits,
        Param::ProxyEnergy::Arap);
    const Param::ProxySystem slim = Param::AssembleProxySystem(
        reference,
        uvs,
        localFits,
        Param::ProxyEnergy::SymmetricDirichlet);
    ExpectSymmetricPsd(arap);
    ExpectSymmetricPsd(slim);

    const std::vector<double> coordinates = BlockedCoordinates(uvs);
    std::vector<double> arapProduct(arap.Dimension, 0.0);
    std::vector<double> slimProduct(slim.Dimension, 0.0);
    arap.NormalMatrix.Multiply(coordinates, arapProduct);
    slim.NormalMatrix.Multiply(coordinates, slimProduct);

    constexpr double epsilon = 1.0e-6;
    for (std::size_t coordinate = 0u;
         coordinate < coordinates.size();
         ++coordinate)
    {
        const std::size_t vertex = coordinate % uvs.size();
        const std::size_t component = coordinate / uvs.size();
        std::vector<glm::dvec2> plus = uvs;
        std::vector<glm::dvec2> minus = uvs;
        plus[vertex][component] += epsilon;
        minus[vertex][component] -= epsilon;
        const Param::ArapEnergyResult plusEnergy =
            Param::EvaluateArapEnergy(reference, plus);
        const Param::ArapEnergyResult minusEnergy =
            Param::EvaluateArapEnergy(reference, minus);
        ASSERT_TRUE(plusEnergy.Succeeded());
        ASSERT_TRUE(minusEnergy.Succeeded());
        const double finiteDifference =
            (plusEnergy.TotalEnergy - minusEnergy.TotalEnergy)
            / (2.0 * epsilon);
        EXPECT_NEAR(
            2.0 * (arapProduct[coordinate] - arap.RightHandSide[coordinate]),
            finiteDifference,
            2.0e-5);
    }

    const Param::SymmetricDirichletResult dirichlet =
        Param::EvaluateSymmetricDirichlet(reference, uvs);
    ASSERT_TRUE(dirichlet.Succeeded());
    for (std::size_t vertex = 0u; vertex < uvs.size(); ++vertex)
    {
        EXPECT_NEAR(
            2.0 * (slimProduct[vertex] - slim.RightHandSide[vertex]),
            dirichlet.Gradient[vertex].x,
            2.0e-8);
        EXPECT_NEAR(
            2.0 * (
                slimProduct[uvs.size() + vertex]
                - slim.RightHandSide[uvs.size() + vertex]),
            dirichlet.Gradient[vertex].y,
            2.0e-8);
    }
}

TEST(ParameterizationOptimize, InjectiveSearchStopsInsideFirstFlipRoot)
{
    const auto mesh = MakeRightTriangle();
    const Param::OptimizationReference reference =
        Param::PrepareOptimizationReference(mesh);
    const std::vector<glm::dvec2> uvs{
        glm::dvec2{0.0, 0.0},
        glm::dvec2{2.0, 0.0},
        glm::dvec2{0.0, 1.0},
    };
    const std::vector<glm::dvec2> direction{
        glm::dvec2{0.0, 0.0},
        glm::dvec2{-1.0, 0.0},
        glm::dvec2{0.0, -2.0},
    };

    const Param::InjectiveStepResult boundary =
        Param::MaxInjectiveStep(reference, uvs, direction);
    ASSERT_TRUE(boundary.Succeeded());
    EXPECT_TRUE(boundary.LimitedByTriangle);
    EXPECT_EQ(boundary.LimitingFace, 0u);
    EXPECT_NEAR(boundary.MaximumStep, 0.5, 1.0e-12);

    const std::vector<glm::dvec2> extremeDirection{
        glm::dvec2{0.0, 0.0},
        glm::dvec2{-1.0e100, 0.0},
        glm::dvec2{0.0, 0.0},
    };
    const Param::InjectiveStepResult extremeBoundary =
        Param::MaxInjectiveStep(
            reference,
            IdentityTriangleUvs(),
            extremeDirection);
    ASSERT_TRUE(extremeBoundary.Succeeded());
    EXPECT_TRUE(extremeBoundary.LimitedByTriangle);
    EXPECT_GT(extremeBoundary.MaximumStep, 0.0);
    EXPECT_NEAR(
        extremeBoundary.MaximumStep / 1.0e-100,
        1.0,
        1.0e-12);

    const std::vector<glm::dvec2> tinyUvs{
        glm::dvec2{0.0, 0.0},
        glm::dvec2{1.0e-100, 0.0},
        glm::dvec2{0.0, 1.0e-100},
    };
    const std::vector<glm::dvec2> opposingExtremeDirection{
        glm::dvec2{0.0, 0.0},
        glm::dvec2{1.0e100, 0.0},
        glm::dvec2{0.0, -1.0e100},
    };
    const Param::InjectiveStepResult ratioExtremeBoundary =
        Param::MaxInjectiveStep(
            reference,
            tinyUvs,
            opposingExtremeDirection,
            1.0,
            1.0e-250);
    ASSERT_TRUE(ratioExtremeBoundary.Succeeded());
    EXPECT_TRUE(ratioExtremeBoundary.LimitedByTriangle);
    EXPECT_EQ(ratioExtremeBoundary.LimitingFace, 0u);
    EXPECT_GT(ratioExtremeBoundary.MaximumStep, 0.0);
    EXPECT_NEAR(
        ratioExtremeBoundary.MaximumStep / 1.0e-200,
        1.0,
        1.0e-12);

    const Param::InjectiveLineSearchResult search =
        Param::FindInjectiveDirichletStep(reference, uvs, direction);
    ASSERT_TRUE(search.Succeeded());
    EXPECT_GT(search.Step, 0.0);
    EXPECT_LT(search.Step, boundary.MaximumStep);
    EXPECT_LT(search.AcceptedEnergy, search.InitialEnergy);
    const std::vector<glm::dvec2> accepted{
        uvs[0u] + search.Step * direction[0u],
        uvs[1u] + search.Step * direction[1u],
        uvs[2u] + search.Step * direction[2u],
    };
    EXPECT_TRUE(
        Param::EvaluateSymmetricDirichlet(reference, accepted).Succeeded());

    const std::vector<glm::dvec2> zeroDirection(3u, glm::dvec2{0.0});
    EXPECT_EQ(
        Param::FindInjectiveDirichletStep(
            reference, uvs, zeroDirection).Status,
        Param::OptimizationStatus::NoDescentDirection);

    Param::InjectiveLineSearchParams noStepParams{};
    noStepParams.MinimumStep = 0.6;
    EXPECT_EQ(
        Param::FindInjectiveDirichletStep(
            reference, uvs, direction, noStepParams).Status,
        Param::OptimizationStatus::NoAcceptableStep);
}

TEST(ParameterizationOptimize, IdenticalInputsProduceBitwiseIdenticalOutputs)
{
    const auto mesh = MakeRightTriangle();
    const Param::OptimizationReference reference =
        Param::PrepareOptimizationReference(mesh);
    const std::vector<glm::dvec2> uvs{
        glm::dvec2{0.0, 0.0},
        glm::dvec2{2.0, 0.0},
        glm::dvec2{0.0, 1.0},
    };
    const Param::LocalFitResult fitsA = Param::FitLocalModels(reference, uvs);
    const Param::LocalFitResult fitsB = Param::FitLocalModels(reference, uvs);
    ASSERT_TRUE(fitsA.Succeeded());
    ASSERT_TRUE(fitsB.Succeeded());
    for (std::size_t column = 0u; column < 2u; ++column)
    {
        for (std::size_t row = 0u; row < 2u; ++row)
        {
            ExpectSameBits(
                fitsA.Faces[0u].Rotation[column][row],
                fitsB.Faces[0u].Rotation[column][row]);
        }
    }
    ExpectSameBits(
        fitsA.Faces[0u].SignedSingularValues,
        fitsB.Faces[0u].SignedSingularValues);

    const Param::SymmetricDirichletResult energyA =
        Param::EvaluateSymmetricDirichlet(reference, uvs);
    const Param::SymmetricDirichletResult energyB =
        Param::EvaluateSymmetricDirichlet(reference, uvs);
    ExpectSameBits(energyA.TotalEnergy, energyB.TotalEnergy);
    ASSERT_EQ(energyA.Gradient.size(), energyB.Gradient.size());
    for (std::size_t i = 0u; i < energyA.Gradient.size(); ++i)
        ExpectSameBits(energyA.Gradient[i], energyB.Gradient[i]);

    const Param::ProxySystem proxyA = Param::AssembleProxySystem(
        reference,
        uvs,
        fitsA,
        Param::ProxyEnergy::SymmetricDirichlet);
    const Param::ProxySystem proxyB = Param::AssembleProxySystem(
        reference,
        uvs,
        fitsB,
        Param::ProxyEnergy::SymmetricDirichlet);
    ASSERT_EQ(proxyA.NormalMatrix.Values.size(), proxyB.NormalMatrix.Values.size());
    for (std::size_t i = 0u; i < proxyA.NormalMatrix.Values.size(); ++i)
        ExpectSameBits(proxyA.NormalMatrix.Values[i], proxyB.NormalMatrix.Values[i]);
    ASSERT_EQ(proxyA.RightHandSide.size(), proxyB.RightHandSide.size());
    for (std::size_t i = 0u; i < proxyA.RightHandSide.size(); ++i)
        ExpectSameBits(proxyA.RightHandSide[i], proxyB.RightHandSide[i]);
}

TEST(ParameterizationOptimize, InvalidDynamicInputsReturnExplicitStatuses)
{
    const auto mesh = MakeRightTriangle();
    const Param::OptimizationReference reference =
        Param::PrepareOptimizationReference(mesh);
    std::vector<glm::dvec2> uvs = IdentityTriangleUvs();

    EXPECT_EQ(
        Param::FitLocalModels(
            reference,
            std::span<const glm::dvec2>{uvs.data(), 2u}).Status,
        Param::OptimizationStatus::SizeMismatch);
    uvs[1u].x = std::numeric_limits<double>::infinity();
    const Param::LocalFitResult nonFiniteFit =
        Param::FitLocalModels(reference, uvs);
    EXPECT_EQ(
        nonFiniteFit.Status,
        Param::OptimizationStatus::NonFiniteInput);
    ASSERT_EQ(nonFiniteFit.Faces.size(), 1u);
    EXPECT_TRUE(std::isfinite(nonFiniteFit.Faces[0u].Determinant));
    for (std::size_t column = 0u; column < 2u; ++column)
    {
        for (std::size_t row = 0u; row < 2u; ++row)
        {
            EXPECT_TRUE(std::isfinite(
                nonFiniteFit.Faces[0u].Jacobian[column][row]));
        }
    }

    uvs = IdentityTriangleUvs();
    uvs[2u] = uvs[0u];
    EXPECT_EQ(
        Param::FitLocalModels(reference, uvs).Status,
        Param::OptimizationStatus::DegenerateUv);
    EXPECT_EQ(
        Param::EvaluateSymmetricDirichlet(reference, uvs).Status,
        Param::OptimizationStatus::NonInjectiveInput);

    uvs = IdentityTriangleUvs();
    const Param::LocalFitResult validFit =
        Param::FitLocalModels(reference, uvs);
    ASSERT_TRUE(validFit.Succeeded());
    EXPECT_EQ(
        Param::AssembleProxySystem(
            reference,
            uvs,
            validFit,
            static_cast<Param::ProxyEnergy>(255u)).Status,
        Param::OptimizationStatus::InvalidProxyInput);

    std::vector<glm::dvec2> direction(3u, glm::dvec2{0.0});
    direction[2u].y = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(
        Param::MaxInjectiveStep(reference, uvs, direction).Status,
        Param::OptimizationStatus::NonFiniteInput);

    EXPECT_EQ(
        Param::MaxInjectiveStep(
            reference,
            uvs,
            std::span<const glm::dvec2>{direction.data(), 2u}).Status,
        Param::OptimizationStatus::SizeMismatch);
}

TEST(ParameterizationOptimize, PublicRecordsAreRevalidatedBeforeIndexing)
{
    const auto mesh = MakeRightTriangle();
    const Param::OptimizationReference reference =
        Param::PrepareOptimizationReference(mesh);
    const std::vector<glm::dvec2> uvs = IdentityTriangleUvs();
    const Param::LocalFitResult fits =
        Param::FitLocalModels(reference, uvs);
    ASSERT_TRUE(reference.Succeeded());
    ASSERT_TRUE(fits.Succeeded());

    Param::OptimizationReference wrongFaceStorage = reference;
    wrongFaceStorage.FaceStorageCount = 0u;
    EXPECT_EQ(
        Param::FitLocalModels(wrongFaceStorage, uvs).Status,
        Param::OptimizationStatus::SizeMismatch);

    Param::OptimizationReference extraFace = reference;
    extraFace.Faces.push_back(extraFace.Faces.front());
    EXPECT_EQ(
        Param::EvaluateArapEnergy(extraFace, uvs).Status,
        Param::OptimizationStatus::SizeMismatch);

    Param::OptimizationReference shortActiveVertices = reference;
    shortActiveVertices.ActiveVertices.clear();
    EXPECT_EQ(
        Param::AssembleProxySystem(
            shortActiveVertices,
            uvs,
            fits,
            Param::ProxyEnergy::Arap).Status,
        Param::OptimizationStatus::SizeMismatch);

    Param::OptimizationReference wrongActiveCount = reference;
    ++wrongActiveCount.ActiveFaceCount;
    EXPECT_EQ(
        Param::FitLocalModels(wrongActiveCount, uvs).Status,
        Param::OptimizationStatus::SizeMismatch);

    Param::OptimizationReference wrongVertex = reference;
    wrongVertex.Faces[0u].Vertices[2u] = reference.VertexStorageCount;
    EXPECT_EQ(
        Param::MaxInjectiveStep(
            wrongVertex,
            uvs,
            std::vector<glm::dvec2>(3u, glm::dvec2{0.0})).Status,
        Param::OptimizationStatus::SizeMismatch);

    Param::OptimizationReference duplicateVertex = reference;
    duplicateVertex.Faces[0u].Vertices[2u] =
        duplicateVertex.Faces[0u].Vertices[1u];
    EXPECT_EQ(
        Param::FitLocalModels(duplicateVertex, uvs).Status,
        Param::OptimizationStatus::DegenerateReference);

    Param::OptimizationReference nonFiniteArea = reference;
    nonFiniteArea.Faces[0u].Area =
        std::numeric_limits<double>::infinity();
    EXPECT_EQ(
        Param::EvaluateSymmetricDirichlet(nonFiniteArea, uvs).Status,
        Param::OptimizationStatus::NonFiniteInput);

    Param::OptimizationReference nonPositiveArea = reference;
    nonPositiveArea.Faces[0u].Area = 0.0;
    EXPECT_EQ(
        Param::FitLocalModels(nonPositiveArea, uvs).Status,
        Param::OptimizationStatus::DegenerateReference);

    Param::OptimizationReference nonFiniteGradient = reference;
    nonFiniteGradient.Faces[0u].Gradients[0u].x =
        std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(
        Param::EvaluateArapEnergy(nonFiniteGradient, uvs).Status,
        Param::OptimizationStatus::NonFiniteInput);

    Param::OptimizationReference wrongActiveDomain = reference;
    wrongActiveDomain.ActiveVertices[0u] = 0u;
    EXPECT_EQ(
        Param::FitLocalModels(wrongActiveDomain, uvs).Status,
        Param::OptimizationStatus::SizeMismatch);

    Param::LocalFitResult nonFiniteFit = fits;
    nonFiniteFit.Faces[0u].Rotation[0u][0u] =
        std::numeric_limits<double>::infinity();
    EXPECT_EQ(
        Param::AssembleProxySystem(
            reference,
            uvs,
            nonFiniteFit,
            Param::ProxyEnergy::Arap).Status,
        Param::OptimizationStatus::InvalidProxyInput);
}
