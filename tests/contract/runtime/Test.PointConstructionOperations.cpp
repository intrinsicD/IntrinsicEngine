#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include "SandboxEditorJobHarness.hpp"
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.Graphics.Component.RenderGeometry;
import Geometry.HalfedgeMesh;
import Geometry.Graph;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace EC = Extrinsic::ECS::Components;
using D = R::GeometryElementDomain;
namespace
{
    Geometry::PropertySet& Properties(Extrinsic::ECS::Scene::Registry& scene, entt::entity entity,
                                      D domain)
    {
        return *const_cast<Geometry::PropertySet*>(R::ResolveGeometryPropertySet(
            R::BuildGeometryAvailability(scene.Raw(), entity), domain));
    }
    entt::entity Make(Extrinsic::ECS::Scene::Registry& scene, D domain)
    {
        auto entity = Extrinsic::ECS::Scene::CreateDefault(scene, "Source");
        const std::array<glm::vec3, 4> points{{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {0, 0, 1}}};
        if (domain >= D::MeshVertex && domain <= D::MeshFace)
        {
            Geometry::HalfedgeMesh::Mesh mesh;
            std::array<Geometry::VertexHandle, 4> v;
            for (unsigned i = 0; i < 4; ++i)
                v[i] = mesh.AddVertex(points[i]);
            (void)mesh.AddTriangle(v[0], v[2], v[1]);
            (void)mesh.AddTriangle(v[0], v[1], v[3]);
            (void)mesh.AddTriangle(v[0], v[3], v[2]);
            (void)mesh.AddTriangle(v[1], v[2], v[3]);
            GS::PopulateFromMesh(scene.Raw(), entity, mesh);
        }
        else if (domain != D::PointCloudPoint)
        {
            Geometry::Graph::Graph graph;
            std::array<Geometry::VertexHandle, 4> v;
            for (unsigned i = 0; i < 4; ++i)
                v[i] = graph.AddVertex(points[i]);
            for (unsigned i = 0; i < 4; ++i)
                for (unsigned j = i + 1; j < 4; ++j)
                    (void)graph.AddEdge(v[i], v[j]);
            GS::PopulateFromGraph(scene.Raw(), entity, graph);
        }
        else
            scene.Raw().emplace<GS::Vertices>(entity).Properties.Resize(5);
        auto& props = Properties(scene, entity, domain);
        auto samples = props.GetOrAdd<glm::vec3>("samples");
        for (std::size_t i = 0; i < props.Size(); ++i)
            samples[i] = {float(i % 3), float(i / 3), .15f * float(i % 2)};
        props.GetOrAdd<glm::vec3>("directions").Vector().assign(props.Size(), glm::vec3(0, 0, 1));
        props.GetOrAdd<float>("keep").Vector().assign(props.Size(), 42);
        if (domain == D::PointCloudPoint)
        {
            props.GetOrAdd<bool>("v:deleted")[4] = true;
            samples[4] = {NAN, 0, 0};
        }
        return entity;
    }
    R::PointConstructionConfig Config(entt::entity entity, D domain,
                                      R::PointConstructionMethod method)
    {
        R::PointConstructionConfig c;
        c.StableEntityId = R::SelectionController::ToStableEntityId(entity);
        c.Method = method;
        c.Positions = {domain, "samples", Geometry::PropertyValueKind::Vec3};
        c.Normals = {domain, "directions", Geometry::PropertyValueKind::Vec3};
        c.EstimateNormals = false;
        c.Resolution = 6;
        c.KNeighbors = 2;
        c.GpuQueryBatchSize = 17;
        return c;
    }
    entt::entity Output(const Extrinsic::ECS::Scene::Registry& scene, std::uint32_t id)
    {
        for (auto e : scene.Raw().view<EC::StableId>())
            if (R::SelectionController::ToStableEntityId(e) == id)
                return e;
        return entt::null;
    }
    std::vector<glm::vec3> OutputPoints(const Extrinsic::ECS::Scene::Registry& scene,
                                        std::uint32_t id)
    {
        return scene.Raw()
            .get<GS::Vertices>(Output(scene, id))
            .Properties.Get<glm::vec3>("v:position")
            .Vector();
    }
} // namespace
TEST(PointConstructionConfig, RoundTripPreviewApplyAndRejectMalformedControls)
{
    namespace C = Extrinsic::Core::Config;
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = Make(scene, D::MeshFace);
    auto c = Config(entity, D::MeshFace, R::PointConstructionMethod::KnnGraph);
    C::EngineConfigSectionRegistry registry;
    ASSERT_TRUE(registry.Register(R::MakePointConstructionConfigSectionRegistration()));
    R::RuntimeEngineConfigControlState state;
    C::PopulateEngineConfigSectionDefaults(state.ActiveConfig, registry);
    R::EditorGeometryProcessingContext context{.Scene = &scene};
    context.EngineConfigControlState = &state;
    context.EngineConfigCommandsAvailable = true;
    unsigned previews = 0, applies = 0;
    context.PreviewEngineConfigDocument = [&](const auto& document, const auto& origin)
    {
        ++previews;
        return C::PreviewEngineConfig(document, state.ActiveConfig, {origin, &registry});
    };
    context.ApplyEngineConfigHotSubset = [&](const auto& preview)
    {
        ++applies;
        state.ActiveConfig = preview.Preview.Config;
        return R::RuntimeEngineConfigApplyResult{.Status =
                                                     R::RuntimeEngineConfigApplyStatus::Applied};
    };
    const auto commands = R::BindEditorGeometryProcessingCommands(context);
    ASSERT_TRUE(R::PreviewEditorPointConstructionCommand(commands, c).Ready);
    EXPECT_TRUE(scene.Raw().view<EC::StableId>().empty());
    ASSERT_TRUE(R::ApplyEditorPointConstructionConfig(commands, c).Succeeded());
    ASSERT_TRUE(R::GetEditorPointConstructionConfig(commands));
    EXPECT_EQ(R::SerializePointConstructionConfig(*R::GetEditorPointConstructionConfig(commands)),
              R::SerializePointConstructionConfig(c));
    ASSERT_TRUE(R::ApplyEditorConfiguredPointConstruction(commands).Succeeded());
    EXPECT_EQ(previews, 1);
    EXPECT_EQ(applies, 1);
    for (auto payload :
         {R"({"backend":"vulkan"})", R"({"resolution":0})", R"({"resolution":513})",
          R"({"method":"poisson"})", R"({"k_neighbors":64})", R"({"k_neighbors":-1})",
          R"({"normal_k_neighbors":2})", R"({"gpu_query_batch_size":0})",
          R"({"max_grid_vertices":7})", R"({"output_name":""})", R"({"estimate_normals":1})",
          R"({"kernel_sigma_scale":0})", R"({"kernel_sigma_scale":1e-310})", R"({"unknown":1})"})
        EXPECT_FALSE(R::ValidatePointConstructionConfigSection(payload, {}, "test").Usable())
            << payload;
    c.KNeighbors = 0;
    EXPECT_FALSE(R::ApplyEditorPointConstructionConfig(commands, c).Succeeded());
    EXPECT_EQ(applies, 1);
}
TEST(PointConstructionOperations, EveryDomainPreservesSourceAndCachedOutputMatchesReference)
{
    for (unsigned d = 1; d <= 8; ++d)
        for (auto method :
             {R::PointConstructionMethod::Hoppe, R::PointConstructionMethod::KnnGraph})
        {
            SCOPED_TRACE(d);
            SCOPED_TRACE(unsigned(method));
            R::WorldRegistry worlds;
            const auto world = worlds.CreateWorld("construction");
            auto& scene = *worlds.Get(world);
            R::SpatialIndexCache cache(worlds);
            const auto entity = Make(scene, D(d));
            auto c = Config(entity, D(d), method);
            auto& props = Properties(scene, entity, D(d));
            const auto revision = props.Revision(), size = props.Size();
            R::EditorCommandHistory history;
            R::SelectionController selection;
            R::EditorGeometryProcessingContext context{.Scene = &scene,
                                                       .World = world,
                                                       .Selection = &selection,
                                                       .CommandHistory = &history,
                                                       .SpatialIndices = &cache};
            const auto catalog =
                R::GetEditorPointConstructionInputCatalog(context, c.StableEntityId);
            EXPECT_TRUE(std::ranges::any_of(catalog.Entries,
                                            [&](const auto& e) { return e.Ref == c.Positions; }));
            ASSERT_TRUE(R::PreviewEditorPointConstructionCommand(context, c).Ready);
            const auto reference = R::ApplyEditorPointConstructionCommand(context, c);
            ASSERT_TRUE(reference.Succeeded()) << reference.Message;
            const auto generated = Output(scene, reference.OutputEntityId);
            ASSERT_NE(generated, entt::entity{entt::null});
            const auto expected = OutputPoints(scene, reference.OutputEntityId);
            const auto durable = scene.Raw().get<EC::StableId>(generated);
            EXPECT_TRUE(
                (scene.Raw().all_of<EC::Selection::SelectableTag, EC::Culling::Local::Bounds>(
                    generated)));
            if (method == R::PointConstructionMethod::Hoppe)
            {
                EXPECT_TRUE(
                    (scene.Raw().all_of<Extrinsic::Graphics::Components::RenderSurface, GS::Faces>(
                        generated)));
                const auto v = GS::BuildConstView(scene.Raw(), generated);
                EXPECT_TRUE(v.VertexSource->Properties.Exists("v:normal"));
                EXPECT_TRUE(v.VertexSource->Properties.Exists("v:texcoord") ||
                            v.HalfedgeSource->Properties.Exists("h:texcoord"));
            }
            else
                EXPECT_TRUE((scene.Raw()
                                 .all_of<Extrinsic::Graphics::Components::RenderEdges,
                                         Extrinsic::Graphics::Components::RenderPoints,
                                         GS::HasGraphTopology>(generated)));
            EXPECT_EQ(props.Revision(), revision);
            EXPECT_EQ(props.Size(), size);
            EXPECT_EQ(selection.SelectedStableIds().front(), reference.OutputEntityId);
            props.Get<float>("keep")[0] = 99;
            ASSERT_TRUE(history.Undo().Succeeded());
            EXPECT_FALSE(scene.Raw().valid(generated));
            EXPECT_EQ(selection.SelectedStableIds().front(), c.StableEntityId);
            ASSERT_TRUE(history.Redo().Succeeded());
            const auto redone = Output(scene, selection.SelectedStableIds().front());
            ASSERT_NE(redone, entt::entity{entt::null});
            EXPECT_EQ(scene.Raw().get<EC::StableId>(redone), durable);
            EXPECT_EQ(std::as_const(props).Get<float>("keep")[0], 99);
            c.Backend = R::PointConstructionBackend::CpuLBVH;
            const auto indexed = R::ApplyEditorPointConstructionCommand(context, c);
            ASSERT_TRUE(indexed.Succeeded()) << indexed.Message;
            EXPECT_EQ(indexed.ActualBackend, "cpu_lbvh");
            EXPECT_EQ(indexed.OutputEdgeCount, reference.OutputEdgeCount);
            EXPECT_EQ(indexed.OutputFaceCount, reference.OutputFaceCount);
            EXPECT_EQ(OutputPoints(scene, indexed.OutputEntityId), expected);
            const auto warm = R::ApplyEditorPointConstructionCommand(context, c);
            ASSERT_TRUE(warm.Succeeded()) << warm.Message;
            EXPECT_TRUE(warm.IndexReused);
        }
}
TEST(PointConstructionOperations, GeneratedHistoryRejectsChangedPropertiesNameAndHierarchy)
{
    for (unsigned change = 0; change < 5; ++change)
    {
        Extrinsic::ECS::Scene::Registry scene;
        const auto source = Make(scene, D::PointCloudPoint);
        R::EditorCommandHistory history;
        R::EditorGeometryProcessingContext context{.Scene = &scene, .CommandHistory = &history};
        const auto result = R::ApplyEditorPointConstructionCommand(
            context, Config(source, D::PointCloudPoint, R::PointConstructionMethod::KnnGraph));
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        const auto output = Output(scene, result.OutputEntityId);
        if (change == 0)
            scene.Raw().get<GS::Vertices>(output).Properties.GetOrAdd<int>("custom")[0] = 99;
        if (change == 1)
            scene.Raw().get<GS::Vertices>(output).Properties.Get<glm::vec3>("v:position")[0].x += 1;
        if (change == 2)
            scene.Raw().get<EC::Hierarchy::Component>(output).ChildCount = 1;
        if (change == 3)
            scene.Raw().get<EC::Transform::Component>(output).Position.x = 1;
        if (change == 4)
            scene.Raw().get<EC::MetaData>(output).EntityName = "Renamed output";
        EXPECT_EQ(history.Undo().Status, R::EditorCommandHistoryStatus::StaleEntity);
        EXPECT_TRUE(scene.Raw().valid(output));
    }
}
TEST(PointConstructionOperations, FullHierarchyTransformIsBakedWithoutChangingMetric)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto source = Make(scene, D::PointCloudPoint);
    const auto parent = Extrinsic::ECS::Scene::CreateDefault(scene, "Parent");
    scene.Raw().get<EC::Hierarchy::Component>(source).Parent = parent;
    auto& parentTransform = scene.Raw().get<EC::Transform::Component>(parent);
    parentTransform.Position = {30, 40, 50};
    parentTransform.Scale = {-2, 3, 1};
    auto& sourceTransform = scene.Raw().get<EC::Transform::Component>(source);
    sourceTransform.Position = {1, 2, 3};
    const auto matrix =
        EC::Transform::GetMatrix(parentTransform) * EC::Transform::GetMatrix(sourceTransform);
    R::EditorGeometryProcessingContext context{.Scene = &scene};
    const auto result = R::ApplyEditorPointConstructionCommand(
        context, Config(source, D::PointCloudPoint, R::PointConstructionMethod::KnnGraph));
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    const auto actual = OutputPoints(scene, result.OutputEntityId);
    const auto input =
        std::as_const(Properties(scene, source, D::PointCloudPoint)).Get<glm::vec3>("samples");
    ASSERT_EQ(actual.size(), 4u);
    for (unsigned i = 0; i < 4; ++i)
        EXPECT_EQ(actual[i], glm::vec3(matrix * glm::vec4(input[i], 1)));
    const auto output = Output(scene, result.OutputEntityId);
    EXPECT_EQ(EC::Transform::GetMatrix(scene.Raw().get<EC::Transform::Component>(output)),
              glm::mat4(1));
    const auto bounds = scene.Raw().get<EC::Culling::Local::Bounds>(output).LocalBoundingAABB;
    EXPECT_GT(bounds.Min.y, 40);
}
TEST(PointConstructionOperations, JobsRejectChangedInputsTransformsDetachAndCancellation)
{
    for (unsigned change = 0; change < 7; ++change)
    {
        SCOPED_TRACE(change);
        Extrinsic::ECS::Scene::Registry scene;
        const auto source = Make(scene, D::MeshVertex);
        auto c = Config(source, D::MeshVertex, R::PointConstructionMethod::Hoppe);
        Intrinsic::Tests::EditorFeatureTestContext context;
        context.Scene = &scene;
        R::EditorCommandHistory history;
        context.CommandHistory = &history;
        bool attached = true;
        context.AttachmentActive = [&] { return attached; };
        std::optional<R::EditorPointConstructionResult> delivered;
        context.MethodResultSinks.PointConstruction = [&](auto r) { delivered = std::move(r); };
        Extrinsic::Tests::EditorJobHarness jobs;
        jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorPointConstructionCommand(context, c).Status,
                  R::EditorCommandStatus::Pending);
        auto& props = Properties(scene, source, D::MeshVertex);
        switch (change)
        {
        case 0:
            props.Get<float>("keep")[0] = 99;
            break;
        case 1:
            props.Get<glm::vec3>("samples")[0].x += 1;
            break;
        case 2:
            props.Get<glm::vec3>("directions")[0].x += 1;
            break;
        case 3:
            props.GetOrAdd<bool>("v:deleted")[0] = true;
            break;
        case 4:
            scene.Raw().get<EC::Transform::Component>(source).Position.x += 1;
            break;
        case 5:
            attached = false;
            break;
        case 6:
            (void)jobs.Jobs().Cancel(jobs.Snapshot().Entries.front().Token);
            break;
        }
        ASSERT_TRUE(jobs.DrainUntilTerminal());
        ASSERT_TRUE(delivered);
        EXPECT_EQ(delivered->Succeeded(), change == 0) << delivered->Message;
        EXPECT_EQ(history.CanUndo(), change == 0);
        EXPECT_EQ(scene.Raw().view<EC::StableId>().empty(), change != 0);
    }
}
TEST(PointConstructionOperations, UnavailableGpuAndInvalidPreparationPublishNothing)
{
    R::WorldRegistry worlds;
    const auto world = worlds.CreateWorld("construction");
    auto& scene = *worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    const auto source = Make(scene, D::PointCloudPoint);
    auto c = Config(source, D::PointCloudPoint, R::PointConstructionMethod::Hoppe);
    R::EditorCommandHistory history;
    R::EditorGeometryProcessingContext context{
        .Scene = &scene, .World = world, .CommandHistory = &history, .SpatialIndices = &cache};
    c.Backend = R::PointConstructionBackend::VulkanLBVH;
    EXPECT_FALSE(R::PreviewEditorPointConstructionCommand(context, c).Ready);
    EXPECT_FALSE(R::ApplyEditorPointConstructionCommand(context, c).Succeeded());
    c.Backend = R::PointConstructionBackend::CpuReference;
    c.MaxGridVertices = 8;
    EXPECT_FALSE(R::ApplyEditorPointConstructionCommand(context, c).Succeeded());
    EXPECT_FALSE(history.CanUndo());
    EXPECT_TRUE(scene.Raw().view<EC::StableId>().empty());
    c.MaxGridVertices = 1u << 22;
    Properties(scene, source, D::PointCloudPoint).Get<glm::vec3>("directions")[0] = {0, 0, 0};
    EXPECT_FALSE(R::PreviewEditorPointConstructionCommand(context, c).Ready);
}
TEST(PointConstructionOperations, OneSampleGraphRetainsTheIsolatedNode)
{
    R::WorldRegistry worlds;
    const auto world = worlds.CreateWorld("one point");
    auto& scene = *worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    const auto source = Make(scene, D::PointCloudPoint);
    auto c = Config(source, D::PointCloudPoint, R::PointConstructionMethod::KnnGraph);
    Properties(scene, source, D::PointCloudPoint).Resize(1);
    R::EditorGeometryProcessingContext context{
        .Scene = &scene, .World = world, .SpatialIndices = &cache};
    for (auto backend :
         {R::PointConstructionBackend::CpuReference, R::PointConstructionBackend::CpuLBVH})
    {
        c.Backend = backend;
        const auto result = R::ApplyEditorPointConstructionCommand(context, c);
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        EXPECT_EQ(result.OutputVertexCount, 1u);
        EXPECT_EQ(result.OutputEdgeCount, 0u);
    }
}
