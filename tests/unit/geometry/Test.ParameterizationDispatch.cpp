// Test.ParameterizationDispatch.cpp — GEOM-063 right-sized CPU strategy dispatch.

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
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

    Geometry::HalfedgeMesh::Mesh MakeCollinearTriangle()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        const auto v2 = mesh.AddVertex({2.0f, 0.0f, 0.0f});
        (void)mesh.AddTriangle(v0, v1, v2);
        return mesh;
    }

    // Two disjoint square fans in one mesh: disk topology per component, but
    // two components, which is the case the editor kept reporting as an
    // unexplained "solver rejected the selected mesh".
    Geometry::HalfedgeMesh::Mesh MakeTwoDisjointSquareFans()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        for (int fan = 0; fan < 2; ++fan)
        {
            const float offset = static_cast<float>(fan) * 4.0f;
            const auto c0 = mesh.AddVertex({offset + 0.0f, 0.0f, 0.0f});
            const auto c1 = mesh.AddVertex({offset + 1.0f, 0.0f, 0.0f});
            const auto c2 = mesh.AddVertex({offset + 1.0f, 1.0f, 0.0f});
            const auto c3 = mesh.AddVertex({offset + 0.0f, 1.0f, 0.0f});
            const auto center = mesh.AddVertex({offset + 0.5f, 0.5f, 0.0f});
            (void)mesh.AddTriangle(center, c0, c1);
            (void)mesh.AddTriangle(center, c1, c2);
            (void)mesh.AddTriangle(center, c2, c3);
            (void)mesh.AddTriangle(center, c3, c0);
        }
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

    const auto puncturedTorus = MakePuncturedTorus();
    EXPECT_FALSE(Param::ComputeLSCM(puncturedTorus).has_value());
    const auto puncturedResult = Param::ParameterizeMesh(puncturedTorus, lscm);
    EXPECT_EQ(puncturedResult.Status, Param::ParameterizationStatus::InvalidInput);
    EXPECT_TRUE(puncturedResult.UVs.empty());

    Param::ParameterizationParams nonFiniteParams;
    nonFiniteParams.PinUV0.x = std::numeric_limits<float>::quiet_NaN();
    const Param::ParameterizationStrategy nonFinite{nonFiniteParams};
    const auto validDisk = MakeSquareFan();
    EXPECT_FALSE(Param::ComputeLSCM(validDisk, nonFiniteParams).has_value());
    const auto nonFiniteResult = Param::ParameterizeMesh(validDisk, nonFinite);
    EXPECT_EQ(nonFiniteResult.Status, Param::ParameterizationStatus::InvalidInput);
    EXPECT_TRUE(nonFiniteResult.UVs.empty());
}

// BUG-141: a rejection has to say why. The two preconditions every strategy
// shares are counted on the failure path and left untouched on success.
TEST(ParameterizationDispatch, RejectionCarriesConnectedComponentAndBoundaryLoopCounts)
{
    const Param::ParameterizationStrategy lscm{Param::ParameterizationParams{}};

    const auto disk = MakeSquareFan();
    const auto success = Param::ParameterizeMesh(disk, lscm);
    ASSERT_TRUE(success.Succeeded());
    EXPECT_FALSE(success.Rejection.Evaluated)
        << "the summary must not be paid for on the success path";

    // Closed surface: one component, no boundary loop.
    const auto closed = MakeTetrahedron();
    const auto closedResult = Param::ParameterizeMesh(closed, lscm);
    ASSERT_FALSE(closedResult.Succeeded());
    ASSERT_TRUE(closedResult.Rejection.Evaluated);
    EXPECT_EQ(closedResult.Rejection.ConnectedComponentCount, 1u);
    EXPECT_EQ(closedResult.Rejection.BoundaryLoopCount, 0u);
    EXPECT_TRUE(closedResult.Rejection.ViolatesDiskTopology());

    // Two disks: disk topology per component, two components.
    const auto disjoint = MakeTwoDisjointSquareFans();
    const auto disjointResult = Param::ParameterizeMesh(disjoint, lscm);
    ASSERT_FALSE(disjointResult.Succeeded());
    ASSERT_TRUE(disjointResult.Rejection.Evaluated);
    EXPECT_EQ(disjointResult.Rejection.ConnectedComponentCount, 2u);
    EXPECT_EQ(disjointResult.Rejection.BoundaryLoopCount, 2u);
    EXPECT_TRUE(disjointResult.Rejection.ViolatesDiskTopology());

    // Punctured torus: both counts are 1, so the counts alone do not explain
    // the rejection and the summary must not claim they do.
    const auto puncturedTorus = MakePuncturedTorus();
    const auto puncturedResult = Param::ParameterizeMesh(puncturedTorus, lscm);
    ASSERT_FALSE(puncturedResult.Succeeded());
    ASSERT_TRUE(puncturedResult.Rejection.Evaluated);
    EXPECT_EQ(puncturedResult.Rejection.ConnectedComponentCount, 1u);
    EXPECT_EQ(puncturedResult.Rejection.BoundaryLoopCount, 1u);
    EXPECT_FALSE(puncturedResult.Rejection.ViolatesDiskTopology());

    // Nothing to count at all.
    const Geometry::HalfedgeMesh::Mesh empty;
    const auto emptyResult = Param::ParameterizeMesh(empty, lscm);
    ASSERT_FALSE(emptyResult.Succeeded());
    EXPECT_FALSE(emptyResult.Rejection.Evaluated);
    EXPECT_FALSE(emptyResult.Rejection.ViolatesDiskTopology());
}

TEST(ParameterizationDispatch, UnusableDiagnosticsFailClosedWithoutUvs)
{
    const auto mesh = MakeCollinearTriangle();
    const auto direct = Param::ComputeHarmonic(mesh, Param::HarmonicParams{});
    ASSERT_TRUE(direct.has_value());
    ASSERT_EQ(direct->Status, Param::HarmonicStatus::Success);
    ASSERT_EQ(
        direct->Diagnostics.Status,
        Param::ParameterizationDiagnosticsStatus::NoEvaluatedFaces);

    const Param::ParameterizationStrategy strategy{Param::HarmonicParams{}};
    const auto dispatched = Param::ParameterizeMesh(mesh, strategy);
    EXPECT_EQ(dispatched.Status, Param::ParameterizationStatus::InvalidInput);
    EXPECT_TRUE(dispatched.UVs.empty());
    EXPECT_EQ(
        dispatched.Diagnostics.Status,
        Param::ParameterizationDiagnosticsStatus::NoEvaluatedFaces);
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
