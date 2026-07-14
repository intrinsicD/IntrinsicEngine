// Test.ParameterizationDispatch.cpp — GEOM-063 right-sized CPU strategy dispatch.

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

namespace
{
    namespace Param = Geometry::Parameterization;

    Geometry::HalfedgeMesh::Mesh MakeSquareFan()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto c0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto c1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        const auto c2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
        const auto c3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
        const auto center = mesh.AddVertex({0.5f, 0.5f, 0.0f});
        (void)mesh.AddTriangle(center, c0, c1);
        (void)mesh.AddTriangle(center, c1, c2);
        (void)mesh.AddTriangle(center, c2, c3);
        (void)mesh.AddTriangle(center, c3, c0);
        return mesh;
    }

    void ExpectUvsEqual(
        const std::vector<glm::vec2>& actual,
        const std::vector<glm::vec2>& expected)
    {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            EXPECT_EQ(actual[i].x, expected[i].x) << "vertex " << i << " u";
            EXPECT_EQ(actual[i].y, expected[i].y) << "vertex " << i << " v";
        }
    }
}

TEST(ParameterizationDispatch, LscmMatchesDirectSolverAndCarriesDiagnostics)
{
    const auto mesh = MakeSquareFan();
    const Param::ParameterizationParams params{};
    const auto direct = Param::ComputeLSCM(mesh, params);

    ASSERT_TRUE(direct.has_value());
    ASSERT_TRUE(direct->Converged);
    ASSERT_EQ(direct->Diagnostics.Status, Param::ParameterizationDiagnosticsStatus::Success);

    const Param::ParameterizationStrategy strategy{params};
    const auto dispatched = Param::ParameterizeMesh(mesh, strategy);

    ASSERT_TRUE(dispatched.Succeeded());
    EXPECT_EQ(dispatched.Status, Param::ParameterizationStatus::Success);
    EXPECT_EQ(dispatched.Diagnostics.Status, Param::ParameterizationDiagnosticsStatus::Success);
    EXPECT_EQ(dispatched.Diagnostics.EvaluatedFaceCount, direct->Diagnostics.EvaluatedFaceCount);
    EXPECT_EQ(dispatched.Diagnostics.FlippedElementCount, direct->Diagnostics.FlippedElementCount);
    ExpectUvsEqual(dispatched.UVs, direct->UVs);
}

TEST(ParameterizationDispatch, HarmonicAndTutteMatchDirectSolver)
{
    const auto mesh = MakeSquareFan();
    Param::HarmonicParams params;
    params.Boundary = Param::HarmonicBoundaryPolicy::Circle;

    for (const auto weights : {Param::HarmonicWeightType::Cotangent, Param::HarmonicWeightType::Uniform})
    {
        params.Weights = weights;
        const auto direct = Param::ComputeHarmonic(mesh, params);
        ASSERT_TRUE(direct.has_value());
        ASSERT_EQ(direct->Status, Param::HarmonicStatus::Success);

        const Param::ParameterizationStrategy strategy{params};
        const auto dispatched = Param::ParameterizeMesh(mesh, strategy);

        ASSERT_TRUE(dispatched.Succeeded());
        EXPECT_EQ(dispatched.Status, Param::ParameterizationStatus::Success);
        EXPECT_EQ(dispatched.Diagnostics.Status, Param::ParameterizationDiagnosticsStatus::Success);
        EXPECT_EQ(dispatched.Diagnostics.EvaluatedFaceCount, direct->Diagnostics.EvaluatedFaceCount);
        EXPECT_EQ(dispatched.Diagnostics.FlippedElementCount, direct->Diagnostics.FlippedElementCount);
        ExpectUvsEqual(dispatched.UVs, direct->UVs);
    }
}

TEST(ParameterizationDispatch, InvalidInputsFailClosedThroughStatus)
{
    const Geometry::HalfedgeMesh::Mesh empty;
    const Param::ParameterizationStrategy lscm{Param::ParameterizationParams{}};
    const auto emptyResult = Param::ParameterizeMesh(empty, lscm);
    EXPECT_EQ(emptyResult.Status, Param::ParameterizationStatus::InvalidInput);
    EXPECT_TRUE(emptyResult.UVs.empty());

    const auto closed = MakeTetrahedron();
    const Param::ParameterizationStrategy harmonic{Param::HarmonicParams{}};
    const auto closedResult = Param::ParameterizeMesh(closed, harmonic);
    EXPECT_EQ(closedResult.Status, Param::ParameterizationStatus::InvalidInput);
    EXPECT_TRUE(closedResult.UVs.empty());
}

TEST(ParameterizationDispatch, LscmNonConvergenceIsSolverFailureWithoutUvs)
{
    const auto mesh = MakeSquareFan();
    Param::ParameterizationParams params;
    params.MaxSolverIterations = 0;

    const auto direct = Param::ComputeLSCM(mesh, params);
    ASSERT_TRUE(direct.has_value());
    ASSERT_FALSE(direct->Converged);

    const Param::ParameterizationStrategy strategy{params};
    const auto dispatched = Param::ParameterizeMesh(mesh, strategy);
    EXPECT_EQ(dispatched.Status, Param::ParameterizationStatus::SolverFailed);
    EXPECT_TRUE(dispatched.UVs.empty());
}

TEST(ParameterizationDispatch, RepeatedCallsAreDeterministic)
{
    const auto mesh = MakeSquareFan();

    const Param::ParameterizationStrategy lscm{Param::ParameterizationParams{}};
    const auto lscmA = Param::ParameterizeMesh(mesh, lscm);
    const auto lscmB = Param::ParameterizeMesh(mesh, lscm);
    ASSERT_TRUE(lscmA.Succeeded());
    ASSERT_TRUE(lscmB.Succeeded());
    ExpectUvsEqual(lscmA.UVs, lscmB.UVs);

    Param::HarmonicParams harmonicParams;
    harmonicParams.Weights = Param::HarmonicWeightType::Uniform;
    const Param::ParameterizationStrategy harmonic{harmonicParams};
    const auto harmonicA = Param::ParameterizeMesh(mesh, harmonic);
    const auto harmonicB = Param::ParameterizeMesh(mesh, harmonic);
    ASSERT_TRUE(harmonicA.Succeeded());
    ASSERT_TRUE(harmonicB.Succeeded());
    ExpectUvsEqual(harmonicA.UVs, harmonicB.UVs);
}
