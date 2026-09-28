// RUNTIME-274: standalone point sampling through the editor command.
#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
import Extrinsic.Runtime.PointSamplingOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Hierarchy;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace T = Extrinsic::ECS::Components::Transform;
using D = R::GeometryElementDomain;
namespace
{
    struct Scene
    {
        R::WorldRegistry Worlds;
        R::WorldHandle World{Worlds.CreateWorld("sampling")};
        Extrinsic::ECS::Scene::Registry& Registry{*Worlds.Get(World)};
        R::EditorCommandHistory History;
        R::EditorProcessingContext Context{.Scene = &Registry, .World = World, .CommandHistory = &History};
        [[nodiscard]] R::EditorProcessingCommands Commands() { return R::BindEditorProcessingCommands(Context); }
    };

    entt::entity MakeCloud(Scene& s, std::size_t count)
    {
        const auto entity = s.Registry.Create();
        s.Registry.Raw().emplace<T::Component>(entity);
        auto& vertices = s.Registry.Raw().emplace<GS::Vertices>(entity).Properties;
        vertices.Resize(count);
        std::mt19937 random(3u);
        std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
        auto positions = vertices.GetOrAdd<glm::vec3>("v:position");
        for (auto& p : positions.Vector()) p = {uniform(random), uniform(random), 0.5f * uniform(random)};
        return entity;
    }

    std::uint32_t Id(entt::entity entity) { return R::SelectionController::ToStableEntityId(entity); }
}

TEST(PointSamplingOperations, PublishesRankAndSelectionPropertiesUndoably)
{
    Scene s;
    const auto cloud = MakeCloud(s, 500);
    R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 64};
    ASSERT_TRUE(R::PreviewEditorPointSamplingCommand(s.Commands(), config).Enabled);
    const auto result = R::ApplyEditorPointSamplingCommand(s.Commands(), config);
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.SampleCount, 64u);
    EXPECT_EQ(result.InputCount, 500u);
    EXPECT_GT(result.DistancePairs, 0u) << "farthest point by default";
    const auto& vertices = std::as_const(s.Registry.Raw().get<GS::Vertices>(cloud).Properties);
    const auto rank = vertices.Get<float>("v:sample_rank");
    const auto selected = vertices.Get<bool>("v:sample_selected");
    ASSERT_TRUE(rank && selected);
    EXPECT_EQ(std::count(selected.Vector().begin(), selected.Vector().end(), true), 64);
    EXPECT_EQ(std::count_if(rank.Vector().begin(), rank.Vector().end(), [](float r) { return r >= 0.0f; }), 64);
    ASSERT_TRUE(s.History.Undo().Succeeded());
    EXPECT_FALSE(vertices.Exists("v:sample_rank"));
    EXPECT_FALSE(vertices.Exists("v:sample_selected"));
}

TEST(PointSamplingOperations, CreatesAPointCloudWithEveryMethod)
{
    Scene s;
    const auto cloud = MakeCloud(s, 400);
    for (const auto method : {R::PointSamplingMethod::Random, R::PointSamplingMethod::FarthestPoint,
                              R::PointSamplingMethod::ProgressivePoisson, R::PointSamplingMethod::CoupledSieve,
                              R::PointSamplingMethod::FlatGreedy, R::PointSamplingMethod::SampleElimination,
                              R::PointSamplingMethod::LazyGreedy, R::PointSamplingMethod::Tournament})
    {
        R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 50,
                                               .Output = R::PointSamplingOutput::PointCloud};
        config.Sampling.Method = method;
        const auto result = R::ApplyEditorPointSamplingCommand(s.Commands(), config);
        ASSERT_TRUE(result.Succeeded()) << result.Message << " (" << result.Method << ")";
        EXPECT_GT(result.OutputEntityId, 0u);
        EXPECT_LE(result.SampleCount, 50u);
        EXPECT_GT(result.SampleCount, 0u);
        ASSERT_TRUE(s.History.Undo().Succeeded()) << result.Method;
    }
}

TEST(PointSamplingOperations, WeightsAndPriorityScoresComeFromAFloatProperty)
{
    Scene s;
    const auto cloud = MakeCloud(s, 300);
    auto& vertices = s.Registry.Raw().get<GS::Vertices>(cloud).Properties;
    auto weights = vertices.GetOrAdd<float>("v:importance");
    for (std::size_t i = 0; i < weights.Vector().size(); ++i) weights.Vector()[i] = 1.0f + float(i % 7);
    R::PointSamplingOperationConfig config{.SourceStableEntityId = Id(cloud), .Count = 40, .WeightsName = "v:importance"};
    EXPECT_TRUE(R::ApplyEditorPointSamplingCommand(s.Commands(), config).Succeeded());
    config.Sampling.Method = R::PointSamplingMethod::ProgressivePoisson;
    config.Sampling.PoissonSelection = R::PointSamplingPoissonSelection::FeaturePriority;
    EXPECT_TRUE(R::ApplyEditorPointSamplingCommand(s.Commands(), config).Succeeded());
    config.WeightsName = "v:missing";
    EXPECT_FALSE(R::PreviewEditorPointSamplingCommand(s.Commands(), config).Enabled);
    config.WeightsName.clear();
    EXPECT_FALSE(R::PreviewEditorPointSamplingCommand(s.Commands(), config).Enabled) << "priority needs scores";
}

TEST(PointSamplingOperations, ConfigRoundTripsAndRejectsUnusableSettings)
{
    R::PointSamplingOperationConfig config{.Count = 0, .Output = R::PointSamplingOutput::PointCloud};
    config.Sampling.Method = R::PointSamplingMethod::FlatGreedy;
    config.Sampling.Beta = 1.4;
    const auto decoded = R::DecodePointSamplingOperationConfig(R::SerializePointSamplingOperationConfig(config));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->Sampling.Method, R::PointSamplingMethod::FlatGreedy);
    EXPECT_EQ(decoded->Sampling.Beta, 1.4);
    EXPECT_EQ(decoded->Output, R::PointSamplingOutput::PointCloud);
    EXPECT_EQ(decoded->Count, 0u);
    const auto invalid = [](const R::PointSamplingOperationConfig& c) {
        return !R::ValidatePointSamplingOperationConfigSection(R::SerializePointSamplingOperationConfig(c), {}, "test").Usable();
    };
    EXPECT_TRUE(invalid({.RankName = "v:sample_selected"}));
    R::PointSamplingOperationConfig exact;
    exact.Sampling = {.Method = R::PointSamplingMethod::CoupledSieve, .Eta = 1.0, .CandidateCap = 3};
    EXPECT_TRUE(invalid(exact));
    Scene s;
    const auto cloud = MakeCloud(s, 20);
    const auto parent = s.Registry.Create();
    s.Registry.Raw().emplace<Extrinsic::ECS::Components::Hierarchy::Component>(cloud).Parent = parent;
    // Topology names are refused once the domain is resolved (here: point-cloud points).
    const auto plain = MakeCloud(s, 20);
    EXPECT_FALSE(R::PreviewEditorPointSamplingCommand(s.Commands(), {.SourceStableEntityId = Id(plain),
                                                                     .RankName = "v:deleted"}).Enabled);
    EXPECT_FALSE(R::PreviewEditorPointSamplingCommand(s.Commands(), {.SourceStableEntityId = Id(cloud)}).Enabled);
}
