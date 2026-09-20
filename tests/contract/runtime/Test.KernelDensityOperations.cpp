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
#include "EditorFeatureTestContext.hpp"
#include "SandboxEditorJobHarness.hpp"

import Extrinsic.Runtime.PointFieldOperations;
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
import Extrinsic.Graphics.Component.VisualizationConfig;
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
            samples[i] = {float(i)*0.01f,0,0};
        samples[domain == D::PointCloudPoint ? 3 : samples.Size()-1] = {10,0,0};
        auto keep = props.GetOrAdd<float>("keep");
        std::ranges::fill(keep.Vector(), 42.f);
        if (domain == D::PointCloudPoint)
        {
            props.GetOrAdd<bool>("v:deleted")[4] = true;
            samples[4] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        }
        return entity;
    }
    R::KernelDensityConfig Config(entt::entity entity, D domain)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Positions = {.Domain = domain,
                              .Name = "samples",
                              .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Density = {domain, "density", Geometry::PropertyValueKind::Float},
                .KNeighbors = 2};
    }
} // namespace

TEST(KernelDensityOperations, QueuedJobsRejectStaleInputsOutputsAndCancellation)
{
    for(unsigned change=0;change<5;++change)
    {
        SCOPED_TRACE(change);
        Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshVertex);auto config=Config(entity,D::MeshVertex);
        auto& props=Properties(scene,entity,D::MeshVertex);
        Intrinsic::Tests::EditorFeatureTestContext context;context.Scene=&scene;
        R::EditorCommandHistory history;context.CommandHistory=&history;
        std::optional<R::EditorKernelDensityResult> delivered;
        const auto resultSink=[&](auto r){delivered=std::move(r);};
        Extrinsic::Tests::EditorJobHarness jobs;jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorKernelDensityCommand(context, config, resultSink).Status,R::EditorCommandStatus::Pending);
        EXPECT_EQ(R::ApplyEditorKernelDensityCommand(context, config, resultSink).Status,R::EditorCommandStatus::Pending);
        EXPECT_EQ(jobs.Snapshot().Entries.size(),1);
        switch(change)
        {
        case 0:props.Get<float>("keep")[0]=99;break;
        case 1:props.Get<glm::vec3>("samples")[0].x+=1;break;
        case 2:props.GetOrAdd<bool>("v:deleted")[0]=true;break;
        case 3:props.GetOrAdd<float>("density")[0]=77;break;
        case 4:(void)jobs.Jobs().Cancel(jobs.Snapshot().Entries[0].Token);break;
        }
        ASSERT_TRUE(jobs.DrainUntilTerminal());ASSERT_TRUE(delivered);
        EXPECT_EQ(delivered->Succeeded(),change==0)<<delivered->Message;
        EXPECT_EQ(history.CanUndo(),change==0);
        EXPECT_EQ(props.Exists("density"),change==0 || change==3);
    }
}
TEST(KernelDensityOperations, OnlyActiveJobRecordsBlockSubmission)
{
    using State = R::JobState;
    const std::array cases{
        std::pair{State::Invalid, false}, std::pair{State::AwaitingDependencies, true},
        std::pair{State::Queued, true}, std::pair{State::Running, true},
        std::pair{State::AwaitingGate, true}, std::pair{State::AwaitingApply, true},
        std::pair{State::Published, false}, std::pair{State::Dropped, false},
        std::pair{State::Cancelled, false}, std::pair{State::Rejected, false},
        std::pair{State::StaleDiscarded, false}};
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = Make(scene, D::MeshVertex);
    const auto config = Config(entity, D::MeshVertex);
    R::EditorProcessingContext context{.Scene = &scene};
    unsigned submissions = 0, lookups = 0;
    context.JobCommands.Submit = [&](R::JobDesc, R::EditorJobIdentity identity) {
        ++submissions;
        EXPECT_EQ(identity.EntityId, config.StableEntityId);
        EXPECT_EQ(identity.OutputName, config.Density.Name);
        return R::JobToken{};
    };
    const auto apply = [&] {
        submissions = lookups = 0;
        return R::ApplyEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), config);
    };
    for (const auto [state, blocks] : cases)
    {
        SCOPED_TRACE(static_cast<unsigned>(state));
        context.JobCommands.FindActive = [&](const R::EditorJobIdentity& identity) {
            ++lookups;
            return std::optional{R::EditorJobRecord{.Identity = identity, .State = state}};
        };
        const auto result = apply();
        EXPECT_EQ(lookups, 1u);
        EXPECT_EQ(submissions, blocks ? 0u : 1u);
        EXPECT_EQ(result.Status, blocks ? R::EditorCommandStatus::Pending
                                       : R::EditorCommandStatus::GeometryProcessingFailed);
    }
    context.JobCommands.FindActive = {};
    EXPECT_EQ(apply().Status, R::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(lookups, 0u);
    EXPECT_EQ(submissions, 1u);
    context.JobCommands.FindActive = [&](const R::EditorJobIdentity&) -> std::optional<R::EditorJobRecord> {
        ++lookups;
        return std::nullopt;
    };
    EXPECT_EQ(apply().Status, R::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(lookups, 1u);
    EXPECT_EQ(submissions, 1u);
    EXPECT_FALSE(Properties(scene, entity, D::MeshVertex).Exists("density"));
}

TEST(KernelDensityConfig, RoundTripAndSharedPreviewApplyRun)
{
    namespace C = Extrinsic::Core::Config;
    Extrinsic::ECS::Scene::Registry scene;
    auto entity = Make(scene, D::MeshFace);
    auto config = Config(entity, D::MeshFace);
    config.Bandwidth = 5;
    C::EngineConfigSectionRegistry registry;
    ASSERT_TRUE(registry.Register(R::MakeKernelDensityConfigSectionRegistration()));
    R::RuntimeEngineConfigControlState state;
    C::PopulateEngineConfigSectionDefaults(state.ActiveConfig, registry);
    R::EditorProcessingContext context{.Scene = &scene};
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
    auto commands = R::BindEditorProcessingCommands(context);
    ASSERT_TRUE(R::PreviewEditorKernelDensityCommand(commands, config).Enabled);
    EXPECT_FALSE(Properties(scene, entity, D::MeshFace).Exists("density"));
    ASSERT_TRUE(R::ApplyEditorKernelDensityConfig(commands, config).Succeeded());
    ASSERT_TRUE(R::GetEditorKernelDensityConfig(commands));
    EXPECT_EQ(R::SerializeKernelDensityConfig(*R::GetEditorKernelDensityConfig(commands)),
              R::SerializeKernelDensityConfig(config));
    ASSERT_TRUE(R::ApplyEditorConfiguredKernelDensity(commands).Succeeded());
    EXPECT_EQ(previews, 1);
    EXPECT_EQ(applies, 1);
    for (auto payload : {R"({"backend":"vulkan"})", R"({"k_neighbors":-1})", R"({"bandwidth":-1})",
                         R"({"gpu_query_batch_size":0})", R"({"bandwidth":1e100})", R"({"bandwidth":1e-100})", R"({"unknown":1})",
                         R"({"density":{"domain":"unknown","name":"x","kind":"uint32"}})"})
        EXPECT_FALSE(R::ValidateKernelDensityConfigSection(payload, {}, "test").Usable()) << payload;
    config.Bandwidth = -1;
    EXPECT_FALSE(R::ApplyEditorKernelDensityConfig(commands, config).Succeeded());
    EXPECT_EQ(applies, 1);
}

TEST(KernelDensityOperations, EveryDomainPublishesNamedDensityAndPreservesDeletedRowsWithHistory)
{
    for(unsigned d=1;d<=8;++d) for(float bandwidth : {0.f,.2f}) for(unsigned k : {1u, 2u, 63u})
    {
        SCOPED_TRACE(d);
        SCOPED_TRACE(bandwidth);
        SCOPED_TRACE(k);
        R::WorldRegistry worlds;auto world=worlds.CreateWorld("density");auto& scene=*worlds.Get(world);
        R::SpatialIndexCache cache(worlds);auto entity=Make(scene,D(d));auto config=Config(entity,D(d));config.Bandwidth=bandwidth;config.KNeighbors=k;
        auto& props=Properties(scene,entity,D(d));
        if (D(d) == D::PointCloudPoint)
        {
            props.Resize(70);
            auto samples = props.Get<glm::vec3>("samples");
            for (std::size_t i = 0; i < samples.Size(); ++i)
                samples[i] = {float(i * i) * 0.01f, 0, 0};
            samples[1] = samples[0];
        }
        const auto size=props.Size();
        const bool half=D(d)==D::MeshHalfedge || D(d)==D::GraphHalfedge;
        if(half)scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<bool>("e:deleted")[1]=true;
        else props.GetOrAdd<bool>(D(d)==D::MeshFace?"f:deleted":(D(d)==D::MeshEdge || D(d)==D::GraphEdge)?"e:deleted":"v:deleted")[2]=true;
        props.Get<glm::vec3>("samples")[2]={std::numeric_limits<float>::quiet_NaN(),0,0};
        props.GetOrAdd<float>("density").Vector().assign(size,77);
        const auto positionRevision=std::as_const(props).Get<glm::vec3>("samples").Revision();
        R::EditorCommandHistory history;
        R::EditorProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
        const auto catalog=R::GetEditorKernelDensityInputCatalog(R::BindEditorProcessingCommands(context), config.StableEntityId);
        EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==config.Positions;}));
        ASSERT_TRUE(R::PreviewEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), config).Enabled);
        const auto reference=R::ApplyEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), config);
        ASSERT_TRUE(reference.Succeeded())<<reference.Message;EXPECT_EQ(reference.ActualBackend,"cpu_octree");
        const auto values=std::as_const(props).Get<float>("density").Vector();
        EXPECT_EQ(values[2],77);if(half)EXPECT_EQ(values[3],77);
        EXPECT_EQ(props.Size(),size);EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(),positionRevision);
        props.Get<float>("keep")[0]=99;
        ASSERT_TRUE(history.Undo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("density")[0],77);
        ASSERT_TRUE(history.Redo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("keep")[0],99);
        config.Backend=R::KernelDensityBackend::CpuLBVH;
        const auto indexed=R::ApplyEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), config);
        ASSERT_TRUE(indexed.Succeeded())<<indexed.Message;EXPECT_EQ(indexed.ActualBackend,"cpu_lbvh");
        const auto actual=std::as_const(props).Get<float>("density");
        for(std::size_t i=0;i<size;++i)EXPECT_NEAR(actual[i],values[i],1e-5*std::max(1.f,values[i]));
        EXPECT_FLOAT_EQ(indexed.UsedBandwidth,reference.UsedBandwidth);
        EXPECT_TRUE(R::ApplyEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), config).IndexReused);
        Intrinsic::Tests::EditorFeatureTestContext visualization;visualization.Scene=&scene;visualization.VisualizationCommandsAvailable=true;
        std::optional<R::VisualizationRecipe> stored;
        visualization.VisualizationRecipes.GetRecipe=[&](std::uint32_t){return stored;};
        visualization.VisualizationRecipes.SetRecipe=[&](std::uint32_t,R::VisualizationRecipe r){stored=std::move(r);};
        visualization.VisualizationRecipes.ClearRecipe=[&](std::uint32_t){stored.reset();};
        const auto shown=R::ApplyEditorVisualizationRecipeCommand(visualization,{.StableEntityId=config.StableEntityId,
            .Recipe={.Data=R::ScalarVisualizationRecipe{.Source=config.Density,.OutputName="density.colors"}}});

        EXPECT_FALSE(stored);
        if (half)
            EXPECT_EQ(shown,R::EditorCommandStatus::InvalidVisualizationProperty);
        else
        {
            EXPECT_EQ(shown,R::EditorCommandStatus::Applied);
            const auto* overrides=scene.Raw().try_get<Extrinsic::Graphics::Components::VisualizationLaneOverrides>(entity);
            ASSERT_NE(overrides,nullptr);
            const auto& lane=D(d)==D::MeshVertex || D(d)==D::MeshFace ? overrides->Surface
                : D(d)==D::PointCloudPoint ? overrides->Points : overrides->Edges;
            ASSERT_TRUE(lane);
            EXPECT_EQ(lane->Source,Extrinsic::Graphics::Components::VisualizationConfig::ColorSource::ScalarField);
            EXPECT_EQ(lane->ScalarFieldName,config.Density.Name);
        }
        props.Get<float>("density")[0]=123;
        EXPECT_FALSE(history.Undo().Succeeded());
    }
}
TEST(KernelDensityOperations, InvalidUnsupportedAndNumericalFailuresRetainOutput)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::PointCloudPoint);auto config=Config(entity,D::PointCloudPoint);
    R::EditorProcessingContext context{.Scene=&scene};
    auto& props=Properties(scene,entity,D::PointCloudPoint);props.GetOrAdd<float>("density").Vector().assign(props.Size(),77);
    for(const char* name:{"v:deleted","samples"})
    {auto bad=config;bad.Density.Name=name;EXPECT_FALSE(R::PreviewEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), bad).Enabled);}
    auto unrelatedName=config;unrelatedName.Density.Name="h:next";
    EXPECT_TRUE(R::PreviewEditorKernelDensityCommand(R::BindEditorProcessingCommands(context),unrelatedName).Enabled);
    config.Backend=R::KernelDensityBackend::VulkanLBVH;
    EXPECT_FALSE(R::ApplyEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), config).Succeeded());
    config.Backend=R::KernelDensityBackend::CpuOctree;config.Bandwidth=1e-30f;
    EXPECT_FALSE(R::ApplyEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), config).Succeeded());
    EXPECT_EQ(std::as_const(props).Get<float>("density")[0],77);
    config.Bandwidth=1;config.KNeighbors=std::numeric_limits<std::uint32_t>::max();
    EXPECT_TRUE(R::ApplyEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), config).Succeeded());
}
TEST(KernelDensityOperations, IndexAdmissionFailurePreservesOutputAndCacheDiagnostic)
{
    R::WorldRegistry worlds;
    const auto world = worlds.CreateWorld("density");
    auto& scene = *worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    const auto entity = Make(scene, D::PointCloudPoint);
    auto config = Config(entity, D::PointCloudPoint);
    config.Backend = R::KernelDensityBackend::CpuLBVH;
    auto& props = Properties(scene, entity, D::PointCloudPoint);
    props.GetOrAdd<float>("density").Vector().assign(props.Size(), 77);
    const auto revision = std::as_const(props).Get<float>("density").Revision();
    R::EditorProcessingContext context{.Scene = &scene, .World = {}, .SpatialIndices = &cache};
    const auto expected = cache.Acquire(context.World, entity, config.Positions);
    ASSERT_FALSE(expected.Ready());
    ASSERT_FALSE(expected.Diagnostic.empty());

    const auto result = R::ApplyEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), config);

    EXPECT_EQ(result.Status, R::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(result.Message, expected.Diagnostic);
    EXPECT_FALSE(result.IndexReused);
    EXPECT_EQ(std::as_const(props).Get<float>("density").Revision(), revision);
    EXPECT_TRUE(std::ranges::all_of(std::as_const(props).Get<float>("density").Vector(),
                                   [](float value) { return value == 77; }));
}

TEST(KernelDensityOperations, IndexAdmissionRejectsMismatchedPositionsAndSourceRows)
{
    for (const bool mismatchRows : {false, true})
    {
        SCOPED_TRACE(mismatchRows);
        R::WorldRegistry worlds;
        const auto world = worlds.CreateWorld("index source");
        auto& indexedScene = *worlds.Get(world);
        Extrinsic::ECS::Scene::Registry capturedScene;
        const auto entity = Make(capturedScene, D::PointCloudPoint);
        ASSERT_EQ(Make(indexedScene, D::PointCloudPoint), entity);
        auto config = Config(entity, D::PointCloudPoint);
        config.Backend = R::KernelDensityBackend::CpuLBVH;
        auto& indexed = Properties(indexedScene, entity, D::PointCloudPoint);
        auto points = indexed.Get<glm::vec3>("samples");
        if (mismatchRows)
        {
            // Keep compact coordinates identical while shifting every source row.
            for (std::size_t i = 4; i > 0; --i) points[i] = points[i - 1];
            indexed.Get<bool>("v:deleted")[0] = true;
            indexed.Get<bool>("v:deleted")[4] = false;
        }
        else points[0].x += 1;
        R::SpatialIndexCache cache(worlds);
        auto& props = Properties(capturedScene, entity, D::PointCloudPoint);
        props.GetOrAdd<float>("density").Vector().assign(props.Size(), 77);
        const auto revision = std::as_const(props).Get<float>("density").Revision();
        R::EditorProcessingContext context{.Scene = &capturedScene, .World = world, .SpatialIndices = &cache};
        for (const bool reused : {false, true})
        {
            const auto result = R::ApplyEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), config);
            EXPECT_EQ(result.Status, R::EditorCommandStatus::StaleEntity);
            EXPECT_EQ(result.Message, "Density index snapshot does not match the selected samples.");
            EXPECT_EQ(result.IndexReused, reused);
            EXPECT_EQ(std::as_const(props).Get<float>("density").Revision(), revision);
            EXPECT_TRUE(std::ranges::all_of(std::as_const(props).Get<float>("density").Vector(),
                                           [](float value) { return value == 77; }));
        }
    }
}

TEST(KernelDensityOperations, NewOutputUndoAndPositionEditsRebuildTheCache)
{
    R::WorldRegistry worlds;auto world=worlds.CreateWorld("density");auto& scene=*worlds.Get(world);
    R::SpatialIndexCache cache(worlds);auto entity=Make(scene,D::PointCloudPoint);auto config=Config(entity,D::PointCloudPoint);
    config.Backend=R::KernelDensityBackend::CpuLBVH;config.KNeighbors=std::numeric_limits<std::uint32_t>::max();
    R::EditorCommandHistory history;
    R::EditorProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
    auto& props=Properties(scene,entity,D::PointCloudPoint);
    ASSERT_TRUE(R::ApplyEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), config).Succeeded());
    ASSERT_TRUE(history.Undo().Succeeded());EXPECT_FALSE(props.Exists("density"));
    ASSERT_TRUE(history.Redo().Succeeded());EXPECT_TRUE(props.Exists("density"));
    props.Get<glm::vec3>("samples")[0].x+=.1f;
    const auto rerun=R::ApplyEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), config);
    ASSERT_TRUE(rerun.Succeeded())<<rerun.Message;EXPECT_FALSE(rerun.IndexReused);
}

TEST(KernelDensityOperations, ReplacedStorageRejectsUndo)
{
    for (const bool replaceOutput : {false, true})
    {
        Extrinsic::ECS::Scene::Registry scene;
        auto entity = Make(scene, D::PointCloudPoint);
        auto config = Config(entity, D::PointCloudPoint);
        auto& props = Properties(scene, entity, D::PointCloudPoint);
        R::EditorCommandHistory history;
        R::EditorProcessingContext context{.Scene=&scene, .CommandHistory=&history};
        ASSERT_TRUE(R::ApplyEditorKernelDensityCommand(R::BindEditorProcessingCommands(context), config).Succeeded());
        const auto computed = std::as_const(props).Get<float>("density").Vector();
        if (replaceOutput)
        {
            auto oldOutput = props.Get<float>("density");
            props.Remove(oldOutput);
            props.GetOrAdd<glm::vec3>("density").Vector().assign(props.Size(), glm::vec3(17));
        }
        else
        {
            const auto samples = std::as_const(props).Get<glm::vec3>("samples").Vector();
            auto oldSamples = props.Get<glm::vec3>("samples");
            props.Remove(oldSamples);
            props.GetOrAdd<glm::vec3>("samples").Vector() = samples;
        }
        EXPECT_FALSE(history.Undo().Succeeded());
        if (replaceOutput)
            EXPECT_EQ(std::as_const(props).Get<glm::vec3>("density")[0], glm::vec3(17));
        else EXPECT_EQ(std::as_const(props).Get<float>("density").Vector(), computed);
    }
}

TEST(KernelDensityOperations, ExpiredAttachmentRejectsQueuedPublicationAndDelivery)
{
    auto scene = std::make_unique<Extrinsic::ECS::Scene::Registry>();
    const auto entity = Make(*scene, D::MeshVertex);
    const auto config = Config(entity, D::MeshVertex);
    bool active = true;
    Intrinsic::Tests::EditorFeatureTestContext context;
    context.Scene = scene.get();
    context.AttachmentActive = [&] { return active; };
    R::EditorCommandHistory history;
    context.CommandHistory = &history;
    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(context);
    unsigned deliveries = 0;
    const auto commands = R::BindEditorProcessingCommands(context);
    ASSERT_EQ(R::ApplyEditorKernelDensityCommand(commands, config,
        [&](R::EditorKernelDensityResult) { ++deliveries; }).Status, R::EditorCommandStatus::Pending);
    active = false;
    scene.reset();
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    EXPECT_EQ(deliveries, 0u);
    EXPECT_FALSE(history.CanUndo());
    EXPECT_FALSE(commands.IsBound());
}
