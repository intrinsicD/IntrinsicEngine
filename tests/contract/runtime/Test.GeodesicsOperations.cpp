// Config, source binding, publication, and undo coverage for mesh geodesics.
#include <array>
#include <cmath>
#include <limits>
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
    auto alternate = h.Properties().GetOrAdd<glm::vec3>("rest_samples", {});
    alternate.Vector() = h.Properties().Get<glm::vec3>("v:position").Vector();
    for (auto& p : alternate.Vector())
        p *= 2;
    h.Command.Config.PositionProperty.Name = "rest_samples";
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
        h.Command.Config.PositionProperty.Name = "v:rest";
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
    Runtime::GeodesicsConfig config;
    config.SourceVertices = {0, 3};
    config.MaxHalfedgeExpansions = 5000;
    config.PositionProperty.Name = "rest_samples";
    config.DistanceProperty.Name = "distance";
    config.SourceMaskProperty.Name = "sources";
    Runtime::SetGeodesicsConfig(engine, config);
    auto decoded = Runtime::GetGeodesicsConfig(engine);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->SourceVertices, config.SourceVertices);
    EXPECT_EQ(decoded->MaxHalfedgeExpansions, 5000);
    EXPECT_EQ(decoded->PositionProperty.Name, "rest_samples");
    EXPECT_EQ(decoded->DistanceProperty.Name, "distance");
    EXPECT_EQ(decoded->SourceMaskProperty.Name, "sources");
    EXPECT_EQ(decoded->PositionProperty, config.PositionProperty);
    EXPECT_EQ(decoded->DistanceProperty, config.DistanceProperty);
    EXPECT_EQ(decoded->SourceMaskProperty, config.SourceMaskProperty);
    const auto* section = Config::FindEngineConfigSection(
        engine.AppSections, Runtime::kGeodesicsConfigSectionName);
    ASSERT_NE(section, nullptr);
    EXPECT_EQ(section->SchemaVersion, 2u);
    for (const auto payload : {R"({"source_vertices":[-1]})", R"({"source_vertices":[1.5]})",
                               R"({"max_halfedge_expansions":0})",
                               R"({"position_property":""})", R"({"backend":"gpu"})"})
        EXPECT_FALSE(Runtime::ValidateGeodesicsConfigSection(payload, {}, "geodesics").Usable())
            << payload;
}

TEST(GeodesicsOperations, RejectsWrongBindingDomainsAndKindsBeforeMutation)
{
    for (auto member : {&Runtime::GeodesicsConfig::PositionProperty,
                        &Runtime::GeodesicsConfig::DistanceProperty,
                        &Runtime::GeodesicsConfig::SourceMaskProperty})
    {
        for (const bool wrongDomain : {false, true})
        {
            Harness h;
            auto& ref = h.Command.Config.*member;
            if (wrongDomain)
                ref.Domain = Runtime::GeometryElementDomain::MeshFace;
            else
                ref.ValueKind = Geometry::PropertyValueKind::Vec4;
            const auto result = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
            EXPECT_EQ(result.Status, Runtime::EditorCommandStatus::InvalidProcessingParameters);
            EXPECT_EQ(h.History.UndoCount(), 0u);
            EXPECT_FALSE(h.Properties().Exists("v:geodesic_distance"));
            EXPECT_FALSE(h.Properties().Exists("v:is_geodesic_source"));
        }
    }
}

TEST(GeodesicsOperations, RejectsIncompleteAndLegacyPropertyReferences)
{
    for (const auto payload : {
             R"({"position_property":"v:position"})",
             R"({"distance_property":{"name":"distance","kind":"double"}})",
             R"({"source_mask_property":{"domain":"mesh_vertex","name":"sources"}})"})
        EXPECT_FALSE(Runtime::ValidateGeodesicsConfigSection(payload, {}, "geodesics").Usable());
    Config::EngineConfig engine;
    Runtime::SetGeodesicsConfig(engine, {});
    auto* section = Config::FindEngineConfigSection(engine.AppSections, Runtime::kGeodesicsConfigSectionName);
    ASSERT_NE(section, nullptr);
    // A schema-1 name-only section must not be interpreted as canonical references.
    auto legacy = *section;
    legacy.SchemaVersion = 1u;
    Config::UpsertEngineConfigSection(engine.AppSections, legacy);
    EXPECT_FALSE(Runtime::GetGeodesicsConfig(engine));
}

TEST(GeodesicsOperations, RejectsStructuralOutputStorageWithoutUsingNamePrefixes)
{
    Runtime::GeodesicsConfig config;
    config.SourceVertices = {0};
    config.PositionProperty.Name = "rest_samples";
    config.DistanceProperty.Name = "distance";
    config.SourceMaskProperty.Name = "sources";
    EXPECT_TRUE(Runtime::ValidateGeodesicsConfigSection(
        Runtime::SerializeGeodesicsConfig(config), {}, "geodesics").Usable());
    for (const std::string structural :
         {"v:position", "v:deleted", "v:connectivity", "v:halfedge"})
    {
        SCOPED_TRACE(structural);
        config.DistanceProperty.Name = structural;
        EXPECT_FALSE(Runtime::ValidateGeodesicsConfigSection(
            Runtime::SerializeGeodesicsConfig(config), {}, "geodesics").Usable());
    }
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
    h.Command.Config.DistanceProperty.Name = "distance_custom";
    h.Command.Config.SourceMaskProperty.Name = "sources_custom";
    Config::EngineConfig engine;
    Runtime::SetGeodesicsConfig(engine, h.Command.Config);
    const auto decoded = Runtime::GetGeodesicsConfig(engine);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->DistanceProperty.Name, h.Command.Config.DistanceProperty.Name);
    EXPECT_EQ(decoded->SourceMaskProperty.Name, h.Command.Config.SourceMaskProperty.Name);
    ASSERT_TRUE(Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command).Succeeded());
    EXPECT_NEAR(h.Properties().Get<double>(decoded->DistanceProperty.Name).Vector()[2], std::sqrt(2.), 1e-6);
    EXPECT_TRUE(h.Properties().Get<bool>(decoded->SourceMaskProperty.Name).Vector()[0]);
    EXPECT_FALSE(h.Properties().Exists("v:geodesic_distance"));
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_FALSE(h.Properties().Exists(decoded->DistanceProperty.Name));
    ASSERT_TRUE(h.History.Redo().Succeeded());
    EXPECT_TRUE(h.Properties().Exists(decoded->DistanceProperty.Name));
    auto invalid = *decoded;
    invalid.DistanceProperty.Name = invalid.SourceMaskProperty.Name;
    EXPECT_FALSE(Runtime::ValidateGeodesicsConfigSection(
        Runtime::SerializeGeodesicsConfig(invalid), {}, "geodesics").Usable());
}

TEST(GeodesicsOperations, UnreachableVertexDoesNotHideReachableDistanceField)
{
    Harness h(true);
    const auto result = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    ASSERT_EQ(result.Diagnostics.UnreachableVertexCount, 1u);
    const auto distance = h.Properties().Get<double>(h.Command.Config.DistanceProperty.Name);
    ASSERT_TRUE(distance);
    ASSERT_TRUE(std::isinf(distance.Vector().back()));
    const auto encoded = Runtime::EncodeVisualizationRecipe(
        Runtime::BuildGeometryAvailability(h.Scene.Raw(), h.Entity),
        {.Data = Runtime::ScalarVisualizationRecipe{
            .Source = h.Command.Config.DistanceProperty}});
    EXPECT_TRUE(encoded.Succeeded()) << static_cast<int>(encoded.Status);
    ASSERT_EQ(encoded.Batch.Scalars.size(), 1u);
    EXPECT_EQ(encoded.Batch.Scalars.front().ElementCount, 5u);
    EXPECT_FLOAT_EQ(encoded.Batch.Scalars.front().RangeMin, 0.0f);
    EXPECT_NEAR(encoded.Batch.Scalars.front().RangeMax, std::sqrt(2.0), 1e-6);
}

TEST(GeodesicsOperations, ScalarStorageSelectionRoundTripsAndPublishesAtomically)
{
    using K = Geometry::PropertyValueKind;
    for (const auto kind : {K::Bool, K::Int32, K::UInt32, K::UInt64, K::Float, K::Double})
    {
        Harness h;
        h.Command.Config.SourceVertices = {0, 1, 2, 3};
        h.Command.Config.DistanceProperty.ValueKind = kind;
        h.Command.Config.SourceMaskProperty.ValueKind = kind;
        Config::EngineConfig config;
        Runtime::SetGeodesicsConfig(config, h.Command.Config);
        const auto decoded = Runtime::GetGeodesicsConfig(config);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded->DistanceProperty.ValueKind, kind);
        EXPECT_EQ(decoded->SourceMaskProperty.ValueKind, kind);
        const auto result = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        EXPECT_EQ(Runtime::DetectGeometryPropertyValueKind(h.Properties(), h.Command.Config.DistanceProperty.Name), kind);
        EXPECT_EQ(Runtime::DetectGeometryPropertyValueKind(h.Properties(), h.Command.Config.SourceMaskProperty.Name), kind);
        ASSERT_TRUE(h.History.Undo().Succeeded());
        EXPECT_FALSE(h.Properties().Exists(h.Command.Config.DistanceProperty.Name));
        EXPECT_FALSE(h.Properties().Exists(h.Command.Config.SourceMaskProperty.Name));
        ASSERT_TRUE(h.History.Redo().Succeeded());
    }
    for (const auto target : {K::UInt32, K::Float})
    {
    Harness h;
    h.Command.Config.DistanceProperty.ValueKind = target;
    h.Command.Config.SourceMaskProperty.ValueKind = K::Float;
    h.Properties().GetOrAdd<float>(h.Command.Config.SourceMaskProperty.Name).Vector().assign(4, 7.f);
    const auto result = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_FALSE(h.Properties().Exists(h.Command.Config.DistanceProperty.Name));
    EXPECT_EQ(h.Properties().Get<float>(h.Command.Config.SourceMaskProperty.Name).Vector(), std::vector<float>(4, 7.f));
    EXPECT_EQ(h.History.UndoCount(), 0u);
    }
}

TEST(GeodesicsOperations, FloatingOutputsPreserveUnreachableInfinityAndDeletedStorage)
{
    Harness h(true);
    h.Command.Config.SourceVertices = {0, 1, 2, 3};
    h.Command.Config.DistanceProperty.ValueKind = Geometry::PropertyValueKind::Float;
    auto result = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_TRUE(std::isinf(h.Properties().Get<float>(h.Command.Config.DistanceProperty.Name).Vector()[4]));
    ASSERT_TRUE(h.History.Undo().Succeeded());
    auto mask = h.Properties().GetOrAdd<bool>("v:deleted", false);
    mask.Vector()[4] = true;
    auto distance = h.Properties().GetOrAdd<float>(h.Command.Config.DistanceProperty.Name);
    distance.Vector().assign(5, 9.f);
    distance.Vector()[4] = std::numeric_limits<float>::quiet_NaN();
    result = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_TRUE(std::isnan(distance.Vector()[4]));
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_EQ(distance.Vector()[0], 9.f);
    EXPECT_TRUE(std::isnan(distance.Vector()[4]));
    ASSERT_TRUE(h.History.Redo().Succeeded());
}

TEST(GeodesicsOperations, SourceVertexPropertyAddsMarkedVerticesAsSources)
{
    Harness h;
    h.Command.Config.SourceVertices.clear();
    auto marks = h.Properties().GetOrAdd<bool>("v:feature", false);
    marks.Vector()[2] = true;
    h.Command.Config.SourceVertexProperty = {Runtime::GeometryElementDomain::MeshVertex,
                                             "v:feature", Geometry::PropertyValueKind::Bool};
    auto result = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    auto distance = h.Properties().Get<double>("v:geodesic_distance");
    ASSERT_TRUE(distance);
    EXPECT_EQ(distance.Vector()[2], 0.0);
    EXPECT_NEAR(distance.Vector()[0], std::sqrt(2.), 1e-6);
    EXPECT_TRUE(h.Properties().Get<bool>("v:is_geodesic_source").Vector()[2]);

    // Explicit sources and marked vertices combine; float marks use nonzero.
    auto weights = h.Properties().GetOrAdd<float>("v:weights", 0.f);
    weights.Vector()[1] = 0.5f;
    h.Command.Config.SourceVertices = {3};
    h.Command.Config.SourceVertexProperty = {Runtime::GeometryElementDomain::MeshVertex,
                                             "v:weights", Geometry::PropertyValueKind::Float};
    result = Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.Diagnostics.SourceCount, 2u);
    EXPECT_EQ(distance.Vector()[1], 0.0);
    EXPECT_EQ(distance.Vector()[3], 0.0);

    // A missing property or an all-zero one with no explicit source is rejected.
    h.Command.Config.SourceVertexProperty.Name = "v:absent";
    EXPECT_EQ(Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command).Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
    h.Command.Config.SourceVertices.clear();
    h.Properties().GetOrAdd<std::uint32_t>("v:none", 0u);
    h.Command.Config.SourceVertexProperty = {Runtime::GeometryElementDomain::MeshVertex,
                                             "v:none", Geometry::PropertyValueKind::UInt32};
    EXPECT_EQ(Runtime::ApplyEditorGeodesicsCommand(h.Commands(), h.Command).Status,
              Runtime::EditorCommandStatus::InvalidProcessingParameters);
}

TEST(GeodesicsOperations, SourceVertexPropertyRoundTripsThroughConfig)
{
    Config::EngineConfig engine;
    Runtime::GeodesicsConfig config;
    Runtime::SetGeodesicsConfig(engine, config);
    auto decoded = Runtime::GetGeodesicsConfig(engine);
    ASSERT_TRUE(decoded);
    EXPECT_TRUE(decoded->SourceVertexProperty.Name.empty());

    config.SourceVertexProperty = {Runtime::GeometryElementDomain::MeshVertex, "v:feature",
                                   Geometry::PropertyValueKind::UInt32};
    Runtime::SetGeodesicsConfig(engine, config);
    decoded = Runtime::GetGeodesicsConfig(engine);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->SourceVertexProperty, config.SourceVertexProperty);

    for (const auto payload :
         {R"({"source_vertex_property":{"domain":"mesh_face","name":"f:x","kind":"bool"}})",
          R"({"source_vertex_property":"v:feature"})"})
        EXPECT_FALSE(Runtime::ValidateGeodesicsConfigSection(payload, {}, "geodesics").Usable())
            << payload;
    auto collide = config;
    collide.SourceVertexProperty.Name = collide.DistanceProperty.Name;
    EXPECT_FALSE(Runtime::ValidateGeodesicsConfigSection(
                     Runtime::SerializeGeodesicsConfig(collide), {}, "geodesics")
                     .Usable());
}
