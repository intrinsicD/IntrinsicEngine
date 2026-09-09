#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <variant>
#include <gtest/gtest.h>
#include "SandboxEditorJobHarness.hpp"

import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.VisualizationRecipes;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Geometry.HalfedgeMesh;
import Geometry.Graph;

namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
using D = R::GeometryElementDomain;
namespace
{
    constexpr std::array<glm::vec3, 4> plane{{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {2, 3, 0}}};
    Geometry::PropertySet &Properties(Extrinsic::ECS::Scene::Registry &scene, entt::entity entity, D domain)
    {
        return *const_cast<Geometry::PropertySet *>(
            R::ResolveGeometryPropertySet(R::BuildGeometryAvailability(scene.Raw(), entity), domain));
    }
    entt::entity Make(Extrinsic::ECS::Scene::Registry &scene, D domain)
    {
        auto entity = scene.Create();
        if (domain >= D::MeshVertex && domain <= D::MeshFace)
        {
            Geometry::HalfedgeMesh::Mesh mesh;
            auto a = mesh.AddVertex(plane[0]), b = mesh.AddVertex(plane[1]), c = mesh.AddVertex(plane[2]),
                 d = mesh.AddVertex(plane[3]);
            (void)mesh.AddTriangle(a, b, c);
            (void)mesh.AddTriangle(c, b, d);
            GS::PopulateFromMesh(scene.Raw(), entity, mesh);
            if (domain == D::MeshFace)
                scene.Raw().get<GS::Faces>(entity).Properties.Resize(4);
        }
        else if (domain != D::PointCloudPoint)
        {
            Geometry::Graph::Graph graph;
            auto a = graph.AddVertex(plane[0]), b = graph.AddVertex(plane[1]), c = graph.AddVertex(plane[2]),
                 d = graph.AddVertex(plane[3]);
            (void)graph.AddEdge(a, b);
            (void)graph.AddEdge(a, c);
            (void)graph.AddEdge(a, d);
            (void)graph.AddEdge(b, c);
            (void)graph.AddEdge(b, d);
            (void)graph.AddEdge(c, d);
            GS::PopulateFromGraph(scene.Raw(), entity, graph);
        }
        else
            scene.Raw().emplace<GS::Vertices>(entity).Properties.Resize(5);
        auto &props = Properties(scene, entity, domain);
        auto samples = props.GetOrAdd<glm::vec3>("samples");
        for (std::size_t i = 0; i < samples.Size(); ++i)
            samples[i] = plane[i % 4];
        auto keep = props.GetOrAdd<float>("keep");
        std::ranges::fill(keep.Vector(), 42.f);
        if (domain == D::PointCloudPoint)
        {
            props.GetOrAdd<bool>("v:deleted")[4] = true;
            samples[4] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        }
        return entity;
    }
    R::NormalEstimationConfig Config(entt::entity entity, D domain)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Positions = {.Domain = domain,
                              .Name = "samples",
                              .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Output = {
                    .Domain = domain, .Name = "estimated", .ValueKind = Geometry::PropertyValueKind::Vec3}};
    }
} // namespace

TEST(NormalEstimation, EveryCanonicalDomainPublishesNamedNormalsAndSupportsUndoRedo)
{
    for (unsigned d = 1; d <= 8; ++d)
        for (auto backend : {R::NormalEstimationBackend::CpuKDTree, R::NormalEstimationBackend::CpuLBVH})
        {
            SCOPED_TRACE(std::to_string(d) + R::ToString(backend));
            R::WorldRegistry worlds;
            auto world = worlds.CreateWorld("normals");
            auto &scene = *worlds.Get(world);
            R::SpatialIndexCache cache(worlds);
            const auto entity = Make(scene, D(d));
            auto config = Config(entity, D(d));
            config.Backend = backend;
            auto &props = Properties(scene, entity, D(d));
            const auto slots = props.Size();
            R::EditorCommandHistory history;
            R::EditorGeometryProcessingContext context{
                .Scene = &scene, .World = world, .CommandHistory = &history, .SpatialIndices = &cache};
            auto catalog = R::GetEditorNormalEstimationInputCatalog(context, config.StableEntityId);
            ASSERT_TRUE(std::ranges::any_of(catalog.Entries,
                                            [&](const auto &e) { return e.Ref == config.Positions; }));
            const auto inputRevision = std::as_const(props).Get<glm::vec3>("samples").Revision();
            ASSERT_TRUE(R::PreviewEditorNormalEstimationCommand(context, config).Ready);
            EXPECT_FALSE(props.Exists("estimated"));
            const auto result = R::ApplyEditorNormalEstimationCommand(context, config);
            ASSERT_EQ(result.Status, R::EditorCommandStatus::Applied) << result.Message;
            EXPECT_EQ(result.ActualBackend, R::ToString(backend));
            EXPECT_EQ(result.Output, config.Output);
            EXPECT_EQ(result.ValidCount, result.LiveCount);
            EXPECT_EQ(result.FallbackCount, 0);
            EXPECT_EQ(result.WrittenCount, result.LiveCount);
            EXPECT_EQ(props.Size(), slots);
            const auto normal = std::as_const(props).Get<glm::vec3>("estimated");
            for (std::size_t i = 0; i < result.LiveCount; ++i)
            {
                EXPECT_NEAR(normal[i].z, 1.f, 1e-5);
                EXPECT_NEAR(glm::length(normal[i]), 1.f, 1e-5);
            }
            if (D(d) == D::PointCloudPoint)
                EXPECT_EQ(normal[4], glm::vec3(0));
            EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(), inputRevision);
            EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 42.f);
            ASSERT_TRUE(history.Undo().Succeeded());
            EXPECT_FALSE(props.Exists("estimated"));
            ASSERT_TRUE(history.Redo().Succeeded());
            EXPECT_TRUE(props.Exists("estimated"));
            auto repeated = R::ApplyEditorNormalEstimationCommand(context, config);
            EXPECT_EQ(repeated.Status, R::EditorCommandStatus::NoChange);
            EXPECT_EQ(repeated.IndexReused, backend == R::NormalEstimationBackend::CpuLBVH);
        }
}

TEST(NormalEstimation, PreservesDeletedRowsAndUnrelatedEditsButGuardsOutputHistory)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = Make(scene, D::PointCloudPoint);
    auto &props = Properties(scene, entity, D::PointCloudPoint);
    std::ranges::fill(props.GetOrAdd<glm::vec3>("estimated").Vector(), glm::vec3(7, 8, 9));
    props.Get<glm::vec3>("estimated")[4].x = std::numeric_limits<float>::quiet_NaN();
    R::EditorCommandHistory history;
    R::EditorGeometryProcessingContext context{.Scene = &scene, .CommandHistory = &history};
    ASSERT_TRUE(
        R::ApplyEditorNormalEstimationCommand(context, Config(entity, D::PointCloudPoint)).Succeeded());
    EXPECT_TRUE(std::isnan(std::as_const(props).Get<glm::vec3>("estimated")[4].x));
    props.Get<float>("keep")[0] = 99.f;
    ASSERT_TRUE(history.Undo().Succeeded());
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("estimated")[0], glm::vec3(7, 8, 9));
    ASSERT_TRUE(history.Redo().Succeeded());
    EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 99.f);
    props.Get<glm::vec3>("estimated")[0] = glm::vec3(1, 0, 0);
    EXPECT_FALSE(history.Undo().Succeeded());
    EXPECT_EQ(std::as_const(props).Get<glm::vec3>("estimated")[0], glm::vec3(1, 0, 0));
}

TEST(NormalEstimation, TopologyVariantsConsumeCustomPositionsAndGraphMethodAcceptsMesh)
{
    for (auto domain : {D::MeshVertex, D::GraphNode})
        for (auto method :
             {R::NormalEstimationMethod::MeshFaceWeighted, R::NormalEstimationMethod::GraphNeighborhood})
        {
            Extrinsic::ECS::Scene::Registry scene;
            const auto entity = Make(scene, domain);
            auto config = Config(entity, domain);
            config.Method = method;
            R::EditorGeometryProcessingContext context{.Scene = &scene};
            if (domain == D::GraphNode && method == R::NormalEstimationMethod::MeshFaceWeighted)
            {
                EXPECT_FALSE(R::PreviewEditorNormalEstimationCommand(context, config).Ready);
                continue;
            }
            auto result = R::ApplyEditorNormalEstimationCommand(context, config);
            ASSERT_TRUE(result.Succeeded()) << result.Message;
            EXPECT_EQ(result.ValidCount, 4);
            EXPECT_EQ(result.FallbackCount, 0);
            auto normal = std::as_const(Properties(scene, entity, domain)).Get<glm::vec3>("estimated");
            for (std::size_t i = 0; i < normal.Size(); ++i)
                EXPECT_NEAR(std::abs(normal[i].z), 1.f, 1e-5);
            if (method == R::NormalEstimationMethod::MeshFaceWeighted)
                EXPECT_EQ(result.ProcessedFaces, 2);
        }
}

TEST(NormalEstimation, MeshDeletionMasksExcludeFacesAndEdgesWithoutRenumberingOutput)
{
    for (bool edgeDeletion : {false, true})
    {
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, D::MeshVertex);
        auto config = Config(entity, D::MeshVertex);
        config.Method = R::NormalEstimationMethod::MeshFaceWeighted;
        if (edgeDeletion)
            scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<bool>("e:deleted")[0] = true;
        else
        {
            auto &faces = scene.Raw().get<GS::Faces>(entity).Properties;
            faces.GetOrAdd<bool>("f:deleted")[0] = true;
            faces.Get<std::uint32_t>("f:halfedge")[0] = std::numeric_limits<std::uint32_t>::max();
        }
        R::EditorGeometryProcessingContext context{.Scene = &scene};
        const auto result = R::ApplyEditorNormalEstimationCommand(context, config);
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        EXPECT_EQ(result.SlotCount, 4);
        EXPECT_EQ(result.ProcessedFaces, 1);
        EXPECT_EQ(result.FallbackCount, 1);
    }
}

TEST(NormalEstimation, RejectsInvalidBindingsAndUnavailableBackendsWithoutMutation)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = Make(scene, D::PointCloudPoint);
    R::EditorGeometryProcessingContext context{.Scene = &scene};
    const auto base = Config(entity, D::PointCloudPoint);
    auto &props = Properties(scene, entity, D::PointCloudPoint);
    for (unsigned invalid = 0; invalid < 7; ++invalid)
    {
        auto config = base;
        switch (invalid)
        {
        case 0:
            config.Output.Domain = D::MeshFace;
            break;
        case 1:
            config.Output.Name = "samples";
            break;
        case 2:
            config.Output.Name = "keep";
            break;
        case 3:
            config.Backend = R::NormalEstimationBackend::CpuLBVH;
            break;
        case 4:
            config.Method = R::NormalEstimationMethod::GraphNeighborhood;
            break;
        case 5:
            config.Output.Name = "v:deleted";
            break;
        case 6:
            config.Positions.Name = "missing";
            break;
        }
        EXPECT_FALSE(R::PreviewEditorNormalEstimationCommand(context, config).Ready);
        const auto rejected = R::ApplyEditorNormalEstimationCommand(context, config);
        EXPECT_FALSE(rejected.Succeeded());
        EXPECT_EQ(rejected.Method, config.Method);
        EXPECT_EQ(rejected.RequestedBackend, config.Backend);
        EXPECT_TRUE(rejected.ActualBackend.empty());
        EXPECT_FALSE(props.Exists("estimated"));
    }
    props.Get<glm::vec3>("samples")[0].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(R::PreviewEditorNormalEstimationCommand(context, base).Ready);
    EXPECT_FALSE(props.Exists("estimated"));
}

TEST(NormalEstimation, RadiusNeighborhoodAndDegenerateFallbackMatchAcrossBackends)
{
    R::WorldRegistry worlds;
    auto world = worlds.CreateWorld("normals");
    auto &scene = *worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    auto entity = Make(scene, D::PointCloudPoint);
    R::EditorGeometryProcessingContext context{.Scene = &scene, .World = world, .SpatialIndices = &cache};
    auto config = Config(entity, D::PointCloudPoint);
    config.UseRadiusSearch = true;
    config.Radius = 10;
    auto reference = R::ApplyEditorNormalEstimationCommand(context, config);
    ASSERT_TRUE(reference.Succeeded());
    config.Backend = R::NormalEstimationBackend::CpuLBVH;
    auto accelerated = R::ApplyEditorNormalEstimationCommand(context, config);
    ASSERT_TRUE(accelerated.Succeeded());
    EXPECT_EQ(accelerated.ChangedCount, 0);
    EXPECT_EQ(accelerated.ValidCount, reference.ValidCount);
    config.Radius = .01f;
    config.FallbackNormal = {1, 0, 0};
    auto fallback = R::ApplyEditorNormalEstimationCommand(context, config);
    ASSERT_TRUE(fallback.Succeeded());
    EXPECT_EQ(fallback.FallbackCount, 4);
    EXPECT_EQ(fallback.ValidCount, 0);
}

TEST(NormalEstimation, QueuedJobsGuardInputsTopologyDeletionAndOutputButAllowUnrelatedEdits)
{
    for (unsigned change = 0; change < 6; ++change)
    {
        SCOPED_TRACE(change);
        Extrinsic::ECS::Scene::Registry scene;
        auto entity = Make(scene, D::MeshVertex);
        auto config = Config(entity, D::MeshVertex);
        config.Method = R::NormalEstimationMethod::GraphNeighborhood;
        auto &props = Properties(scene, entity, D::MeshVertex);
        Intrinsic::Tests::EditorFeatureTestContext context;
        context.Scene = &scene;
        R::EditorCommandHistory history;
        context.CommandHistory = &history;
        std::optional<R::EditorNormalEstimationResult> delivered;
        context.MethodResultSinks.NormalEstimation = [&](auto r) { delivered = std::move(r); };
        Extrinsic::Tests::EditorJobHarness jobs;
        jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorNormalEstimationCommand(context, config).Status,
                  R::EditorCommandStatus::Pending);
        EXPECT_EQ(R::ApplyEditorNormalEstimationCommand(context, config).Status,
                  R::EditorCommandStatus::Pending);
        EXPECT_EQ(jobs.Snapshot().Entries.size(), 1);
        switch (change)
        {
        case 0:
            props.Get<float>("keep")[0] = 99.f;
            break;
        case 1:
            props.Get<glm::vec3>("samples")[0] = plane[0];
            break;
        case 2:
            props.GetOrAdd<bool>("v:deleted")[0] = true;
            break;
        case 3:
            props.GetOrAdd<glm::vec3>("estimated")[0] = {1, 0, 0};
            break;
        case 4:
            scene.Raw().get<GS::Edges>(entity).Properties.Get<std::uint32_t>("e:v0")[0] = 0;
            break;
        case 5:
            (void)jobs.Jobs().Cancel(jobs.Snapshot().Entries[0].Token);
            break;
        }
        ASSERT_TRUE(jobs.DrainUntilTerminal());
        ASSERT_TRUE(delivered);
        if (change == 0)
        {
            EXPECT_TRUE(delivered->Succeeded()) << delivered->Message;
            EXPECT_TRUE(history.CanUndo());
            EXPECT_TRUE(props.Exists("estimated"));
        }
        else
        {
            EXPECT_FALSE(delivered->Succeeded());
            EXPECT_FALSE(history.CanUndo());
            EXPECT_EQ(props.Exists("estimated"), change == 3);
        }
    }
}

TEST(NormalEstimationConfig, RoundTripAndSharedPreviewApplyRun)
{
    namespace C = Extrinsic::Core::Config;
    Extrinsic::ECS::Scene::Registry scene;
    auto entity = Make(scene, D::MeshFace);
    auto config = Config(entity, D::MeshFace);
    config.UseRadiusSearch = true;
    config.Radius = 5;
    C::EngineConfigSectionRegistry registry;
    ASSERT_TRUE(registry.Register(R::MakeNormalEstimationConfigSectionRegistration()));
    R::RuntimeEngineConfigControlState state;
    C::PopulateEngineConfigSectionDefaults(state.ActiveConfig, registry);
    R::EditorGeometryProcessingContext context{.Scene = &scene};
    context.EngineConfigControlState = &state;
    context.EngineConfigCommandsAvailable = true;
    unsigned previews = 0, applies = 0;
    context.PreviewEngineConfigDocument = [&](const auto &document, const auto &origin) {
        ++previews;
        return C::PreviewEngineConfig(document, state.ActiveConfig, {origin, &registry});
    };
    context.ApplyEngineConfigHotSubset = [&](const auto &preview) {
        ++applies;
        state.ActiveConfig = preview.Preview.Config;
        return R::RuntimeEngineConfigApplyResult{.Status = R::RuntimeEngineConfigApplyStatus::Applied};
    };
    auto commands = R::BindEditorGeometryProcessingCommands(context);
    ASSERT_TRUE(R::PreviewEditorNormalEstimationCommand(commands, config).Ready);
    EXPECT_FALSE(Properties(scene, entity, D::MeshFace).Exists("estimated"));
    ASSERT_TRUE(R::ApplyEditorNormalEstimationConfig(commands, config).Succeeded());
    ASSERT_TRUE(R::GetEditorNormalEstimationConfig(commands));
    EXPECT_EQ(R::SerializeNormalEstimationConfig(*R::GetEditorNormalEstimationConfig(commands)),
              R::SerializeNormalEstimationConfig(config));
    ASSERT_TRUE(R::ApplyEditorConfiguredNormalEstimation(commands).Succeeded());
    EXPECT_EQ(previews, 1);
    EXPECT_EQ(applies, 1);
    for (auto payload : {R"({"method":"automatic"})", R"({"backend":"vulkan"})", R"({"k_neighbors":0})",
                         R"({"minimum_neighbors":-1})", R"({"orientation":2})", R"({"weighting":5})",
                         R"({"use_radius":true,"radius":0})", R"({"radius":1e100})", R"({"unknown":1})",
                         R"({"fallback_normal":[1,2]})"})
        EXPECT_FALSE(R::ValidateNormalEstimationConfigSection(payload, {}, "test").Usable()) << payload;
    config.KNeighbors = 0;
    EXPECT_FALSE(R::ApplyEditorNormalEstimationConfig(commands, config).Succeeded());
    EXPECT_EQ(applies, 1);
}

TEST(NormalEstimation, NamedOutputUsesSharedVectorVisualizationRecipe)
{
    for (unsigned d = 1; d <= 8; ++d)
    {
        Extrinsic::ECS::Scene::Registry scene;
        auto entity = Make(scene, D(d));
        auto config = Config(entity, D(d));
        Intrinsic::Tests::EditorFeatureTestContext context;
        context.Scene = &scene;
        context.VisualizationCommandsAvailable = true;
        std::optional<R::VisualizationRecipe> stored;
        context.VisualizationRecipes.GetRecipe = [&](std::uint32_t) { return stored; };
        context.VisualizationRecipes.SetRecipe = [&](std::uint32_t, R::VisualizationRecipe r) {
            stored = std::move(r);
        };
        context.VisualizationRecipes.ClearRecipe = [&](std::uint32_t) { stored.reset(); };
        ASSERT_TRUE(R::ApplyEditorNormalEstimationCommand(context, config).Succeeded());
        EXPECT_EQ(
            R::ApplyEditorVisualizationRecipeCommand(
                context,
                {.StableEntityId = config.StableEntityId,
                 .Recipe = {.Data = R::VectorFieldVisualizationRecipe{.Source = config.Output,
                                                                      .PositionSource = config.Positions,
                                                                      .OutputName = "normal_vectors"}}}),
            R::EditorCommandStatus::Applied);
        ASSERT_TRUE(stored);
        const auto *recipe = std::get_if<R::VectorFieldVisualizationRecipe>(&stored->Data);
        ASSERT_NE(recipe, nullptr);
        EXPECT_EQ(recipe->Source, config.Output);
        EXPECT_EQ(recipe->PositionSource, config.Positions);
    }
}

TEST(NormalEstimation, TopologyCatalogRetainsSmallGraphsAndReportsPcaMinimum)
{
    Extrinsic::ECS::Scene::Registry scene;
    auto entity = Make(scene, D::GraphNode);
    auto &vertices = Properties(scene, entity, D::GraphNode);
    vertices.Resize(2);
    R::EditorGeometryProcessingContext context{.Scene = &scene};
    auto config = Config(entity, D::GraphNode);
    const auto catalog = R::GetEditorNormalEstimationInputCatalog(context, config.StableEntityId);
    EXPECT_TRUE(
        std::ranges::any_of(catalog.Entries, [&](const auto &e) { return e.Ref == config.Positions; }));
    EXPECT_FALSE(R::PreviewEditorNormalEstimationCommand(context, config).Ready);
    config.Method = R::NormalEstimationMethod::GraphNeighborhood;
    EXPECT_TRUE(R::PreviewEditorNormalEstimationCommand(context, config).Ready);
    const auto result = R::ApplyEditorNormalEstimationCommand(context, config);
    EXPECT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_GT(result.InvalidEdges, 0);
}

TEST(NormalEstimation, GraphPositionSlotMayBindAnExistingNormalNamedProperty)
{
    Extrinsic::ECS::Scene::Registry scene;
    auto entity = Make(scene, D::GraphNode);
    auto &vertices = Properties(scene, entity, D::GraphNode);
    vertices.GetOrAdd<glm::vec3>("v:normal").Vector() =
        std::as_const(vertices).Get<glm::vec3>("samples").Vector();
    const auto revision = std::as_const(vertices).Get<glm::vec3>("v:normal").Revision();
    auto config = Config(entity, D::GraphNode);
    config.Positions.Name = "v:normal";
    config.Method = R::NormalEstimationMethod::GraphNeighborhood;
    R::EditorGeometryProcessingContext context{.Scene = &scene};
    const auto result = R::ApplyEditorNormalEstimationCommand(context, config);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.ValidCount, 4);
    EXPECT_EQ(std::as_const(vertices).Get<glm::vec3>("v:normal").Revision(), revision);
    EXPECT_EQ(std::as_const(vertices).Get<glm::vec3>("v:normal")[1], plane[1]);
}


TEST(NormalEstimation, FaceNormalsUseFullPolygonRingAndPublishOnlyFaceOutput)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = scene.Create();
    Geometry::HalfedgeMesh::Mesh mesh;
    // First three corners are collinear; the full pentagon still has a +Z normal.
    std::vector<Geometry::VertexHandle> ring;
    for (const glm::vec3 p : {glm::vec3{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}})
        ring.push_back(mesh.AddVertex(p));
    ASSERT_TRUE(mesh.AddFace(ring));
    const auto a = mesh.AddVertex({3, 0, 0}), b = mesh.AddVertex({3, 0, 1}),
               c = mesh.AddVertex({3, 1, 0});
    ASSERT_TRUE(mesh.AddTriangle(a, b, c)); // -X from winding.
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    auto &vertices = Properties(scene, entity, D::MeshVertex);
    auto &faces = Properties(scene, entity, D::MeshFace);
    (void)vertices.GetOrAdd<glm::vec3>("v:normal", {0, 1, 0});
    (void)faces.GetOrAdd<float>("keep", 42.f);
    const auto positions = std::as_const(vertices).Get<glm::vec3>("v:position");
    const auto revision = positions.Revision();
    R::NormalEstimationConfig config;
    config.StableEntityId = R::SelectionController::ToStableEntityId(entity);
    config.Method = R::NormalEstimationMethod::MeshFaceNormals;
    config.Positions.Domain = D::MeshVertex;
    config.Output = {D::MeshFace, "f:normal", Geometry::PropertyValueKind::Vec3};
    const auto json = R::SerializeNormalEstimationConfig(config);
    EXPECT_TRUE(R::ValidateNormalEstimationConfigSection(json, {}, {}).Usable());
    Extrinsic::Core::Config::EngineConfig engineConfig;
    R::SetNormalEstimationConfig(engineConfig, config);
    ASSERT_TRUE(R::GetNormalEstimationConfig(engineConfig));
    EXPECT_EQ(R::GetNormalEstimationConfig(engineConfig)->Method, config.Method);
    R::EditorCommandHistory history;
    R::EditorGeometryProcessingContext context{.Scene = &scene, .CommandHistory = &history};
    ASSERT_TRUE(R::PreviewEditorNormalEstimationCommand(context, config).Ready);
    const auto result = R::ApplyEditorNormalEstimationCommand(context, config);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.SlotCount, 2u);
    EXPECT_EQ(result.ValidCount, 2u);
    EXPECT_EQ(result.ProcessedFaces, 2u);
    EXPECT_EQ(result.ActualBackend, "cpu_mesh_face_normals");
    const auto normals = std::as_const(faces).Get<glm::vec3>("f:normal");
    ASSERT_EQ(normals.Size(), 2u);
    EXPECT_EQ(normals[0], (glm::vec3{0, 0, 1}));
    EXPECT_EQ(normals[1], (glm::vec3{-1, 0, 0}));
    EXPECT_EQ(positions.Revision(), revision);
    EXPECT_EQ(std::as_const(vertices).Get<glm::vec3>("v:normal")[0], (glm::vec3{0, 1, 0}));
    EXPECT_EQ(std::as_const(faces).Get<float>("keep")[1], 42.f);
    EXPECT_EQ(history.Undo().Status, R::EditorCommandHistoryStatus::Undone);
    EXPECT_FALSE(faces.Exists("f:normal"));
    EXPECT_EQ(history.Redo().Status, R::EditorCommandHistoryStatus::Redone);
    EXPECT_EQ(std::as_const(faces).Get<glm::vec3>("f:normal")[1], (glm::vec3{-1, 0, 0}));
}

TEST(NormalEstimation, FaceNormalsPreserveDeletedSlotsAndReportDegenerateFallback)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = Make(scene, D::MeshVertex);
    auto &faces = Properties(scene, entity, D::MeshFace);
    faces.GetOrAdd<bool>("f:deleted")[0] = true;
    faces.Get<std::uint32_t>("f:halfedge")[0] = std::numeric_limits<std::uint32_t>::max();
    (void)faces.GetOrAdd<glm::vec3>("f:normal", {0, -1, 0});
    auto config = Config(entity, D::MeshVertex);
    config.Method = R::NormalEstimationMethod::MeshFaceNormals;
    config.Output = {D::MeshFace, "f:normal", Geometry::PropertyValueKind::Vec3};
    R::EditorGeometryProcessingContext context{.Scene = &scene};
    auto result = R::ApplyEditorNormalEstimationCommand(context, config);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.WrittenCount, 1u);
    EXPECT_EQ(result.SlotCount, 2u);
    EXPECT_EQ(std::as_const(faces).Get<glm::vec3>("f:normal")[0], (glm::vec3{0, -1, 0}));
    EXPECT_EQ(std::as_const(faces).Get<glm::vec3>("f:normal")[1], (glm::vec3{0, 0, 1}));
    Properties(scene, entity, D::MeshVertex).Get<glm::vec3>("samples").Vector() =
        {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
    config.FallbackNormal = {0, 2, 0};
    result = R::ApplyEditorNormalEstimationCommand(context, config);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.FallbackCount, 1u);
    EXPECT_EQ(result.ValidCount, 0u);
    EXPECT_EQ(std::as_const(faces).Get<glm::vec3>("f:normal")[1], (glm::vec3{0, 1, 0}));
    config.Output.Domain = D::MeshVertex;
    EXPECT_FALSE(R::PreviewEditorNormalEstimationCommand(context, config).Ready);
}


TEST(NormalEstimation, QueuedFaceNormalsPublishToFacesAndRejectStaleTopology)
{
    for (const bool stale : {false, true})
    {
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, D::MeshVertex);
        auto config = Config(entity, D::MeshVertex);
        config.Method = R::NormalEstimationMethod::MeshFaceNormals;
        config.Output = {D::MeshFace, "f:normal", Geometry::PropertyValueKind::Vec3};
        Intrinsic::Tests::EditorFeatureTestContext context;
        context.Scene = &scene;
        std::optional<R::EditorNormalEstimationResult> delivered;
        context.MethodResultSinks.NormalEstimation = [&](auto result) { delivered = std::move(result); };
        Extrinsic::Tests::EditorJobHarness jobs;
        jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorNormalEstimationCommand(context, config).Status, R::EditorCommandStatus::Pending);
        auto &faces = Properties(scene, entity, D::MeshFace);
        if (stale)
            faces.GetOrAdd<bool>("f:deleted")[0] = true;
        ASSERT_TRUE(jobs.DrainUntilTerminal());
        ASSERT_TRUE(delivered);
        EXPECT_EQ(delivered->Succeeded(), !stale) << delivered->Message;
        EXPECT_EQ(faces.Exists("f:normal"), !stale);
        EXPECT_FALSE(Properties(scene, entity, D::MeshVertex).Exists("f:normal"));
        if (!stale)
            EXPECT_EQ(std::as_const(faces).Get<glm::vec3>("f:normal").Vector(),
                      (std::vector<glm::vec3>{{0, 0, 1}, {0, 0, 1}}));
    }
}
