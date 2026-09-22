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
#include "PointDomainFixture.hpp"

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
import Extrinsic.Graphics.Component.VisualizationConfig;

namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
using D = R::GeometryElementDomain;
using Intrinsic::Tests::PointDomainProperties;
namespace
{
    constexpr std::array<glm::vec3, 4> plane{{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {2, 3, 0}}};
    entt::entity Make(Extrinsic::ECS::Scene::Registry &scene, D domain)
    {
        auto entity = Intrinsic::Tests::MakePointDomainSource(scene, domain);
        auto &props = PointDomainProperties(scene, entity, domain);
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
    R::PointSpacingConfig Config(entt::entity entity, D domain)
    {
        return {.StableEntityId = R::SelectionController::ToStableEntityId(entity),
                .Positions = {.Domain = domain,
                              .Name = "samples",
                              .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Radii = {domain, "radii", Geometry::PropertyValueKind::Float},
                .KNeighbors = 2};
    }
} // namespace

TEST(PointSpacingOperations, CopiedSummaryDescribesOnlyLiveSamples)
{
    R::WorldRegistry worlds;
    const auto world = worlds.CreateWorld("spacing summary");
    auto& scene = *worlds.Get(world);
    R::SpatialIndexCache cache(worlds);
    const auto entity = Make(scene, D::PointCloudPoint);
    auto samples = PointDomainProperties(scene, entity, D::PointCloudPoint).Get<glm::vec3>("samples");
    std::ranges::copy(plane, samples.Vector().begin());
    auto config = Config(entity, D::PointCloudPoint);
    const R::EditorProcessingContext context{.Scene = &scene, .World = world, .SpatialIndices = &cache};
    for (const auto backend : {R::PointSpacingBackend::CpuOctree, R::PointSpacingBackend::CpuLBVH})
    {
        SCOPED_TRACE(int(backend));
        config.Backend = backend;
        const auto result = R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(context), config);
        ASSERT_TRUE(result.Succeeded()) << result.Message;
        EXPECT_EQ(result.SlotCount, 5u);
        EXPECT_EQ(result.LiveCount, 4u);
        EXPECT_EQ(result.Centroid, glm::vec3(1.f, 1.5f, 0.f));
        EXPECT_FLOAT_EQ(result.AverageSpacing, 2.f);
        EXPECT_FLOAT_EQ(result.MinSpacing, 2.f);
        EXPECT_FLOAT_EQ(result.MaxSpacing, 2.f);
        EXPECT_FLOAT_EQ(result.BoundingBoxDiagonal, std::sqrt(13.f));
    }
}

TEST(PointSpacingOperations, DeletionMaskValidationPreservesPreviewApplyRejection)
{
    for (unsigned domain = 1; domain <= unsigned(D::PointCloudPoint); ++domain)
    {
        for (unsigned fault = 0; fault < 5; ++fault)
        {
            SCOPED_TRACE(domain);
            SCOPED_TRACE(fault);
            Extrinsic::ECS::Scene::Registry scene;
            const auto entity = Make(scene, D(domain));
            const auto config = Config(entity, D(domain));
            const bool halfedge = D(domain) == D::MeshHalfedge || D(domain) == D::GraphHalfedge;
            if (fault >= 3 && !halfedge) continue;
            auto& input = PointDomainProperties(scene, entity, D(domain));
            auto& masks = halfedge ? scene.Raw().get<GS::Edges>(entity).Properties : input;
            const char* name = halfedge || D(domain) == D::MeshEdge || D(domain) == D::GraphEdge
                ? "e:deleted" : D(domain) == D::MeshFace ? "f:deleted" : "v:deleted";
            if (auto old = masks.Get<bool>(name)) masks.Remove(old);
            if (fault == 0) (void)masks.GetOrAdd<float>(name);
            if (fault == 1) masks.GetOrAdd<bool>(name).Vector().resize(masks.Size() - 1);
            if (fault == 2) masks.GetOrAdd<bool>(name).Vector().resize(masks.Size() + 1);
            if (fault == 3) masks.Resize(masks.Size() + 1);
            if (fault == 4) input.Resize(input.Size() - 1);
            const auto revision = input.Revision();
            R::EditorCommandHistory history;
            const auto commands = R::BindEditorProcessingCommands(
                R::EditorProcessingContext{.Scene = &scene, .CommandHistory = &history});
            const auto preview = R::PreviewEditorPointSpacingCommand(commands, config);
            const auto applied = R::ApplyEditorPointSpacingCommand(commands, config);
            const char* expected = fault < 3
                ? "Deletion mask must be a count-matched bool property."
                : "Invalid deletion domain/cardinality.";
            EXPECT_FALSE(preview.Enabled);
            EXPECT_EQ(preview.DisabledReason, expected);
            EXPECT_EQ(applied.Status, R::EditorCommandStatus::InvalidProcessingParameters);
            EXPECT_EQ(applied.Message, expected);
            EXPECT_FALSE(input.Exists(config.Radii.Name));
            EXPECT_EQ(input.Revision(), revision);
            EXPECT_FALSE(history.CanUndo());
        }
    }
}

TEST(PointSpacingOperations, InputCatalogsShareRevisionMetadataWithoutChangingEligibility)
{
    using Query = R::GeometryPropertyCatalogSnapshot (*)(const R::EditorProcessingCommands&, std::uint32_t);
    const std::array<Query, 3> queries{R::GetEditorPointInputCatalog,
        R::GetEditorPointSpacingInputCatalog, R::GetEditorKernelDensityInputCatalog};
    for (unsigned domain = 1; domain <= unsigned(D::PointCloudPoint); ++domain)
    {
        SCOPED_TRACE(domain);
        Extrinsic::ECS::Scene::Registry scene;
        const auto entity = Make(scene, D(domain));
        auto& props = PointDomainProperties(scene, entity, D(domain));
        const auto config = Config(entity, D(domain));
        R::EditorProcessingContext context{.Scene = &scene};
        const auto commands = R::BindEditorProcessingCommands(context);
        const auto sampleRevision = props.FindPropertyRevision("samples").value();
        const auto before = queries.front()(commands, config.StableEntityId);
        const auto check = [&](const R::GeometryPropertyCatalogSnapshot& catalog, auto revision) {
            EXPECT_EQ(catalog.SourceStableId, config.StableEntityId);
            const auto entry = std::ranges::find_if(catalog.Entries, [&](const auto& row) {
                return row.Ref == config.Positions;
            });
            ASSERT_NE(entry, catalog.Entries.end());
            EXPECT_EQ(entry->PropertyGeneration, revision);
            EXPECT_EQ(entry->ElementCount, props.Size());
            EXPECT_TRUE(std::ranges::all_of(catalog.Entries, [](const auto& row) {
                return row.Ref.ValueKind == Geometry::PropertyValueKind::Vec3;
            }));
        };
        for (auto query : queries)
        {
            const auto catalog = query(commands, config.StableEntityId);
            check(catalog, sampleRevision);
            EXPECT_EQ(catalog.SourceGeneration, before.SourceGeneration);
        }
        props.Get<float>("keep")[0] = 99.f;
        const auto changed = queries.front()(commands, config.StableEntityId);
        EXPECT_NE(changed.SourceGeneration, before.SourceGeneration);
        for (auto query : queries)
        {
            const auto catalog = query(commands, config.StableEntityId);
            check(catalog, sampleRevision);
            EXPECT_EQ(catalog.SourceGeneration, changed.SourceGeneration);
        }
        props.Get<glm::vec3>("samples")[0].x += 0.25f;
        const auto editedRevision = props.FindPropertyRevision("samples").value();
        EXPECT_NE(editedRevision, sampleRevision);
        const auto finalGeneration = queries.front()(commands, config.StableEntityId).SourceGeneration;
        EXPECT_NE(finalGeneration, changed.SourceGeneration);
        const auto propertySetRevision = props.Revision();
        for (auto query : queries)
        {
            const auto catalog = query(commands, config.StableEntityId);
            check(catalog, editedRevision);
            EXPECT_EQ(catalog.SourceGeneration, finalGeneration);
            EXPECT_EQ(props.Revision(), propertySetRevision);
        }
    }
}

TEST(PointSpacingOperations, SingleSampleRemainsDiscoverableOnlyForCompatibleMethods)
{
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = Make(scene, D::PointCloudPoint);
    auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
    props.Resize(1);
    const auto config = Config(entity, D::PointCloudPoint);
    R::EditorProcessingContext context{.Scene = &scene};
    const auto commands = R::BindEditorProcessingCommands(context);
    const auto generic = R::GetEditorPointInputCatalog(commands, config.StableEntityId);
    ASSERT_EQ(generic.Entries.size(), 1u);
    EXPECT_EQ(generic.Entries.front().Ref, config.Positions);
    EXPECT_TRUE(R::GetEditorPointSpacingInputCatalog(commands, config.StableEntityId).Entries.empty());
    EXPECT_TRUE(R::GetEditorKernelDensityInputCatalog(commands, config.StableEntityId).Entries.empty());
}

TEST(PointSpacingOperations, QueuedJobsRejectStaleInputsOutputsAndCancellation)
{
    for(unsigned change=0;change<5;++change)
    {
        SCOPED_TRACE(change);
        Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::MeshVertex);auto config=Config(entity,D::MeshVertex);
        auto& props=PointDomainProperties(scene,entity,D::MeshVertex);
        Intrinsic::Tests::EditorFeatureTestContext context;context.Scene=&scene;
        R::EditorCommandHistory history;context.CommandHistory=&history;
        std::optional<R::EditorPointSpacingResult> delivered;
        const auto resultSink=[&](auto r){delivered=std::move(r);};
        Extrinsic::Tests::EditorJobHarness jobs;jobs.Attach(context);
        ASSERT_EQ(R::ApplyEditorPointSpacingCommand(context, config, resultSink).Status,R::EditorCommandStatus::Pending);
        EXPECT_EQ(R::ApplyEditorPointSpacingCommand(context, config, resultSink).Status,R::EditorCommandStatus::Pending);
        EXPECT_EQ(jobs.Snapshot().Entries.size(),1);
        switch(change)
        {
        case 0:props.Get<float>("keep")[0]=99;break;
        case 1:props.Get<glm::vec3>("samples")[0].x+=1;break;
        case 2:props.GetOrAdd<bool>("v:deleted")[0]=true;break;
        case 3:props.GetOrAdd<float>("radii")[0]=77;break;
        case 4:(void)jobs.Jobs().Cancel(jobs.Snapshot().Entries[0].Token);break;
        }
        ASSERT_TRUE(jobs.DrainUntilTerminal());ASSERT_TRUE(delivered);
        EXPECT_EQ(delivered->Succeeded(),change==0)<<delivered->Message;
        EXPECT_EQ(history.CanUndo(),change==0);
        EXPECT_EQ(props.Exists("radii"),change==0 || change==3);
    }
}
TEST(PointSpacingConfig, RoundTripAndSharedPreviewApplyRun)
{
    namespace C = Extrinsic::Core::Config;
    Extrinsic::ECS::Scene::Registry scene;
    auto entity = Make(scene, D::MeshFace);
    auto config = Config(entity, D::MeshFace);
    config.ScaleFactor = 5;
    C::EngineConfigSectionRegistry registry;
    ASSERT_TRUE(registry.Register(R::MakePointSpacingConfigSectionRegistration()));
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
    ASSERT_TRUE(R::PreviewEditorPointSpacingCommand(commands, config).Enabled);
    EXPECT_FALSE(PointDomainProperties(scene, entity, D::MeshFace).Exists("radii"));
    ASSERT_TRUE(R::ApplyEditorPointSpacingConfig(commands, config).Succeeded());
    ASSERT_TRUE(R::GetEditorPointSpacingConfig(commands));
    EXPECT_EQ(R::SerializePointSpacingConfig(*R::GetEditorPointSpacingConfig(commands)),
              R::SerializePointSpacingConfig(config));
    ASSERT_TRUE(R::ApplyEditorConfiguredPointSpacing(commands).Succeeded());
    EXPECT_EQ(previews, 1);
    EXPECT_EQ(applies, 1);
    for (auto payload : {R"({"backend":"vulkan"})", R"({"k_neighbors":-1})", R"({"scale_factor":-1})",
                         R"({"gpu_query_batch_size":0})", R"({"scale_factor":1e100})", R"({"scale_factor":1e-100})", R"({"unknown":1})",
                         R"({"radii":{"domain":"unknown","name":"x","kind":"uint32"}})"})
        EXPECT_FALSE(R::ValidatePointSpacingConfigSection(payload, {}, "test").Usable()) << payload;
    config.ScaleFactor = -1;
    EXPECT_FALSE(R::ApplyEditorPointSpacingConfig(commands, config).Succeeded());
    EXPECT_EQ(applies, 1);
}

TEST(PointSpacingOperations, EveryDomainPublishesNamedRadiiAndPreservesDeletedRowsWithHistory)
{
    for(unsigned d=1;d<=8;++d) for(float scale : {0.f,2.f}) for(unsigned k : {1u, 2u, 63u})
    {
        SCOPED_TRACE(d);
        SCOPED_TRACE(scale);
        SCOPED_TRACE(k);
        R::WorldRegistry worlds;auto world=worlds.CreateWorld("radii");auto& scene=*worlds.Get(world);
        R::SpatialIndexCache cache(worlds);auto entity=Make(scene,D(d));auto config=Config(entity,D(d));config.ScaleFactor=scale;config.KNeighbors=k;
        auto& props=PointDomainProperties(scene,entity,D(d));
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
        props.GetOrAdd<float>("radii").Vector().assign(size,77);
        const auto positionRevision=std::as_const(props).Get<glm::vec3>("samples").Revision();
        R::EditorCommandHistory history;
        R::EditorProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
        const auto catalog=R::GetEditorPointSpacingInputCatalog(R::BindEditorProcessingCommands(context), config.StableEntityId);
        EXPECT_TRUE(std::ranges::any_of(catalog.Entries,[&](auto& e){return e.Ref==config.Positions;}));
        ASSERT_TRUE(R::PreviewEditorPointSpacingCommand(R::BindEditorProcessingCommands(context), config).Enabled);
        const auto reference=R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(context), config);
        ASSERT_TRUE(reference.Succeeded())<<reference.Message;EXPECT_EQ(reference.ActualBackend,"cpu_octree");
        const auto values=std::as_const(props).Get<float>("radii").Vector();
        EXPECT_EQ(values[2],77);if(half)EXPECT_EQ(values[3],77);
        EXPECT_EQ(props.Size(),size);EXPECT_EQ(std::as_const(props).Get<glm::vec3>("samples").Revision(),positionRevision);
        props.Get<float>("keep")[0]=99;
        ASSERT_TRUE(history.Undo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("radii")[0],77);
        ASSERT_TRUE(history.Redo().Succeeded());EXPECT_EQ(std::as_const(props).Get<float>("keep")[0],99);
        config.Backend=R::PointSpacingBackend::CpuLBVH;
        const auto indexed=R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(context), config);
        ASSERT_TRUE(indexed.Succeeded())<<indexed.Message;EXPECT_EQ(indexed.ActualBackend,"cpu_lbvh");
        const auto actual=std::as_const(props).Get<float>("radii");
        for(std::size_t i=0;i<size;++i)EXPECT_NEAR(actual[i],values[i],1e-5*std::max(1.f,values[i]));
        EXPECT_FLOAT_EQ(indexed.AverageSpacing,reference.AverageSpacing);
        EXPECT_FLOAT_EQ(indexed.MinSpacing,reference.MinSpacing);
        EXPECT_FLOAT_EQ(indexed.MaxSpacing,reference.MaxSpacing);
        EXPECT_EQ(indexed.Centroid,reference.Centroid);
        EXPECT_FLOAT_EQ(indexed.BoundingBoxDiagonal,reference.BoundingBoxDiagonal);
        EXPECT_FLOAT_EQ(indexed.MeanRadius,reference.MeanRadius);
        EXPECT_TRUE(R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(context), config).IndexReused);
        Intrinsic::Tests::EditorFeatureTestContext visualization;visualization.Scene=&scene;visualization.VisualizationCommandsAvailable=true;
        std::optional<R::VisualizationRecipe> stored;
        visualization.VisualizationRecipes.GetRecipe=[&](std::uint32_t){return stored;};
        visualization.VisualizationRecipes.SetRecipe=[&](std::uint32_t,R::VisualizationRecipe r){stored=std::move(r);};
        visualization.VisualizationRecipes.ClearRecipe=[&](std::uint32_t){stored.reset();};
        const auto shown=R::ApplyEditorVisualizationRecipeCommand(visualization,{.StableEntityId=config.StableEntityId,
            .Recipe={.Data=R::ScalarVisualizationRecipe{.Source=config.Radii,.OutputName="radii.colors"}}});

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
            EXPECT_EQ(lane->ScalarFieldName,config.Radii.Name);
        }
        props.Get<float>("radii")[0]=123;
        EXPECT_FALSE(history.Undo().Succeeded());
    }
}
TEST(PointSpacingOperations, InvalidUnsupportedAndNumericalFailuresRetainOutput)
{
    Extrinsic::ECS::Scene::Registry scene;auto entity=Make(scene,D::PointCloudPoint);auto config=Config(entity,D::PointCloudPoint);
    R::EditorProcessingContext context{.Scene=&scene};
    auto& props=PointDomainProperties(scene,entity,D::PointCloudPoint);props.GetOrAdd<float>("radii").Vector().assign(props.Size(),77);
    for(const char* name:{"v:deleted","samples"})
    {auto bad=config;bad.Radii.Name=name;EXPECT_FALSE(R::PreviewEditorPointSpacingCommand(R::BindEditorProcessingCommands(context), bad).Enabled);}
    auto unrelatedName=config;unrelatedName.Radii.Name="h:next";
    EXPECT_TRUE(R::PreviewEditorPointSpacingCommand(R::BindEditorProcessingCommands(context),unrelatedName).Enabled);
    config.Backend=R::PointSpacingBackend::VulkanLBVH;
    EXPECT_FALSE(R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(context), config).Succeeded());
    config.Backend=R::PointSpacingBackend::CpuOctree;config.ScaleFactor=std::numeric_limits<float>::max();
    EXPECT_FALSE(R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(context), config).Succeeded());
    EXPECT_EQ(std::as_const(props).Get<float>("radii")[0],77);
    config.ScaleFactor=1;config.KNeighbors=std::numeric_limits<std::uint32_t>::max();
    EXPECT_TRUE(R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(context), config).Succeeded());
}
TEST(PointSpacingOperations, NewOutputUndoAndPositionEditsRebuildTheCache)
{
    R::WorldRegistry worlds;auto world=worlds.CreateWorld("radii");auto& scene=*worlds.Get(world);
    R::SpatialIndexCache cache(worlds);auto entity=Make(scene,D::PointCloudPoint);auto config=Config(entity,D::PointCloudPoint);
    config.Backend=R::PointSpacingBackend::CpuLBVH;config.KNeighbors=std::numeric_limits<std::uint32_t>::max();
    R::EditorCommandHistory history;
    R::EditorProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history,.SpatialIndices=&cache};
    auto& props=PointDomainProperties(scene,entity,D::PointCloudPoint);
    ASSERT_TRUE(R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(context), config).Succeeded());
    ASSERT_TRUE(history.Undo().Succeeded());EXPECT_FALSE(props.Exists("radii"));
    ASSERT_TRUE(history.Redo().Succeeded());EXPECT_TRUE(props.Exists("radii"));
    props.Get<glm::vec3>("samples")[0].x+=.1f;
    const auto rerun=R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(context), config);
    ASSERT_TRUE(rerun.Succeeded())<<rerun.Message;EXPECT_FALSE(rerun.IndexReused);
}

TEST(PointSpacingOperations, ReplacedStorageRejectsUndo)
{
    for (const bool replaceOutput : {false, true})
    {
        Extrinsic::ECS::Scene::Registry scene;
        auto entity = Make(scene, D::PointCloudPoint);
        auto config = Config(entity, D::PointCloudPoint);
        auto& props = PointDomainProperties(scene, entity, D::PointCloudPoint);
        R::EditorCommandHistory history;
        R::EditorProcessingContext context{.Scene=&scene, .CommandHistory=&history};
        ASSERT_TRUE(R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(context), config).Succeeded());
        const auto computed = std::as_const(props).Get<float>("radii").Vector();
        if (replaceOutput)
        {
            auto oldOutput = props.Get<float>("radii");
            props.Remove(oldOutput);
            props.GetOrAdd<glm::vec3>("radii").Vector().assign(props.Size(), glm::vec3(17));
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
            EXPECT_EQ(std::as_const(props).Get<glm::vec3>("radii")[0], glm::vec3(17));
        else EXPECT_EQ(std::as_const(props).Get<float>("radii").Vector(), computed);
    }
}

TEST(PointSpacingOperations, ExpiredAttachmentRejectsQueuedPublicationAndDelivery)
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
    ASSERT_EQ(R::ApplyEditorPointSpacingCommand(commands, config,
        [&](R::EditorPointSpacingResult) { ++deliveries; }).Status, R::EditorCommandStatus::Pending);
    active = false;
    scene.reset();
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    EXPECT_EQ(deliveries, 0u);
    EXPECT_FALSE(history.CanUndo());
    EXPECT_FALSE(commands.IsBound());
}

TEST(PointSpacingOperations, ScalarStorageChoicesRoundTripAndUndo)
{
    using K = Geometry::PropertyValueKind;
    for (const auto kind : {K::Bool,K::Int32,K::UInt32,K::UInt64,K::Float,K::Double})
    {
        SCOPED_TRACE(int(kind));
        R::WorldRegistry worlds;
        const auto world=worlds.CreateWorld("scalar outputs");
        auto& scene=*worlds.Get(world);
        const auto entity=Make(scene,D::PointCloudPoint);
        auto& props=PointDomainProperties(scene,entity,D::PointCloudPoint);
        auto samples=props.Get<glm::vec3>("samples");
        for(unsigned i=0;i<4;++i)samples[i]={float(i),0,0};
        auto config=Config(entity,D::PointCloudPoint);
        config.KNeighbors=1;
        config.ScaleFactor=1;
        config.Radii.ValueKind=kind;
        const auto payload=R::SerializePointSpacingConfig(config);
        EXPECT_TRUE(R::ValidatePointSpacingConfigSection(payload,{},R::kPointSpacingConfigSectionName).Usable());
        Extrinsic::Core::Config::EngineConfig document;
        R::SetPointSpacingConfig(document,config);
        const auto decoded=R::GetPointSpacingConfig(document);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded->Radii,config.Radii);
        R::EditorCommandHistory history;
        const R::EditorProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history};
        const auto result=R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(context),config);
        ASSERT_TRUE(result.Succeeded())<<result.Message;
        EXPECT_EQ(R::DetectGeometryPropertyValueKind(props,"radii"),kind);
        EXPECT_EQ(history.Undo().Status,R::EditorCommandHistoryStatus::Undone);
        EXPECT_FALSE(props.Exists("radii"));
        EXPECT_EQ(history.Redo().Status,R::EditorCommandHistoryStatus::Redone);
    }
}

TEST(PointSpacingOperations, InexactIntegerOutputFailsWithoutCreatingPropertyOrHistory)
{
    R::WorldRegistry worlds;
    const auto world=worlds.CreateWorld("inexact output");
    auto& scene=*worlds.Get(world);
    const auto entity=Make(scene,D::PointCloudPoint);
    auto config=Config(entity,D::PointCloudPoint);
    config.Radii.ValueKind=Geometry::PropertyValueKind::Int32;
    R::EditorCommandHistory history;
    const R::EditorProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history};
    EXPECT_FALSE(R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(context),config).Succeeded());
    EXPECT_FALSE(PointDomainProperties(scene,entity,D::PointCloudPoint).Exists("radii"));
    EXPECT_EQ(history.Undo().Status,R::EditorCommandHistoryStatus::EmptyUndoStack);
}

TEST(PointSpacingOperations, DoubleOutputUndoPreservesDeletedNanAndUnrelatedEdits)
{
    R::WorldRegistry worlds;
    const auto world=worlds.CreateWorld("exact history");
    auto& scene=*worlds.Get(world);
    const auto entity=Make(scene,D::PointCloudPoint);
    auto& props=PointDomainProperties(scene,entity,D::PointCloudPoint);
    props.GetOrAdd<double>("radii").Vector().assign(props.Size(),77.0);
    props.Get<double>("radii")[4]=std::numeric_limits<double>::quiet_NaN();
    auto config=Config(entity,D::PointCloudPoint);
    config.Radii.ValueKind=Geometry::PropertyValueKind::Double;
    R::EditorCommandHistory history;
    const R::EditorProcessingContext context{.Scene=&scene,.World=world,.CommandHistory=&history};
    const auto result=R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(context),config);
    ASSERT_TRUE(result.Succeeded())<<result.Message;
    EXPECT_TRUE(std::isnan(std::as_const(props).Get<double>("radii")[4]));
    props.Get<float>("keep")[0]=99;
    ASSERT_TRUE(history.Undo().Succeeded());
    EXPECT_EQ(std::as_const(props).Get<double>("radii")[0],77);
    EXPECT_TRUE(std::isnan(std::as_const(props).Get<double>("radii")[4]));
    ASSERT_TRUE(history.Redo().Succeeded());
    EXPECT_EQ(std::as_const(props).Get<float>("keep")[0],99);
    EXPECT_TRUE(std::isnan(std::as_const(props).Get<double>("radii")[4]));
}
