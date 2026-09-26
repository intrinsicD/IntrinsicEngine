// Harmonic-field binding on all element domains, constraint sources, labels, config
// validation and guarded undo/redo.
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
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
import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace S = Geometry::Smoothing;
namespace H = Geometry::HarmonicField;
using D = R::GeometryElementDomain;
using K = Geometry::PropertyValueKind;
namespace
{
    struct HarmonicHarness
    {
        Extrinsic::ECS::Scene::Registry Scene;
        R::EditorCommandHistory History;
        entt::entity Entity;
        R::EditorProcessingContext Context;
        R::HarmonicFieldConfig Config;
        explicit HarmonicHarness(D domain = D::MeshVertex)
        {
            Entity = Intrinsic::Tests::MakePointDomainSource(Scene, domain);
            Config.Input = {domain, "values", K::Double};
            Config.Output = {domain, "field", K::Double};
            Config.Positions = {domain, "samples", K::Vec3};
            Config.HardMask = {domain, "fixed", K::Bool};
            Config.SoftWeights.Domain = Config.Confidence.Domain = domain;
            Config.Weight = S::PropertyWeight::Uniform;
            auto& props = Props();
            auto samples = props.GetOrAdd<glm::vec3>("samples", {});
            auto values = props.GetOrAdd<double>("values", 0.);
            auto fixed = props.GetOrAdd<bool>("fixed", false);
            for (std::size_t i = 0; i < props.Size(); ++i) { samples[i] = {float(i), 0, 0}; values[i] = 100.0 + double(i); }
            // First and last rows are hard constraints with values 0 and 10.
            fixed[0] = fixed[props.Size() - 1] = true;
            values[0] = 0; values[props.Size() - 1] = 10;
            Context.Scene = &Scene; Context.CommandHistory = &History;
        }
        Geometry::PropertySet& Props() { return Intrinsic::Tests::PointDomainProperties(Scene, Entity, Config.Input.Domain); }
        auto Commands() { return R::BindEditorProcessingCommands(Context); }
        auto Id() { return R::SelectionController::ToStableEntityId(Entity); }
        auto Run() { return R::ApplyEditorHarmonicFieldCommand(Commands(), Id(), Config); }
    };
    constexpr std::array domains{D::MeshVertex, D::MeshEdge, D::MeshHalfedge, D::MeshFace,
                                 D::GraphNode, D::GraphEdge, D::GraphHalfedge, D::PointCloudPoint};
}

TEST(HarmonicFieldOperations, AllDomainsInterpolateBetweenHardRowsWithUndoRedo)
{
    for (auto domain : domains)
        for (auto order : {H::FieldOrder::Harmonic, H::FieldOrder::Biharmonic})
        {
            SCOPED_TRACE(int(domain));
            SCOPED_TRACE(int(order));
            HarmonicHarness h(domain);
            h.Config.Field.Order = order;
            const auto before = std::as_const(h.Props()).Get<double>("values").Vector();
            const auto result = h.Run();
            ASSERT_TRUE(result.Succeeded()) << result.Message;
            EXPECT_EQ(result.BackendId, "cpu_reference_sparse_cholesky");
            EXPECT_EQ(result.HardRows, 2u);
            EXPECT_EQ(result.LiveCount, h.Props().Size());
            const auto field = std::as_const(h.Props()).Get<double>("field").Vector();
            EXPECT_EQ(field.front(), 0.0);
            EXPECT_EQ(field.back(), 10.0);
            for (std::size_t i = 1; i + 1 < field.size(); ++i)
            {
                // Harmonic values obey the maximum principle; free inputs (100+) are ignored.
                if (order == H::FieldOrder::Harmonic) { EXPECT_GE(field[i], 0.0); EXPECT_LE(field[i], 10.0); }
                EXPECT_LT(std::abs(field[i]), 50.0);
            }
            EXPECT_EQ(std::as_const(h.Props()).Get<double>("values").Vector(), before);
            ASSERT_TRUE(h.History.Undo().Succeeded());
            EXPECT_FALSE(h.Props().Exists("field"));
            ASSERT_TRUE(h.History.Redo().Succeeded());
            EXPECT_EQ(std::as_const(h.Props()).Get<double>("field").Vector(), field);
        }
}

TEST(HarmonicFieldOperations, CotangentFieldWithPinnedBoundaryIsLinearOnAPlanarMesh)
{
    // 3x3 vertex grid: the center is the only interior vertex; the pinned boundary carries a
    // linear function, which the cotangent harmonic field reproduces exactly.
    HarmonicHarness h;
    Geometry::HalfedgeMesh::Mesh mesh;
    std::array<Geometry::VertexHandle, 9> v{};
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            v[y * 3 + x] = mesh.AddVertex({float(x), float(y), 0});
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
        {
            const auto a = v[y * 3 + x], b = v[y * 3 + x + 1], c = v[(y + 1) * 3 + x + 1], d = v[(y + 1) * 3 + x];
            ASSERT_TRUE(mesh.AddTriangle(a, b, c));
            ASSERT_TRUE(mesh.AddTriangle(a, c, d));
        }
    GS::PopulateFromMesh(h.Scene.Raw(), h.Entity, mesh);
    auto& props = h.Props();
    auto values = props.GetOrAdd<double>("values", 0.);
    const auto positions = std::as_const(props).Get<glm::vec3>("v:position");
    for (std::size_t i = 0; i < props.Size(); ++i) values[i] = i == 4 ? 99.0 : 3.0 * positions[i].x - 2.0 * positions[i].y;
    h.Config.Positions = {D::MeshVertex, "v:position", K::Vec3};
    h.Config.HardMask.Name.clear();
    h.Config.PinBoundary = true;
    h.Config.Weight = S::PropertyWeight::Cotangent;
    const auto result = h.Run();
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.HardRows, 8u);
    EXPECT_EQ(result.FreeRows, 1u);
    EXPECT_NEAR(std::as_const(props).Get<double>("field")[4], 3.0 - 2.0, 1e-9);
}

TEST(HarmonicFieldOperations, SoftWeightsAndVectorChannels)
{
    HarmonicHarness h(D::PointCloudPoint);
    auto& props = h.Props();
    auto vectors = props.GetOrAdd<glm::vec3>("targets", glm::vec3(0));
    auto weights = props.GetOrAdd<float>("weights", 0.f);
    vectors[0] = glm::vec3(1, 2, 3);
    vectors[4] = glm::vec3(-1, 0, 5);
    weights[0] = weights[4] = 1000.f;
    h.Config.Input = {D::PointCloudPoint, "targets", K::Vec3};
    h.Config.Output = {D::PointCloudPoint, "smooth_targets", K::Vec3};
    h.Config.HardMask.Name.clear();
    h.Config.SoftWeights = {D::PointCloudPoint, "weights", K::Float};
    const auto result = h.Run();
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.SoftRows, 2u);
    const auto out = std::as_const(props).Get<glm::vec3>("smooth_targets");
    EXPECT_NEAR(out[0].x, 1.0f, 0.02f);
    EXPECT_NEAR(out[4].z, 5.0f, 0.02f);
    // Negative weights are rejected without publishing.
    weights[2] = -1.f;
    const auto before = std::as_const(props).Get<glm::vec3>("smooth_targets").Vector();
    EXPECT_FALSE(h.Run().Succeeded());
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("smooth_targets").Vector(), before);
}

TEST(HarmonicFieldOperations, LabelsPropagateWithConfidenceInOneTransaction)
{
    HarmonicHarness h(D::PointCloudPoint);
    auto& props = h.Props();
    auto labels = props.GetOrAdd<std::int32_t>("labels", 0);
    labels[0] = 7;
    labels[4] = 3;
    h.Config.Mode = R::HarmonicFieldMode::Labels;
    h.Config.Input = {D::PointCloudPoint, "labels", K::Int32};
    h.Config.Output = {D::PointCloudPoint, "segments", K::Int32};
    h.Config.Confidence = {D::PointCloudPoint, "segment_confidence", K::Float};
    h.Config.WeightsPrefix = "segment_weight_";
    h.Config.HardMask.Name.clear();
    h.Config.Weight = S::PropertyWeight::InverseDistance;
    h.Config.Neighbors = 2;
    const auto result = h.Run();
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    const auto segments = std::as_const(props).Get<std::int32_t>("segments").Vector();
    // The graph is mirror-symmetric, so row 2 is an exact tie up to roundoff; the kernel tests
    // cover tie-breaking deterministically.
    EXPECT_EQ(segments[0], 7); EXPECT_EQ(segments[1], 7);
    EXPECT_EQ(segments[3], 3); EXPECT_EQ(segments[4], 3);
    EXPECT_TRUE(segments[2] == 3 || segments[2] == 7);
    const auto confidence = std::as_const(props).Get<float>("segment_confidence");
    EXPECT_FLOAT_EQ(confidence[0], 1.0f);
    EXPECT_NEAR(confidence[2], 0.5f, 1e-6f);
    EXPECT_EQ(result.WeightOutputs, 2u);
    const auto w3 = std::as_const(props).Get<float>("segment_weight_3");
    const auto w7 = std::as_const(props).Get<float>("segment_weight_7");
    ASSERT_TRUE(w3 && w7);
    for (std::size_t i = 0; i < props.Size(); ++i) EXPECT_NEAR(w3[i] + w7[i], 1.0f, 1e-6f) << i; // partition of unity
    EXPECT_FLOAT_EQ(w7[0], 1.0f);
    EXPECT_FLOAT_EQ(w3[4], 1.0f);
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_FALSE(props.Exists("segments"));
    EXPECT_FALSE(props.Exists("segment_confidence"));
    EXPECT_FALSE(props.Exists("segment_weight_3"));
    ASSERT_TRUE(h.History.Redo().Succeeded());
    EXPECT_EQ(std::as_const(props).Get<std::int32_t>("segments").Vector(), segments);
}

TEST(HarmonicFieldOperations, UnconstrainedComponentsFailUnlessKept)
{
    HarmonicHarness h(D::PointCloudPoint);
    auto& props = h.Props();
    auto fixed = props.Get<bool>("fixed");
    fixed[4] = false;
    // kNN with k=1 on x = 0..4 links {0,1} and {2,3,4}; only row 0 is constrained.
    h.Config.Neighbors = 1;
    auto samples = props.Get<glm::vec3>("samples");
    samples[2] = {10, 0, 0}; samples[3] = {11, 0, 0}; samples[4] = {12, 0, 0};
    const auto failed = h.Run();
    EXPECT_FALSE(failed.Succeeded());
    EXPECT_FALSE(props.Exists("field"));
    h.Config.Field.Unconstrained = H::UnconstrainedPolicy::KeepInput;
    const auto kept = h.Run();
    ASSERT_TRUE(kept.Succeeded()) << kept.Message;
    EXPECT_EQ(kept.UnconstrainedComponents, 1u);
    const auto field = std::as_const(props).Get<double>("field");
    EXPECT_EQ(field[1], 0.0);
    EXPECT_EQ(field[3], 103.0);
}

TEST(HarmonicFieldOperations, PoissonSourceGroundedNeumannAndTriharmonicOrder)
{
    HarmonicHarness h(D::PointCloudPoint);
    auto& props = h.Props();
    // Growing gaps make 1-NN symmetrize to the path 0-1-2-3-4 with unit weights.
    auto samples = props.Get<glm::vec3>("samples");
    const float x[] = {0, 1, 3, 6, 10};
    for (std::size_t i = 0; i < 5; ++i) samples[i] = {x[i], 0, 0};
    h.Config.Neighbors = 1;
    auto rho = props.GetOrAdd<double>("rho", 0.);

    // Poisson: L u = -2 with u = 0 and 16 at the ends reproduces u = i^2.
    for (std::size_t i = 0; i < 5; ++i) rho[i] = -2;
    props.Get<double>("values")[4] = 16;
    h.Config.Source = {D::PointCloudPoint, "rho", K::Double};
    auto poisson = h.Run();
    ASSERT_TRUE(poisson.Succeeded()) << poisson.Message;
    auto field = std::as_const(props).Get<double>("field").Vector();
    for (std::size_t i = 0; i < 5; ++i) EXPECT_NEAR(field[i], double(i * i), 1e-10) << i;

    // Pure Neumann: unit flux in at row 0 and out at row 4, no constraints, zero mean.
    for (std::size_t i = 0; i < 5; ++i) rho[i] = 0;
    rho[0] = 1; rho[4] = -1;
    h.Config.HardMask.Name.clear();
    h.Config.Field.Unconstrained = H::UnconstrainedPolicy::ZeroMean;
    const auto neumann = h.Run();
    ASSERT_TRUE(neumann.Succeeded()) << neumann.Message;
    EXPECT_EQ(neumann.GroundedComponents, 1u);
    EXPECT_EQ(neumann.MaxCompatibilityDefect, 0.0);
    field = std::as_const(props).Get<double>("field").Vector();
    for (std::size_t i = 0; i < 5; ++i) EXPECT_NEAR(field[i], 2.0 - double(i), 1e-10) << i;

    // Triharmonic interpolation keeps the hard rows exactly.
    h.Config.Source.Name.clear();
    h.Config.HardMask.Name = "fixed";
    h.Config.Field = {.Order = H::FieldOrder::Triharmonic};
    const auto tri = h.Run();
    ASSERT_TRUE(tri.Succeeded()) << tri.Message;
    field = std::as_const(props).Get<double>("field").Vector();
    EXPECT_EQ(field[0], 0.0);
    EXPECT_EQ(field[4], 16.0);
}

TEST(HarmonicFieldOperations, ConfigRoundtripAndFailClosedValidation)
{
    const auto registration = R::MakeHarmonicFieldConfigSectionRegistration();
    EXPECT_TRUE(registration.Validate(registration.DefaultSection.PayloadJson, {}, R::kHarmonicFieldConfigSectionName).Usable());
    R::HarmonicFieldConfig c;
    c.Mode = R::HarmonicFieldMode::Labels;
    c.Input = {D::GraphNode, "labels", K::Int32};
    c.Output = {D::GraphNode, "segments", K::Int32};
    c.Positions = {D::GraphNode, "v:position", K::Vec3};
    c.Confidence = {D::GraphNode, "confidence", K::Double};
    c.WeightsPrefix = "weight_";
    c.Weight = S::PropertyWeight::Gaussian;
    c.Unlabeled = -1;
    c.Field.Order = H::FieldOrder::Biharmonic;
    c.Field.Unconstrained = H::UnconstrainedPolicy::KeepInput;
    const auto payload = R::SerializeHarmonicFieldConfig(c);
    const auto valid = registration.Validate(payload, {}, R::kHarmonicFieldConfigSectionName);
    ASSERT_TRUE(valid.Usable());
    EXPECT_EQ(valid.CanonicalPayloadJson, payload);
    R::HarmonicFieldConfig poisson;
    poisson.Source = {D::MeshVertex, "rho", K::Float};
    poisson.LumpedMass = true; // area-weighted source
    poisson.Field.Unconstrained = H::UnconstrainedPolicy::ZeroMean;
    poisson.Field.Order = H::FieldOrder::Triharmonic;
    const auto poissonPayload = R::SerializeHarmonicFieldConfig(poisson);
    const auto poissonValid = registration.Validate(poissonPayload, {}, R::kHarmonicFieldConfigSectionName);
    ASSERT_TRUE(poissonValid.Usable());
    EXPECT_EQ(poissonValid.CanonicalPayloadJson, poissonPayload);
    for (const char* invalid : {
             R"({"mode":2,"pin_boundary":true})", R"({"unknown":1,"pin_boundary":true})",
             R"({})",                                                    // field mode without constraints
             R"({"pin_boundary":true,"order":4})", R"({"pin_boundary":true,"unconstrained":3})",
             R"({"pin_boundary":true,"weights_prefix":"w_"})",          // weights are label-mode outputs
             R"({"pin_boundary":true,"weights_prefix":""})",
             R"({"pin_boundary":true,"source":{"domain":"MeshVertex","name":"s","kind":"vec3"}})", // channel count
             R"({"mode":1,"input":{"domain":"MeshVertex","name":"l","kind":"int32"},"output":{"domain":"MeshVertex","name":"o","kind":"int32"},"unconstrained":2})",
             R"({"pin_boundary":true,"lumped_mass":true})",              // lumped mass needs biharmonic
             R"({"pin_boundary":true,"neighbors":0})", R"({"pin_boundary":true,"spatial_sigma":0})",
             R"({"pin_boundary":true,"unlabeled":4294967296})",
             R"({"pin_boundary":true,"confidence":{"domain":"MeshVertex","name":"c","kind":"float"}})",
             R"({"mode":1,"input":{"domain":"MeshVertex","name":"l","kind":"int32"},"output":{"domain":"MeshVertex","name":"o","kind":"int32"},"pin_boundary":true})",
             R"({"mode":1})",                                            // label mode needs int32 bindings
             R"({"pin_boundary":true,"weight":0,"positions":{"domain":"PointCloudPoint","name":"p","kind":"vec3"}})",
             R"({"hard_mask":{"domain":"MeshVertex","name":"m","kind":"float"}})"})
        EXPECT_FALSE(registration.Validate(invalid, {}, R::kHarmonicFieldConfigSectionName).Usable()) << invalid;
}

TEST(HarmonicFieldOperations, RejectsMissingConstraintPropertiesAndStaleHistory)
{
    HarmonicHarness h;
    h.Config.HardMask.Name = "missing";
    EXPECT_FALSE(R::PreviewEditorHarmonicFieldCommand(h.Commands(), h.Id(), h.Config).Enabled);
    h.Config.HardMask.Name = "fixed";
    ASSERT_TRUE(h.Run().Succeeded());
    h.Props().Get<double>("values")[0] = 5; // input revision changed after publication
    EXPECT_FALSE(h.History.Undo().Succeeded());
}
