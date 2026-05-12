#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.ReferenceScene;

using Extrinsic::Core::Config::ReferenceSceneSelector;
using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Graphics::CameraViewInput;
using Extrinsic::Runtime::IReferenceSceneProvider;
using Extrinsic::Runtime::MakeDefaultReferenceSceneRegistry;
using Extrinsic::Runtime::ReferenceSceneEntity;
using Extrinsic::Runtime::ReferenceScenePopulation;
using Extrinsic::Runtime::ReferenceSceneRegistry;

namespace
{
    class TrackingProvider final : public IReferenceSceneProvider
    {
    public:
        ReferenceScenePopulation Populate(Registry& scene) override
        {
            ++PopulateCalls;
            ReferenceScenePopulation pop;
            const EntityHandle entity = scene.Create();
            CreatedEntity = entity;
            pop.Entities.push_back(ReferenceSceneEntity{entity});
            CameraViewInput cam{};
            cam.Valid = true;
            pop.Camera = cam;
            return pop;
        }

        void Teardown(Registry& scene,
                      const std::vector<ReferenceSceneEntity>& entities) override
        {
            ++TeardownCalls;
            for (const auto& e : entities)
            {
                if (scene.IsValid(e.Entity))
                    scene.Destroy(e.Entity);
            }
        }

        int PopulateCalls{0};
        int TeardownCalls{0};
        EntityHandle CreatedEntity{Extrinsic::ECS::InvalidEntityHandle};
    };
}

TEST(ReferenceSceneRegistry, DefaultProviderReturnsEmptyPopulation)
{
    ReferenceSceneRegistry registry = MakeDefaultReferenceSceneRegistry();
    Registry scene;

    IReferenceSceneProvider& provider = registry.Resolve(ReferenceSceneSelector::Triangle);
    const auto population = provider.Populate(scene);

    EXPECT_TRUE(population.Entities.empty());
    EXPECT_FALSE(population.Camera.has_value());
    EXPECT_EQ(scene.Raw().storage<EntityHandle>().size(), 0u);
}

TEST(ReferenceSceneRegistry, ResolveOnUnknownSelectorReturnsNullptrButResolveFallsBack)
{
    ReferenceSceneRegistry registry = MakeDefaultReferenceSceneRegistry();

    EXPECT_EQ(registry.ResolveOrNull(ReferenceSceneSelector::Triangle), nullptr);
    // GRAPHICS-029A transitional behavior: Resolve falls back to the no-op
    // default. GRAPHICS-029B will tighten this to std::terminate once
    // TriangleProvider is registered for ReferenceSceneSelector::Triangle.
    IReferenceSceneProvider& provider = registry.Resolve(ReferenceSceneSelector::Triangle);
    Registry scene;
    EXPECT_TRUE(provider.Populate(scene).Entities.empty());
}

TEST(ReferenceSceneRegistry, RegisterFollowedByResolveReturnsTheRegisteredProvider)
{
    ReferenceSceneRegistry registry = MakeDefaultReferenceSceneRegistry();
    auto provider = std::make_unique<TrackingProvider>();
    TrackingProvider* providerRaw = provider.get();

    registry.Register(ReferenceSceneSelector::Triangle, std::move(provider));

    EXPECT_EQ(registry.ResolveOrNull(ReferenceSceneSelector::Triangle), providerRaw);
    EXPECT_EQ(&registry.Resolve(ReferenceSceneSelector::Triangle), providerRaw);

    Registry scene;
    auto population = registry.Resolve(ReferenceSceneSelector::Triangle).Populate(scene);
    EXPECT_EQ(providerRaw->PopulateCalls, 1);
    ASSERT_EQ(population.Entities.size(), 1u);
    EXPECT_TRUE(scene.IsValid(population.Entities.front().Entity));
}

TEST(ReferenceSceneRegistry, DoubleRegistrationFiresTerminateGuard)
{
    ReferenceSceneRegistry registry = MakeDefaultReferenceSceneRegistry();
    registry.Register(ReferenceSceneSelector::Triangle,
                      std::make_unique<TrackingProvider>());

    EXPECT_DEATH(
        registry.Register(ReferenceSceneSelector::Triangle,
                          std::make_unique<TrackingProvider>()),
        "");
}

TEST(ReferenceSceneRegistry, NullProviderRegistrationFiresTerminateGuard)
{
    ReferenceSceneRegistry registry = MakeDefaultReferenceSceneRegistry();
    EXPECT_DEATH(
        registry.Register(ReferenceSceneSelector::Triangle, nullptr),
        "");
}

namespace
{
    class StubApplication final : public Extrinsic::Runtime::IApplication
    {
    public:
        void OnInitialize(Extrinsic::Runtime::Engine& /*engine*/) override {}
        void OnSimTick(Extrinsic::Runtime::Engine& /*engine*/, double /*fixedDt*/) override {}
        void OnVariableTick(Extrinsic::Runtime::Engine& /*engine*/,
                            double /*alpha*/,
                            double /*dt*/) override {}
        void OnShutdown(Extrinsic::Runtime::Engine& /*engine*/) override {}
    };
}

TEST(EngineReferenceScene, DefaultEngineConfigKeepsReferenceSceneDisabledAndCreatesNoEntities)
{
    Extrinsic::Core::Config::EngineConfig config{};
    ASSERT_FALSE(config.ReferenceScene.Enabled);

    Extrinsic::Runtime::Engine engine(config, std::make_unique<StubApplication>());
    engine.Initialize();

    EXPECT_FALSE(engine.IsReferenceSceneInstalled());
    EXPECT_EQ(engine.GetScene().Raw().storage<EntityHandle>().size(), 0u);

    engine.Shutdown();
    EXPECT_FALSE(engine.IsReferenceSceneInstalled());
}

TEST(EngineReferenceScene, ReferenceEngineConfigInvokesResolvedProviderOnce)
{
    Extrinsic::Core::Config::EngineConfig config =
        []() {
            auto c = Extrinsic::Core::Config::EngineConfig{};
            c.ReferenceScene.Enabled = true;
            c.ReferenceScene.Selector = ReferenceSceneSelector::Triangle;
            return c;
        }();

    Extrinsic::Runtime::Engine engine(config, std::make_unique<StubApplication>());

    auto provider = std::make_unique<TrackingProvider>();
    TrackingProvider* providerRaw = provider.get();
    engine.GetReferenceSceneRegistry().Register(
        ReferenceSceneSelector::Triangle, std::move(provider));

    engine.Initialize();

    EXPECT_TRUE(engine.IsReferenceSceneInstalled());
    EXPECT_EQ(providerRaw->PopulateCalls, 1);
    EXPECT_EQ(providerRaw->TeardownCalls, 0);
    EXPECT_EQ(engine.GetScene().Raw().storage<EntityHandle>().size(), 1u);

    engine.Shutdown();

    EXPECT_FALSE(engine.IsReferenceSceneInstalled());
    EXPECT_EQ(providerRaw->TeardownCalls, 1);
}

TEST(EngineReferenceScene, ReferenceEnabledWithNoExplicitProviderFallsBackToNoopAndCreatesNoEntities)
{
    Extrinsic::Core::Config::EngineConfig config{};
    config.ReferenceScene.Enabled = true;
    config.ReferenceScene.Selector = ReferenceSceneSelector::Triangle;

    Extrinsic::Runtime::Engine engine(config, std::make_unique<StubApplication>());
    engine.Initialize();

    EXPECT_TRUE(engine.IsReferenceSceneInstalled());
    EXPECT_EQ(engine.GetScene().Raw().storage<EntityHandle>().size(), 0u);

    engine.Shutdown();
    EXPECT_FALSE(engine.IsReferenceSceneInstalled());
}
