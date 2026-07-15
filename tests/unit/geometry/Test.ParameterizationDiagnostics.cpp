#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

namespace
{
    constexpr double kTolerance = 1.0e-6;

    [[nodiscard]] std::vector<glm::vec2> IdentitySquareUvs()
    {
        return {
            glm::vec2{0.0f, 0.0f},
            glm::vec2{1.0f, 0.0f},
            glm::vec2{1.0f, 1.0f},
            glm::vec2{0.0f, 1.0f},
        };
    }
} // namespace

TEST(ParameterizationDiagnostics, IdentityTriangleHasZeroNormalizedError)
{
    const auto mesh = MakeRightTriangle();
    const std::vector<glm::vec2> uvs{
        glm::vec2{0.0f, 0.0f},
        glm::vec2{1.0f, 0.0f},
        glm::vec2{0.0f, 1.0f},
    };

    const auto diagnostics = Geometry::Parameterization::EvaluateParameterizationDiagnostics(mesh, uvs);

    EXPECT_EQ(diagnostics.Status, Geometry::Parameterization::ParameterizationDiagnosticsStatus::Success);
    EXPECT_EQ(diagnostics.EvaluatedFaceCount, 1u);
    EXPECT_EQ(diagnostics.SkippedFaceCount, 0u);
    EXPECT_EQ(diagnostics.FlippedElementCount, 0u);
    EXPECT_NEAR(diagnostics.MeanConformalDistortion, 1.0, kTolerance);
    EXPECT_NEAR(diagnostics.MeanConformalError, 0.0, kTolerance);
    EXPECT_NEAR(diagnostics.RootMeanSquareConformalError, 0.0, kTolerance);
    EXPECT_NEAR(diagnostics.MeanAreaRatio, 1.0, kTolerance);
    EXPECT_NEAR(diagnostics.MeanAreaError, 0.0, kTolerance);
    EXPECT_NEAR(diagnostics.MeanSymmetricDirichletExcess, 0.0, kTolerance);
    EXPECT_NEAR(diagnostics.MeanStretchError, 0.0, kTolerance);
}

TEST(ParameterizationDiagnostics, IdentitySquareReportsBoundaryMetrics)
{
    const auto mesh = MakeTwoTriangleSquare();
    const auto uvs = IdentitySquareUvs();

    const auto diagnostics = Geometry::Parameterization::EvaluateParameterizationDiagnostics(mesh, uvs);

    EXPECT_EQ(diagnostics.Status, Geometry::Parameterization::ParameterizationDiagnosticsStatus::Success);
    EXPECT_EQ(diagnostics.EvaluatedFaceCount, 2u);
    EXPECT_EQ(diagnostics.BoundaryLoopCount, 1u);
    EXPECT_EQ(diagnostics.BoundaryEdgeCount, 4u);
    EXPECT_EQ(diagnostics.SkippedBoundaryEdgeCount, 0u);
    EXPECT_NEAR(diagnostics.MeanBoundaryLengthRatio, 1.0, kTolerance);
    EXPECT_NEAR(diagnostics.MinBoundaryLengthRatio, 1.0, kTolerance);
    EXPECT_NEAR(diagnostics.MaxBoundaryLengthRatio, 1.0, kTolerance);
    EXPECT_NEAR(diagnostics.MeanBoundaryLengthDistortion, 1.0, kTolerance);
}

TEST(ParameterizationDiagnostics, StretchedRectangleHasPredictableDistortion)
{
    const auto mesh = MakeTwoTriangleSquare();
    const std::vector<glm::vec2> uvs{
        glm::vec2{0.0f, 0.0f},
        glm::vec2{2.0f, 0.0f},
        glm::vec2{2.0f, 1.0f},
        glm::vec2{0.0f, 1.0f},
    };

    const auto diagnostics = Geometry::Parameterization::EvaluateParameterizationDiagnostics(mesh, uvs);

    EXPECT_EQ(diagnostics.Status, Geometry::Parameterization::ParameterizationDiagnosticsStatus::Success);
    EXPECT_EQ(diagnostics.EvaluatedFaceCount, 2u);
    EXPECT_NEAR(diagnostics.MeanConformalDistortion, 2.0, kTolerance);
    EXPECT_NEAR(diagnostics.MaxConformalDistortion, 2.0, kTolerance);
    EXPECT_NEAR(diagnostics.RootMeanSquareConformalError, 1.0, kTolerance);
    EXPECT_NEAR(diagnostics.MeanAreaRatio, 2.0, kTolerance);
    EXPECT_NEAR(diagnostics.MeanAreaDistortion, 2.0, kTolerance);
    EXPECT_NEAR(diagnostics.MeanSymmetricDirichletEnergy, 3.125, kTolerance);
    EXPECT_NEAR(diagnostics.MeanStretch, 2.0, kTolerance);
    EXPECT_NEAR(diagnostics.MeanBoundaryLengthRatio, 1.5, kTolerance);
}

TEST(ParameterizationDiagnostics, FlippedUvReportsElementWithoutFailing)
{
    const auto mesh = MakeRightTriangle();
    const std::vector<glm::vec2> uvs{
        glm::vec2{0.0f, 0.0f},
        glm::vec2{0.0f, 1.0f},
        glm::vec2{1.0f, 0.0f},
    };

    const auto diagnostics = Geometry::Parameterization::EvaluateParameterizationDiagnostics(mesh, uvs);

    EXPECT_EQ(diagnostics.Status, Geometry::Parameterization::ParameterizationDiagnosticsStatus::Success);
    EXPECT_EQ(diagnostics.EvaluatedFaceCount, 1u);
    EXPECT_EQ(diagnostics.FlippedElementCount, 1u);
    EXPECT_NEAR(diagnostics.MeanConformalDistortion, 1.0, kTolerance);
}

TEST(ParameterizationDiagnostics, DegeneratePositionTriangleIsSkipped)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    const auto v2 = mesh.AddVertex({2.0f, 0.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);

    const std::vector<glm::vec2> uvs{
        glm::vec2{0.0f, 0.0f},
        glm::vec2{1.0f, 0.0f},
        glm::vec2{0.0f, 1.0f},
    };

    const auto diagnostics = Geometry::Parameterization::EvaluateParameterizationDiagnostics(mesh, uvs);

    EXPECT_EQ(diagnostics.Status, Geometry::Parameterization::ParameterizationDiagnosticsStatus::NoEvaluatedFaces);
    EXPECT_EQ(diagnostics.EvaluatedFaceCount, 0u);
    EXPECT_EQ(diagnostics.SkippedFaceCount, 1u);
    EXPECT_EQ(diagnostics.DegeneratePositionFaceCount, 1u);
}

TEST(ParameterizationDiagnostics, DegenerateUvTriangleIsSkipped)
{
    const auto mesh = MakeRightTriangle();
    const std::vector<glm::vec2> uvs{
        glm::vec2{0.0f, 0.0f},
        glm::vec2{1.0f, 0.0f},
        glm::vec2{2.0f, 0.0f},
    };

    const auto diagnostics = Geometry::Parameterization::EvaluateParameterizationDiagnostics(mesh, uvs);

    EXPECT_EQ(diagnostics.Status, Geometry::Parameterization::ParameterizationDiagnosticsStatus::NoEvaluatedFaces);
    EXPECT_EQ(diagnostics.EvaluatedFaceCount, 0u);
    EXPECT_EQ(diagnostics.SkippedFaceCount, 1u);
    EXPECT_EQ(diagnostics.DegenerateUvFaceCount, 1u);
}

TEST(ParameterizationDiagnostics, NonTriangleFaceIsReported)
{
    const auto mesh = MakeSingleQuad();
    const auto uvs = IdentitySquareUvs();

    const auto diagnostics = Geometry::Parameterization::EvaluateParameterizationDiagnostics(mesh, uvs);

    EXPECT_EQ(diagnostics.Status, Geometry::Parameterization::ParameterizationDiagnosticsStatus::NoEvaluatedFaces);
    EXPECT_EQ(diagnostics.NonTriangleFaceCount, 1u);
    EXPECT_EQ(diagnostics.SkippedFaceCount, 1u);
}

TEST(ParameterizationDiagnostics, MissingUvsFailBeforeEvaluation)
{
    const auto mesh = MakeRightTriangle();
    const std::vector<glm::vec2> uvs{
        glm::vec2{0.0f, 0.0f},
        glm::vec2{1.0f, 0.0f},
    };

    const auto diagnostics = Geometry::Parameterization::EvaluateParameterizationDiagnostics(mesh, uvs);

    EXPECT_EQ(diagnostics.Status, Geometry::Parameterization::ParameterizationDiagnosticsStatus::MissingUvCoordinates);
    EXPECT_EQ(diagnostics.EvaluatedFaceCount, 0u);
    EXPECT_EQ(diagnostics.SkippedFaceCount, 1u);
}

TEST(ParameterizationDiagnostics, NonFiniteUvsAreReported)
{
    const auto mesh = MakeRightTriangle();
    const std::vector<glm::vec2> uvs{
        glm::vec2{0.0f, 0.0f},
        glm::vec2{std::numeric_limits<float>::quiet_NaN(), 0.0f},
        glm::vec2{0.0f, 1.0f},
    };

    const auto diagnostics = Geometry::Parameterization::EvaluateParameterizationDiagnostics(mesh, uvs);

    EXPECT_EQ(diagnostics.Status, Geometry::Parameterization::ParameterizationDiagnosticsStatus::NoEvaluatedFaces);
    EXPECT_EQ(diagnostics.NonFiniteUvFaceCount, 1u);
    EXPECT_EQ(diagnostics.SkippedFaceCount, 1u);
}
