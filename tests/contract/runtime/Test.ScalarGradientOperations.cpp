// Analytic face gradients, typed bindings, guarded history and config validation.
#include <cmath>
#include <limits>
#include <string>
#include <vector>
#include <utility>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.SelectionController;
import Geometry.HalfedgeMesh;
import Geometry.Properties;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace
{
    struct GradientHarness
    {
        Extrinsic::ECS::Scene::Registry Scene;
        R::EditorCommandHistory History;
        Extrinsic::ECS::EntityHandle Entity;
        R::EditorProcessingContext Context;
        R::ScalarGradientConfig Config;
        GradientHarness(bool polygon = false, bool deletedFace = false)
        {
            Geometry::HalfedgeMesh::Mesh mesh;
            const auto a = mesh.AddVertex({0,0,0}), b = mesh.AddVertex({1,0,0});
            const auto c = mesh.AddVertex({1,1,0}), d = mesh.AddVertex({0,1,0});
            if (polygon) EXPECT_TRUE(mesh.AddQuad(a,b,c,d));
            else
            {
                const auto first = mesh.AddTriangle(a,b,c);
                EXPECT_TRUE(first);
                EXPECT_TRUE(mesh.AddTriangle(a,c,d));
                if (deletedFace) mesh.DeleteFace(*first);
            }
            Entity = Scene.Create();
            GS::PopulateFromMesh(Scene.Raw(), Entity, mesh);
            auto values = Vertices().GetOrAdd<double>("temperature", 0.);
            const auto positions = std::as_const(Vertices()).Get<glm::vec3>("v:position");
            for (std::size_t i=0; i<values.Size(); ++i) values[i] = 2*positions[i].x + 3*positions[i].y;
            Config.Scalar.Name = "temperature";
            Context.Scene = &Scene;
            Context.CommandHistory = &History;
        }
        Geometry::PropertySet& Vertices() { return Scene.Raw().get<GS::Vertices>(Entity).Properties; }
        Geometry::PropertySet& Faces() { return Scene.Raw().get<GS::Faces>(Entity).Properties; }
        auto Commands() { return R::BindEditorProcessingCommands(Context); }
        auto Id() { return R::SelectionController::ToStableEntityId(Entity); }
        auto Run() { return R::ApplyEditorScalarGradientCommand(Commands(), Id(), Config); }
    };
}
TEST(ScalarGradientOperations, AnalyticFieldPublishesFacesAndUndoRedo)
{
    GradientHarness h;
    (void)h.Faces().GetOrAdd<float>("unrelated", 7.f);
    const auto positions = std::as_const(h.Vertices()).Get<glm::vec3>("v:position").Vector();
    const auto result = h.Run();
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.FaceCount, 2u);
    auto gradients = std::as_const(h.Faces()).Get<glm::vec3>("f:scalar_gradient");
    ASSERT_TRUE(gradients);
    for (const auto g : gradients.Vector()) EXPECT_EQ(g, glm::vec3(2,3,0));
    EXPECT_EQ(h.Vertices().Get<glm::vec3>("v:position").Vector(), positions);
    EXPECT_EQ(h.Faces().Get<float>("unrelated").Vector(), std::vector<float>(2,7.f));
    EXPECT_EQ(h.Run().Status, R::EditorCommandStatus::NoChange);
    EXPECT_EQ(h.History.UndoCount(), 1u);
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_FALSE(h.Faces().Exists("f:scalar_gradient"));
    ASSERT_TRUE(h.History.Redo().Succeeded());
    EXPECT_EQ(h.Faces().Get<glm::vec3>("f:scalar_gradient")[1], glm::vec3(2,3,0));
}
TEST(ScalarGradientOperations, AlternatePositionsAndIntegerScalar)
{
    GradientHarness h;
    auto positions = h.Vertices().GetOrAdd<glm::vec3>("rest", {});
    positions.Vector() = std::as_const(h.Vertices()).Get<glm::vec3>("v:position").Vector();
    for (auto& p : positions.Vector()) p *= 2;
    auto scalar = h.Vertices().GetOrAdd<std::int32_t>("integer_field", 0);
    scalar.Vector() = {0,2,5,3};
    h.Config.Positions.Name = "rest";
    h.Config.Scalar.Name = "integer_field";
    h.Config.Scalar.ValueKind = Geometry::PropertyValueKind::Int32;
    ASSERT_TRUE(h.Run().Succeeded());
    EXPECT_EQ(h.Faces().Get<glm::vec3>("f:scalar_gradient")[0], glm::vec3(1,1.5,0));
    h.Vertices().Get<double>("temperature")[0] = 123.;
    EXPECT_TRUE(h.History.Undo().Succeeded());
    EXPECT_TRUE(h.History.Redo().Succeeded());
    scalar[0] = 99;
    EXPECT_FALSE(h.History.Undo().Succeeded());
}
TEST(ScalarGradientOperations, RejectsInvalidBindingsAndNonfiniteInputWithoutMutation)
{
    GradientHarness h;
    h.Config.Scalar.Domain = R::GeometryElementDomain::MeshFace;
    EXPECT_FALSE(h.Run().Succeeded());
    h.Config.Scalar.Domain = R::GeometryElementDomain::MeshVertex;
    h.Config.Output.Name = "f:deleted";
    EXPECT_FALSE(h.Run().Succeeded());
    h.Config.Output.Name = "wrong_storage";
    (void)h.Faces().GetOrAdd<float>("wrong_storage", 7.f);
    EXPECT_FALSE(h.Run().Succeeded());
    h.Config.Output.Name = "f:scalar_gradient";
    h.Vertices().Get<double>("temperature")[0] = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(h.Run().Succeeded());
    EXPECT_FALSE(h.Faces().Exists("f:scalar_gradient"));
    EXPECT_EQ(h.History.UndoCount(), 0u);
}
TEST(ScalarGradientOperations, PolygonRejectedAndDeletedFaceSlotsPreserved)
{
    GradientHarness polygon(true);
    EXPECT_FALSE(polygon.Run().Succeeded());
    GradientHarness deleted(false, true);
    auto old = deleted.Faces().GetOrAdd<glm::vec3>("f:scalar_gradient", {9,9,9});
    ASSERT_TRUE(deleted.Run().Succeeded());
    EXPECT_EQ(old[0], glm::vec3(9,9,9));
    EXPECT_EQ(old[1], glm::vec3(2,3,0));
    ASSERT_TRUE(deleted.History.Undo().Succeeded());
    EXPECT_EQ(old[1], glm::vec3(9,9,9));
}
TEST(ScalarGradientOperations, ConfigCanonicalRoundTripAndRejection)
{
    const auto registration = R::MakeScalarGradientConfigSectionRegistration();
    R::ScalarGradientConfig c;
    c.Scalar.Name = "arbitrary scalar";
    c.Scalar.ValueKind = Geometry::PropertyValueKind::Float;
    c.Output.Name = "custom_gradient";
    const auto serialized = R::SerializeScalarGradientConfig(c);
    const auto validated = registration.Validate(serialized, {}, R::kScalarGradientConfigSectionName);
    ASSERT_TRUE(validated.Usable());
    EXPECT_EQ(validated.CanonicalPayloadJson, serialized);
    c.Output.Domain = R::GeometryElementDomain::MeshVertex;
    EXPECT_FALSE(registration.Validate(R::SerializeScalarGradientConfig(c), {}, "gradient").Usable());
    EXPECT_FALSE(registration.Validate("{\"typo\":1}", {}, "gradient").Usable());
}

TEST(ScalarGradientOperations, RejectsLossyIntegerConversionAndConstantFieldIsZero)
{
    GradientHarness h;
    (void)h.Vertices().GetOrAdd<std::uint64_t>("large", 9007199254740993ull);
    h.Config.Scalar = {R::GeometryElementDomain::MeshVertex, "large", Geometry::PropertyValueKind::UInt64};
    EXPECT_FALSE(h.Run().Succeeded());
    h.Config.Scalar = {R::GeometryElementDomain::MeshVertex, "temperature", Geometry::PropertyValueKind::Double};
    h.Vertices().Get<double>("temperature").Vector().assign(4, 7.);
    ASSERT_TRUE(h.Run().Succeeded());
    for (const auto g : h.Faces().Get<glm::vec3>("f:scalar_gradient").Vector()) EXPECT_EQ(g, glm::vec3(0));
}

TEST(ScalarGradientOperations, RejectsMalformedOutputCardinality)
{
    GradientHarness h;
    h.Faces().GetOrAdd<glm::vec3>("f:scalar_gradient", {}).Vector().resize(1);
    EXPECT_FALSE(h.Run().Succeeded());
    EXPECT_EQ(h.History.UndoCount(), 0u);
}
