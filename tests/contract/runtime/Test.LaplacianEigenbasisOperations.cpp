// UI-056 / METHOD-051: spectral modes operation — analytic spectrum, mesh lumped mass, modal
// operators of Hildebrandt et al. 2012, signatures and distances, config and history.
#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include "PointDomainFixture.hpp"
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.DEC;
import Geometry.ModalAnalysis;
import Geometry.Sparse;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace S = Geometry::Smoothing;
using D = R::GeometryElementDomain;
using K = Geometry::PropertyValueKind;
namespace
{
    struct Harness
    {
        Extrinsic::ECS::Scene::Registry Scene;
        R::EditorCommandHistory History;
        entt::entity Entity;
        R::EditorProcessingContext Context;
        R::LaplacianEigenbasisConfig Config;
        explicit Harness(D domain)
        {
            Entity = Intrinsic::Tests::MakePointDomainSource(Scene, domain);
            Props(domain).Resize(16);
            auto samples = Props(domain).GetOrAdd<glm::vec3>("samples", {});
            for (std::size_t i = 0; i < Props(domain).Size(); ++i) samples[i] = {float(i), 0, 0};
            Config.Domain = domain;
            Config.Positions = {domain, "samples", K::Vec3};
            Config.Weight = S::PropertyWeight::Uniform;
            Config.Neighbors = 1;
            Config.LumpedMass = false;
            Config.Count = 4;
            Context.Scene = &Scene;
            Context.CommandHistory = &History;
        }
        Geometry::PropertySet& Props(D domain) { return Intrinsic::Tests::PointDomainProperties(Scene, Entity, domain); }
        auto Commands() { return R::BindEditorProcessingCommands(Context); }
        auto Id() { return R::SelectionController::ToStableEntityId(Entity); }
        auto Run() { return R::ApplyEditorLaplacianEigenbasisCommand(Commands(), Id(), Config); }
    };
}

TEST(LaplacianEigenbasisOperations, OneNearestNeighborLineGivesThePathSpectrum)
{
    for (auto domain : {D::GraphNode, D::PointCloudPoint, D::MeshVertex})
    {
        SCOPED_TRACE(int(domain));
        Harness h(domain);
        const std::size_t n = h.Props(domain).Size();
        const auto result = h.Run();
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        ASSERT_EQ(result.Eigenvalues.size(), 4u);
        for (std::size_t j = 0; j < 4; ++j)
            EXPECT_NEAR(result.Eigenvalues[j], 2.0 - 2.0 * std::cos(double(j) * std::numbers::pi / double(n)), 1e-9);
        ASSERT_EQ(result.Outputs.size(), 4u);
        const auto first = std::as_const(h.Props(domain)).Get<float>("eigen_0").Vector();
        for (const float v : first) EXPECT_NEAR(v, 1.0f / std::sqrt(float(n)), 1e-5f) << "constant nullspace, unit mass";
        ASSERT_TRUE(h.History.Undo().Succeeded());
        EXPECT_FALSE(h.Props(domain).Exists("eigen_0"));
        ASSERT_TRUE(h.History.Redo().Succeeded());
        EXPECT_TRUE(h.Props(domain).Exists("eigen_3"));
    }
}

TEST(LaplacianEigenbasisOperations, CotangentMeshEigenvectorsAreMassOrthonormal)
{
    Harness h(D::MeshVertex);
    Geometry::HalfedgeMesh::Mesh mesh;
    std::vector<Geometry::VertexHandle> v;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) v.push_back(mesh.AddVertex({float(x), float(y) + 0.1f * float(x % 3), 0.1f * float(y % 2)}));
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 7; ++x)
        {
            (void)mesh.AddTriangle(v[std::size_t(y * 8 + x)], v[std::size_t(y * 8 + x + 1)], v[std::size_t((y + 1) * 8 + x + 1)]);
            (void)mesh.AddTriangle(v[std::size_t(y * 8 + x)], v[std::size_t((y + 1) * 8 + x + 1)], v[std::size_t((y + 1) * 8 + x)]);
        }
    GS::PopulateFromMesh(h.Scene.Raw(), h.Entity, mesh);
    h.Config.Positions = {D::MeshVertex, "v:position", K::Vec3};
    h.Config.Weight = S::PropertyWeight::Cotangent;
    h.Config.LumpedMass = true;
    h.Config.Count = 6;
    const auto result = h.Run();
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_NEAR(result.Eigenvalues[0], 0.0, 1e-9);
    for (std::size_t j = 1; j < 6; ++j) EXPECT_GE(result.Eigenvalues[j], result.Eigenvalues[j - 1]);
    for (const double r : result.RelativeResiduals) EXPECT_LE(r, 1e-10);
    const auto mass = Geometry::DEC::BuildOperators(mesh).Hodge0.Diagonal;
    std::vector<std::vector<float>> vectors;
    for (int j = 0; j < 6; ++j) vectors.push_back(std::as_const(h.Props(D::MeshVertex)).Get<float>("eigen_" + std::to_string(j)).Vector());
    for (int a = 0; a < 6; ++a)
        for (int b = 0; b < 6; ++b)
        {
            double gram = 0;
            for (std::size_t i = 0; i < mass.size(); ++i) gram += mass[i] * double(vectors[a][i]) * double(vectors[b][i]);
            EXPECT_NEAR(gram, a == b ? 1.0 : 0.0, 1e-5) << a << "," << b;
        }
}

TEST(LaplacianEigenbasisOperations, ConfigValidationAndRejections)
{
    const auto registration = R::MakeLaplacianEigenbasisConfigSectionRegistration();
    R::LaplacianEigenbasisConfig c;
    c.Count = 12; c.OutputPrefix = "v:mode_"; c.Tolerance = 1e-9; c.MaxIterations = 321;
    const auto payload = R::SerializeLaplacianEigenbasisConfig(c);
    const auto valid = registration.Validate(payload, {}, R::kLaplacianEigenbasisConfigSectionName);
    ASSERT_TRUE(valid.Usable());
    EXPECT_EQ(valid.CanonicalPayloadJson, payload);
    for (const char* invalid : {"{\"count\":0}", "{\"count\":257}", "{\"domain\":0}", "{\"domain\":9}", "{\"weight\":5}",
                                "{\"output_prefix\":\"\"}", "{\"tolerance\":1}", "{\"max_iterations\":0}", "{\"unknown\":1}",
                                "{\"domain\":5,\"weight\":3}", "{\"domain\":5,\"lumped_mass\":true,\"weight\":0}"})
        EXPECT_FALSE(registration.Validate(invalid, {}, R::kLaplacianEigenbasisConfigSectionName).Usable()) << invalid;

    Harness h(D::GraphNode);
    h.Config.Count = std::uint32_t(h.Props(D::GraphNode).Size());
    EXPECT_FALSE(h.Run().Succeeded()) << "k must be smaller than the live row count";
    h.Config.Count = 2;
    (void)h.Props(D::GraphNode).GetOrAdd<double>("eigen_1", 0.0);
    EXPECT_FALSE(R::PreviewEditorLaplacianEigenbasisCommand(h.Commands(), h.Id(), h.Config).Enabled)
        << "an output with another storage type is rejected";
    EXPECT_FALSE(h.Props(D::GraphNode).Exists("eigen_0"));
}

namespace
{
    Geometry::HalfedgeMesh::Mesh Bumpy(int size)
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        std::vector<Geometry::VertexHandle> v;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                v.push_back(mesh.AddVertex({float(x), float(y), 0.5f * std::sin(0.9f * float(x)) * std::cos(0.6f * float(y))}));
        for (int y = 0; y + 1 < size; ++y)
            for (int x = 0; x + 1 < size; ++x)
            {
                const auto at = [&](int i, int j) { return v[std::size_t(j * size + i)]; };
                (void)mesh.AddTriangle(at(x, y), at(x + 1, y), at(x + 1, y + 1));
                (void)mesh.AddTriangle(at(x, y), at(x + 1, y + 1), at(x, y + 1));
            }
        return mesh;
    }
}

TEST(LaplacianEigenbasisOperations, ModalOperatorsMatchTheGeometryKernels)
{
    auto mesh = Bumpy(6);
    const auto mass = Geometry::DEC::BuildHodgeStar0(mesh, Geometry::DEC::MassMode::Barycentric);
    for (const auto op : {R::ModalOperator::ModifiedDirichlet, R::ModalOperator::ThinShell})
    {
        SCOPED_TRACE(int(op));
        Harness h(D::MeshVertex);
        auto copy = mesh;
        GS::PopulateFromMesh(h.Scene.Raw(), h.Entity, copy);
        h.Config.Operator = op;
        h.Config.Positions = {D::MeshVertex, "v:position", K::Vec3};
        h.Config.LumpedMass = true;
        h.Config.Count = op == R::ModalOperator::ThinShell ? 9 : 5;
        h.Config.SkipModes = op == R::ModalOperator::ThinShell ? 6 : 0;
        h.Config.SignatureOutput = "modal_signature";
        const auto result = h.Run();
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        // Oracle: the same geometry kernel solved directly over every vertex.
        const bool shell = op == R::ModalOperator::ThinShell;
        const auto A = shell ? Geometry::ModalAnalysis::BuildThinShellHessian(mesh).Hessian
                             : Geometry::ModalAnalysis::BuildModifiedDirichletMatrix(mesh).Matrix;
        const auto M = shell ? Geometry::ModalAnalysis::ExpandMass(mass, 3) : mass;
        const auto oracle = Geometry::Sparse::SolveSymmetricGeneralizedEigen(A, M, {.Count = h.Config.Count});
        ASSERT_TRUE(oracle.Succeeded());
        for (std::size_t j = 0; j < h.Config.Count; ++j)
            EXPECT_NEAR(result.Eigenvalues[j], oracle.Eigenvalues[j], 1e-8 * std::max(1.0, oracle.Eigenvalues[j])) << j;
        const auto& props = std::as_const(h.Props(D::MeshVertex));
        if (shell) EXPECT_TRUE(props.Get<glm::vec3>("eigen_8"));
        else EXPECT_TRUE(props.Get<float>("eigen_4"));
        // Σ_v m_v S_t(v) = Σ_{j ≥ skip} e^{-λ_j t} for M-normalized modes.
        const auto signature = props.Get<float>("modal_signature").Vector();
        double weighted = 0.0, expected = 0.0;
        for (std::size_t i = 0; i < signature.size(); ++i) weighted += mass.Diagonal[i] * double(signature[i]);
        for (std::size_t j = h.Config.SkipModes; j < result.Eigenvalues.size(); ++j) expected += std::exp(-result.Eigenvalues[j] * result.SignatureTime);
        EXPECT_NEAR(weighted, expected, 1e-5 * expected);
        EXPECT_GT(result.ScaleMax, result.ScaleMin);
        ASSERT_TRUE(h.History.Undo().Succeeded());
        EXPECT_FALSE(h.Props(D::MeshVertex).Exists("modal_signature"));
    }
}

TEST(LaplacianEigenbasisOperations, HeatKernelDistanceVanishesAtTheSourceAndRejectsBadSources)
{
    Harness h(D::MeshVertex);
    auto mesh = Bumpy(6);
    GS::PopulateFromMesh(h.Scene.Raw(), h.Entity, mesh);
    h.Config.Positions = {D::MeshVertex, "v:position", K::Vec3};
    h.Config.Weight = S::PropertyWeight::Cotangent;
    h.Config.LumpedMass = true;
    h.Config.Count = 8;
    h.Config.SkipModes = 1;
    h.Config.DistanceSource = 99;
    EXPECT_FALSE(h.Run().Succeeded()) << "source outside the live rows";
    EXPECT_FALSE(h.Props(D::MeshVertex).Exists("eigen_0"));
    h.Config.DistanceSource = 14;
    const auto result = h.Run();
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    const auto distance = std::as_const(h.Props(D::MeshVertex)).Get<float>("modal_distance").Vector();
    EXPECT_EQ(distance[14], 0.0f);
    EXPECT_GT(*std::ranges::max_element(distance), 0.0f);
    EXPECT_EQ(result.Outputs.back(), "modal_distance");
}

TEST(LaplacianEigenbasisOperations, ModalConfigValidation)
{
    const auto registration = R::MakeLaplacianEigenbasisConfigSectionRegistration();
    R::LaplacianEigenbasisConfig c;
    c.Operator = R::ModalOperator::ThinShell; c.Count = 12; c.SkipModes = 6; c.ShellFlexural = 3.0;
    c.SignatureOutput = "sig"; c.SignatureScale = 0.25; c.DistanceSource = 4; c.DistanceSamples = 16;
    const auto payload = R::SerializeLaplacianEigenbasisConfig(c);
    const auto valid = registration.Validate(payload, {}, R::kLaplacianEigenbasisConfigSectionName);
    ASSERT_TRUE(valid.Usable());
    EXPECT_EQ(valid.CanonicalPayloadJson, payload);
    for (const char* invalid : {"{\"operator\":3}", "{\"operator\":1,\"domain\":5,\"lumped_mass\":false,\"weight\":0}",
                                "{\"operator\":2,\"shell_flexural\":0,\"shell_length\":0,\"shell_area\":0}",
                                "{\"shell_area\":-1}", "{\"skip_modes\":10}", "{\"signature_scale\":1.5}",
                                "{\"distance_samples\":0}", "{\"distance_source\":1.5}",
                                "{\"signature_output\":\"eigen_2\"}", "{\"distance_source\":0,\"distance_output\":\"\"}",
                                "{\"signature_output\":\"v:position\"}"})
        EXPECT_FALSE(registration.Validate(invalid, {}, R::kLaplacianEigenbasisConfigSectionName).Usable()) << invalid;
}
