// Config, source binding, publication, and undo coverage for mesh geodesics.
#include <array>
#include <cmath>
#include <string>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.VisualizationRecipes;
import Geometry.HalfedgeMesh;
import Geometry.Properties;
namespace Runtime = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Config = Extrinsic::Core::Config;
namespace
{
    struct Harness
    {
        Extrinsic::ECS::Scene::Registry Scene;
        Runtime::EditorCommandHistory History;
        Extrinsic::ECS::EntityHandle Entity;
        Runtime::EditorProcessingContext Context;
        Runtime::EditorGeodesicsCommand Command;
        explicit Harness(bool isolatedVertex = false)
        {
            Geometry::HalfedgeMesh::Mesh mesh;
            auto a = mesh.AddVertex({0, 0, 0}), b = mesh.AddVertex({1, 0, 0});
            auto c = mesh.AddVertex({1, 1, 0}), d = mesh.AddVertex({0, 1, 0});
            EXPECT_TRUE(mesh.AddTriangle(a, b, c));
            EXPECT_TRUE(mesh.AddTriangle(a, c, d));
            if (isolatedVertex)
                (void)mesh.AddVertex({3, 3, 0});
            Entity = Scene.Create();
            GS::PopulateFromMesh(Scene.Raw(), Entity, mesh);
            Context.Scene = &Scene;
            Context.CommandHistory = &History;
            Command.StableEntityId = Runtime::SelectionController::ToStableEntityId(Entity);
            Command.Config.SourceVertices = {0};
        }
        Geometry::PropertySet& Properties()
        {
            return Scene.Raw().get<GS::Vertices>(Entity).Properties;
        }
        // Rebound per call so a test that mutates the context after setup gets
        // the handle its current dependencies describe.
        [[nodiscard]] Runtime::EditorProcessingCommands Commands() const
        {
            return Runtime::BindEditorProcessingCommands(Context);
        }
    };
} // namespace
TEST(GeodesicsOperations, PublishesWithoutChangingMeshAndSupportsUndoRedo)
{
    Harness h;
    auto unrelated = h.Properties().GetOrAdd<float>("v:unrelated", 7.f);
    const auto before = h.Properties().Get<glm::vec3>("v:position").Vector();
    const auto result = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.BackendId, "cpu_reference");
    EXPECT_EQ(h.Properties().Get<glm::vec3>("v:position").Vector(), before);
    auto distance = h.Properties().Get<double>("v:geodesic_distance");
    ASSERT_TRUE(distance);
    EXPECT_NEAR(distance.Vector()[2], std::sqrt(2.), 1e-6);
    EXPECT_EQ(unrelated.Vector(), std::vector<float>(4, 7.f));
    EXPECT_EQ(Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command).Status,
              Runtime::EditorCommandStatus::NoChange);
    EXPECT_EQ(h.History.UndoCount(), 1);
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_FALSE(h.Properties().Exists("v:geodesic_distance"));
    EXPECT_FALSE(h.Properties().Exists("v:is_geodesic_source"));
    ASSERT_TRUE(h.History.Redo().Succeeded());
    EXPECT_TRUE(h.Properties().Exists("v:geodesic_distance"));
}
TEST(GeodesicsOperations, AcceptsAlternateVertexPositionsAndRejectsWrongTypes)
{
    Harness h;
    auto alternate = h.Properties().GetOrAdd<glm::vec3>("v:rest", {});
    alternate.Vector() = h.Properties().Get<glm::vec3>("v:position").Vector();
    for (auto& p : alternate.Vector())
        p *= 2;
    h.Command.Config.PositionProperty = "v:rest";
    auto result = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_NEAR(result.Diagnostics.Distances[2], std::sqrt(8.), 1e-6);
    h.Command.Config.SourceVertices = {999};
    EXPECT_FALSE(Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command).Succeeded());
    EXPECT_EQ(h.History.UndoCount(), 1);
    auto distance = h.Properties().Get<double>("v:geodesic_distance");
    h.Properties().Remove(distance);
    (void)h.Properties().GetOrAdd<float>("v:geodesic_distance", 9.f);
    h.Command.Config.SourceVertices = {0};
    EXPECT_FALSE(Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command).Succeeded());
    EXPECT_EQ(h.Properties().Get<float>("v:geodesic_distance").Vector()[0], 9.f);
}
TEST(GeodesicsOperations, SourceDiagnosticsNameTheOperationAndBoundPositionProperty)
{
    for (const bool malformedDeletionMask : {false, true})
    {
        SCOPED_TRACE(malformedDeletionMask);
        Harness h;
        h.Command.Config.PositionProperty = "v:rest";
        if (malformedDeletionMask)
        {
            auto positions = h.Properties().GetOrAdd<glm::vec3>("v:rest", {});
            positions.Vector() = h.Properties().Get<glm::vec3>("v:position").Vector();
            auto deleted = h.Properties().GetOrAdd<bool>("v:deleted", false);
            deleted.Vector().pop_back();
        }
        const auto result = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
        EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
        EXPECT_TRUE(result.Message.starts_with("Geodesics")) << result.Message;
        EXPECT_EQ(result.Message.find("denoise"), std::string::npos) << result.Message;
        EXPECT_NE(result.Message.find("v:rest"), std::string::npos) << result.Message;
        if (malformedDeletionMask)
            EXPECT_NE(result.Message.find("v:deleted"), std::string::npos) << result.Message;
        EXPECT_EQ(h.History.UndoCount(), 0u);
        EXPECT_FALSE(h.Properties().Exists("v:geodesic_distance"));
        EXPECT_FALSE(h.Properties().Exists("v:is_geodesic_source"));
    }
}

TEST(GeodesicsOperations, ConfigRoundTripsAndRejectsInvalidPayloads)
{
    Config::EngineConfig engine;
    Runtime::GeodesicsConfig config{{0, 3}, 5000, "v:rest"};
    Runtime::SetGeodesicsConfig(engine, config);
    auto decoded = Runtime::GetGeodesicsConfig(engine);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->SourceVertices, config.SourceVertices);
    EXPECT_EQ(decoded->MaxHalfedgeExpansions, 5000);
    EXPECT_EQ(decoded->PositionProperty, "v:rest");
    for (const auto payload : {R"({"source_vertices":[-1]})", R"({"source_vertices":[1.5]})",
                               R"({"max_halfedge_expansions":0})",
                               R"({"position_property":"f:centroid"})", R"({"backend":"gpu"})"})
        EXPECT_FALSE(Runtime::ValidateGeodesicsConfigSection(payload, {}, "geodesics").Usable())
            << payload;
}
TEST(GeodesicsOperations, UndoRejectsChangedGeometry)
{
    Harness h;
    ASSERT_TRUE(Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command).Succeeded());
    h.Properties().Get<glm::vec3>("v:position").Vector()[0].x = 3;
    EXPECT_FALSE(h.History.Undo().Succeeded());
    EXPECT_TRUE(h.Properties().Exists("v:geodesic_distance"));
}

TEST(GeodesicsOperations, ExhaustedBudgetPreservesExistingOutput)
{
    Harness h;
    ASSERT_TRUE(Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command).Succeeded());
    const auto distances = h.Properties().Get<double>("v:geodesic_distance").Vector();
    const auto sources = h.Properties().Get<bool>("v:is_geodesic_source").Vector();
    h.Command.Config.SourceVertices = {1};
    h.Command.Config.MaxHalfedgeExpansions = 1;
    const auto result = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Diagnostics.Status, Geometry::Geodesic::VirtualSourceStatus::ExpansionLimit);
    EXPECT_TRUE(result.Diagnostics.Distances.empty());
    EXPECT_EQ(h.Properties().Get<double>("v:geodesic_distance").Vector(), distances);
    EXPECT_EQ(h.Properties().Get<bool>("v:is_geodesic_source").Vector(), sources);
    EXPECT_EQ(h.History.UndoCount(), 1);
}

TEST(GeodesicsOperations, RejectsMalformedVertexSourcesBeforePublishing)
{
    Harness h;
    auto positions = h.Properties().Get<glm::vec3>("v:position");
    positions.Vector().push_back({2, 2, 0});
    const auto malformed = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
    EXPECT_EQ(malformed.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(malformed.Message,
              "Geodesics: selected mesh requires a count-matched vertex position property: v:position");
    EXPECT_FALSE(h.Properties().Exists("v:geodesic_distance"));
    EXPECT_FALSE(h.Properties().Exists("v:is_geodesic_source"));
    positions.Vector().pop_back();
    auto deleted = h.Properties().GetOrAdd<bool>("v:deleted", false);
    deleted.Vector()[2] = true;
    EXPECT_FALSE(Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command).Succeeded());
    EXPECT_EQ(h.History.UndoCount(), 0);
    EXPECT_FALSE(h.Properties().Exists("v:geodesic_distance"));
}

TEST(GeodesicsOperations, UndoRejectsChangedDeletionState)
{
    Harness h;
    ASSERT_TRUE(Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command).Succeeded());
    auto deleted = h.Properties().GetOrAdd<bool>("v:deleted", false);
    deleted.Vector()[2] = true;
    EXPECT_FALSE(h.History.Undo().Succeeded());
    EXPECT_TRUE(h.Properties().Exists("v:geodesic_distance"));
}

TEST(GeodesicsOperations, UsesSharedPreviewApplyAndConfiguredCommand)
{
    Harness h;
    Config::EngineConfigSectionRegistry registry;
    ASSERT_TRUE(registry.Register(Runtime::MakeGeodesicsConfigSectionRegistration()));
    Runtime::RuntimeEngineConfigControlState state;
    Config::PopulateEngineConfigSectionDefaults(state.ActiveConfig, registry);
    h.Context.EngineConfigControlState = &state;
    h.Context.EngineConfigCommandsAvailable = true;
    int previews = 0, applies = 0;
    h.Context.PreviewEngineConfigDocument = [&](const std::string& document,
                                                const std::string& source) {
        ++previews;
        return Config::PreviewEngineConfig(document, state.ActiveConfig, {source, &registry});
    };
    h.Context.ApplyEngineConfigHotSubset = [&](const Config::EngineConfigLoadResult& preview) {
        ++applies;
        state.ActiveConfig = preview.Preview.Config;
        return Runtime::RuntimeEngineConfigApplyResult{
            .Status = Runtime::RuntimeEngineConfigApplyStatus::Applied};
    };
    const auto commands = h.Commands();
    ASSERT_TRUE(Runtime::ApplyEditorGeodesicsConfig(commands, h.Command.Config).Succeeded());
    EXPECT_EQ(previews, 1);
    EXPECT_EQ(applies, 1);
    const auto computed =
        Runtime::ApplyEditorConfiguredGeodesicsCommand(commands, h.Command.StableEntityId);
    ASSERT_TRUE(computed.Succeeded()) << computed.Message;
    EXPECT_EQ(computed.Diagnostics.SourceCount, 1);
    h.Command.Config.MaxHalfedgeExpansions = 0;
    EXPECT_FALSE(Runtime::ApplyEditorGeodesicsConfig(commands, h.Command.Config).Succeeded());
    EXPECT_EQ(applies, 1);
}

TEST(GeodesicsOperations, CustomOutputBindingsRoundTripAndUndoWithoutTouchingDefaults)
{
    Harness h;
    h.Command.Config.DistanceProperty = "v:distance_custom";
    h.Command.Config.SourceMaskProperty = "v:sources_custom";
    Config::EngineConfig engine;
    Runtime::SetGeodesicsConfig(engine, h.Command.Config);
    const auto decoded = Runtime::GetGeodesicsConfig(engine);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->DistanceProperty, h.Command.Config.DistanceProperty);
    EXPECT_EQ(decoded->SourceMaskProperty, h.Command.Config.SourceMaskProperty);
    ASSERT_TRUE(Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command).Succeeded());
    EXPECT_NEAR(h.Properties().Get<double>(decoded->DistanceProperty).Vector()[2], std::sqrt(2.), 1e-6);
    EXPECT_TRUE(h.Properties().Get<bool>(decoded->SourceMaskProperty).Vector()[0]);
    EXPECT_FALSE(h.Properties().Exists("v:geodesic_distance"));
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_FALSE(h.Properties().Exists(decoded->DistanceProperty));
    ASSERT_TRUE(h.History.Redo().Succeeded());
    EXPECT_TRUE(h.Properties().Exists(decoded->DistanceProperty));
    auto invalid = *decoded;
    invalid.DistanceProperty = invalid.SourceMaskProperty;
    EXPECT_FALSE(Runtime::ValidateGeodesicsConfigSection(
        Runtime::SerializeGeodesicsConfig(invalid), {}, "geodesics").Usable());
}

TEST(GeodesicsOperations, UnreachableVertexDoesNotHideReachableDistanceField)
{
    Harness h(true);
    const auto result = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    ASSERT_EQ(result.Diagnostics.UnreachableVertexCount, 1u);
    const auto distance = h.Properties().Get<double>(h.Command.Config.DistanceProperty);
    ASSERT_TRUE(distance);
    ASSERT_TRUE(std::isinf(distance.Vector().back()));
    const auto encoded = Runtime::EncodeVisualizationRecipe(
        Runtime::BuildGeometryAvailability(h.Scene.Raw(), h.Entity),
        {.Data = Runtime::ScalarVisualizationRecipe{
            .Source = {Runtime::GeometryElementDomain::MeshVertex,
                       h.Command.Config.DistanceProperty, Geometry::PropertyValueKind::Double}}});
    EXPECT_TRUE(encoded.Succeeded()) << static_cast<int>(encoded.Status);
    ASSERT_EQ(encoded.Batch.Scalars.size(), 1u);
    EXPECT_EQ(encoded.Batch.Scalars.front().ElementCount, 5u);
    EXPECT_FLOAT_EQ(encoded.Batch.Scalars.front().RangeMin, 0.0f);
    EXPECT_NEAR(encoded.Batch.Scalars.front().RangeMax, std::sqrt(2.0), 1e-6);
}
