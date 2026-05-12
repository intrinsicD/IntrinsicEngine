#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.RenderExtraction;

using Extrinsic::Core::Config::ReferenceSceneSelector;
using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Graphics::BuildCameraViewSnapshot;
using Extrinsic::Graphics::CameraViewInput;
using Extrinsic::Graphics::CameraViewSnapshot;
using Extrinsic::Runtime::BuildReferenceCameraViewInput;
using Extrinsic::Runtime::IReferenceSceneProvider;
using Extrinsic::Runtime::MakeDefaultReferenceSceneRegistry;
using Extrinsic::Runtime::ReferenceSceneEntity;
using Extrinsic::Runtime::ReferenceScenePopulation;
using Extrinsic::Runtime::ReferenceSceneRegistry;
using Extrinsic::Runtime::RegisterDefaultReferenceProvidersIfAbsent;
using Extrinsic::Runtime::TriangleProvider;

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

TEST(ReferenceSceneRegistry, ResolveOnUnregisteredSelectorFiresTerminateGuard)
{
    // GRAPHICS-029B closing cleanup from GRAPHICS-029A's Transition notice:
    // selectors with no registered provider now std::terminate at Resolve()
    // instead of falling back to the GRAPHICS-029A no-op default. The
    // production resolve path is gated through
    // RegisterDefaultReferenceProvidersIfAbsent so engines never reach this
    // branch unintentionally.
    ReferenceSceneRegistry registry;
    EXPECT_EQ(registry.ResolveOrNull(ReferenceSceneSelector::Triangle), nullptr);
    EXPECT_DEATH(
        (void)registry.Resolve(ReferenceSceneSelector::Triangle),
        "");
}

TEST(ReferenceSceneRegistry, MakeDefaultReferenceSceneRegistryRegistersTriangleProvider)
{
    namespace G = Extrinsic::Graphics::Components;
    namespace E = Extrinsic::ECS::Components;

    ReferenceSceneRegistry registry = MakeDefaultReferenceSceneRegistry();

    IReferenceSceneProvider* provider =
        registry.ResolveOrNull(ReferenceSceneSelector::Triangle);
    ASSERT_NE(provider, nullptr);

    // Identify the registered provider by behavior (the build uses
    // -fno-rtti so dynamic_cast is unavailable): Populate must emit the
    // canonical TriangleProvider entity contract.
    Registry scene;
    const ReferenceScenePopulation population = provider->Populate(scene);
    ASSERT_EQ(population.Entities.size(), 1u);
    const EntityHandle entity = population.Entities.front().Entity;
    ASSERT_TRUE(scene.IsValid(entity));
    auto& raw = scene.Raw();
    ASSERT_TRUE(raw.all_of<E::MetaData>(entity));
    EXPECT_EQ(raw.get<E::MetaData>(entity).EntityName, "ReferenceTriangle");
    EXPECT_TRUE((raw.all_of<G::RenderSurface, E::ProceduralGeometryRef>(entity)));
}

TEST(ReferenceSceneRegistry, RegisterFollowedByResolveReturnsTheRegisteredProvider)
{
    ReferenceSceneRegistry registry;
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
    ReferenceSceneRegistry registry;
    registry.Register(ReferenceSceneSelector::Triangle,
                      std::make_unique<TrackingProvider>());

    EXPECT_DEATH(
        registry.Register(ReferenceSceneSelector::Triangle,
                          std::make_unique<TrackingProvider>()),
        "");
}

TEST(ReferenceSceneRegistry, NullProviderRegistrationFiresTerminateGuard)
{
    ReferenceSceneRegistry registry;
    EXPECT_DEATH(
        registry.Register(ReferenceSceneSelector::Triangle, nullptr),
        "");
}

TEST(ReferenceSceneRegistry, RegisterDefaultsIfAbsentPreservesPreRegisteredProviders)
{
    ReferenceSceneRegistry registry;
    auto provider = std::make_unique<TrackingProvider>();
    TrackingProvider* providerRaw = provider.get();
    registry.Register(ReferenceSceneSelector::Triangle, std::move(provider));

    RegisterDefaultReferenceProvidersIfAbsent(registry);
    EXPECT_EQ(registry.ResolveOrNull(ReferenceSceneSelector::Triangle), providerRaw);
}

TEST(TriangleProviderContract, PopulateCreatesNamedTriangleEntityWithExpectedComponents)
{
    namespace G = Extrinsic::Graphics::Components;
    namespace E = Extrinsic::ECS::Components;

    Registry scene;
    TriangleProvider provider;
    const ReferenceScenePopulation population = provider.Populate(scene);

    ASSERT_EQ(population.Entities.size(), 1u);
    const EntityHandle entity = population.Entities.front().Entity;
    ASSERT_TRUE(scene.IsValid(entity));

    auto& raw = scene.Raw();
    ASSERT_TRUE(raw.all_of<E::MetaData>(entity));
    EXPECT_EQ(raw.get<E::MetaData>(entity).EntityName, "ReferenceTriangle");

    EXPECT_TRUE((raw.all_of<E::Transform::Component,
                            E::Transform::WorldMatrix,
                            E::Hierarchy::Component>(entity)));

    ASSERT_TRUE(raw.all_of<G::RenderSurface>(entity));
    EXPECT_EQ(raw.get<G::RenderSurface>(entity).Domain,
              G::RenderSurface::SourceDomain::Vertex);

    ASSERT_TRUE(raw.all_of<E::ProceduralGeometryRef>(entity));
    EXPECT_EQ(raw.get<E::ProceduralGeometryRef>(entity).Kind,
              E::ProceduralGeometryKind::Triangle);

    ASSERT_TRUE(population.Camera.has_value());
    const CameraViewInput& seed = *population.Camera;
    EXPECT_TRUE(seed.Valid);
    EXPECT_EQ(seed.Position, glm::vec3(0.0f, 0.0f, 3.0f));
    EXPECT_EQ(seed.Forward, glm::vec3(0.0f, 0.0f, -1.0f));
    EXPECT_EQ(seed.Up, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_FLOAT_EQ(seed.NearPlane, 0.1f);
    EXPECT_FLOAT_EQ(seed.FarPlane, 100.0f);
}

TEST(TriangleProviderContract, TeardownDestroysAuthoredEntities)
{
    Registry scene;
    TriangleProvider provider;
    ReferenceScenePopulation population = provider.Populate(scene);

    ASSERT_EQ(population.Entities.size(), 1u);
    const EntityHandle entity = population.Entities.front().Entity;
    ASSERT_TRUE(scene.IsValid(entity));

    provider.Teardown(scene, population.Entities);
    EXPECT_FALSE(scene.IsValid(entity));
}

TEST(ReferenceCameraBuildInput, ProducesSanitizerValidSnapshotWithViewportAspect)
{
    Registry scene;
    TriangleProvider provider;
    const ReferenceScenePopulation population = provider.Populate(scene);
    ASSERT_TRUE(population.Camera.has_value());

    const int viewportWidth = 1600;
    const int viewportHeight = 900;
    const CameraViewInput finalized = BuildReferenceCameraViewInput(*population.Camera,
                                                                     viewportWidth,
                                                                     viewportHeight);
    EXPECT_TRUE(finalized.Valid);

    // Vulkan clip-space Y inversion: glm::perspective produces a positive
    // Projection[1][1]; BuildReferenceCameraViewInput must flip it negative
    // for parity with the legacy CameraComponent path
    // (src/legacy/Graphics/Graphics.Camera.cpp:34-39). Without the flip, the
    // promoted renderer would render the reference triangle vertically
    // inverted.
    EXPECT_LT(finalized.Projection[1][1], 0.0f);

    // Aspect baked into the perspective matrix matches the viewport. Using
    // glm::perspective(fovY, aspect, n, f) with the Vulkan Y flip:
    // Projection[1][1] = -1 / tan(fovY/2), Projection[0][0] = (1 / tan(fovY/2)) / aspect,
    // so |Projection[1][1] / Projection[0][0]| == aspect.
    ASSERT_NE(finalized.Projection[0][0], 0.0f);
    const float derivedAspect = std::abs(finalized.Projection[1][1] /
                                         finalized.Projection[0][0]);
    const float expectedAspect = static_cast<float>(viewportWidth) /
                                  static_cast<float>(viewportHeight);
    EXPECT_NEAR(derivedAspect, expectedAspect, 1e-4f);

    const Extrinsic::Platform::Extent2D viewport{viewportWidth, viewportHeight};
    const CameraViewSnapshot snapshot = BuildCameraViewSnapshot(finalized, viewport);
    EXPECT_TRUE(snapshot.Valid);
    EXPECT_EQ(snapshot.Position, finalized.Position);
    EXPECT_EQ(snapshot.Forward, finalized.Forward);
    EXPECT_EQ(snapshot.Up, finalized.Up);
}

TEST(ReferenceCameraBuildInput, ZeroViewportFallsBackToUnitAspectAndStaysValid)
{
    Registry scene;
    TriangleProvider provider;
    const ReferenceScenePopulation population = provider.Populate(scene);
    ASSERT_TRUE(population.Camera.has_value());

    const CameraViewInput finalized = BuildReferenceCameraViewInput(*population.Camera, 0, 0);
    EXPECT_TRUE(finalized.Valid);

    const CameraViewSnapshot snapshot = BuildCameraViewSnapshot(finalized, {1, 1});
    EXPECT_TRUE(snapshot.Valid);
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
    EXPECT_FALSE(engine.GetReferenceCameraSeed().has_value());
    EXPECT_EQ(engine.GetScene().Raw().storage<EntityHandle>().size(), 0u);

    engine.Shutdown();
    EXPECT_FALSE(engine.IsReferenceSceneInstalled());
    EXPECT_FALSE(engine.GetReferenceCameraSeed().has_value());
}

TEST(EngineReferenceScene, ReferenceEngineConfigInvokesPreRegisteredProviderOnce)
{
    Extrinsic::Core::Config::EngineConfig config{};
    config.ReferenceScene.Enabled = true;
    config.ReferenceScene.Selector = ReferenceSceneSelector::Triangle;

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
    EXPECT_TRUE(engine.GetReferenceCameraSeed().has_value());

    engine.Shutdown();

    EXPECT_FALSE(engine.IsReferenceSceneInstalled());
    EXPECT_EQ(providerRaw->TeardownCalls, 1);
    EXPECT_FALSE(engine.GetReferenceCameraSeed().has_value());
}

TEST(EngineReferenceScene, ReferenceEnabledInstallsDefaultTriangleProviderWhenAbsent)
{
    namespace G = Extrinsic::Graphics::Components;
    namespace E = Extrinsic::ECS::Components;

    Extrinsic::Core::Config::EngineConfig config{};
    config.ReferenceScene.Enabled = true;
    config.ReferenceScene.Selector = ReferenceSceneSelector::Triangle;

    Extrinsic::Runtime::Engine engine(config, std::make_unique<StubApplication>());
    engine.Initialize();

    EXPECT_TRUE(engine.IsReferenceSceneInstalled());
    ASSERT_EQ(engine.GetScene().Raw().storage<EntityHandle>().size(), 1u);

    auto& raw = engine.GetScene().Raw();
    auto view = raw.view<E::MetaData, G::RenderSurface, E::ProceduralGeometryRef>();
    ASSERT_EQ(view.size_hint(), 1u);
    for (const auto entity : view)
    {
        EXPECT_EQ(view.get<E::MetaData>(entity).EntityName, "ReferenceTriangle");
        EXPECT_EQ(view.get<G::RenderSurface>(entity).Domain,
                  G::RenderSurface::SourceDomain::Vertex);
        EXPECT_EQ(view.get<E::ProceduralGeometryRef>(entity).Kind,
                  E::ProceduralGeometryKind::Triangle);
    }

    ASSERT_TRUE(engine.GetReferenceCameraSeed().has_value());
    EXPECT_TRUE(engine.GetReferenceCameraSeed()->Valid);

    engine.Shutdown();
    EXPECT_FALSE(engine.IsReferenceSceneInstalled());
}

TEST(EngineReferenceScene, RenderExtractionReportsOneCandidateAndOneAllocatedInstance)
{
    Extrinsic::Core::Config::EngineConfig config{};
    config.ReferenceScene.Enabled = true;
    config.ReferenceScene.Selector = ReferenceSceneSelector::Triangle;

    Extrinsic::Runtime::Engine engine(config, std::make_unique<StubApplication>());
    engine.Initialize();
    ASSERT_TRUE(engine.IsReferenceSceneInstalled());

    Extrinsic::Runtime::RenderExtractionCache extraction;
    const auto stats = extraction.ExtractAndSubmit(engine.GetScene(),
                                                    engine.GetRenderer(),
                                                    &engine.GetGpuAssetCache());
    EXPECT_EQ(stats.CandidateRenderableCount, 1u);
    EXPECT_EQ(stats.AllocatedInstanceCount, 1u);

    extraction.Shutdown(engine.GetRenderer());
    engine.Shutdown();
}
