#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Platform.Input;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Core = Extrinsic::Core;
namespace Graphics = Extrinsic::Graphics;
namespace Platform = Extrinsic::Platform;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    [[nodiscard]] Graphics::CameraViewInput SeedAt(
        const glm::vec3 position)
    {
        Graphics::CameraViewInput seed =
            Runtime::DefaultCameraControllerSeed();
        seed.Position = position;
        seed.Valid = true;
        return seed;
    }

    class RecordingController final
        : public Runtime::ICameraController
    {
    public:
        explicit RecordingController(
            Graphics::CameraViewInput view =
                Runtime::DefaultCameraControllerSeed())
            : m_View(std::move(view))
        {
        }

        void Seed(
            const Graphics::CameraViewInput& seed) noexcept override
        {
            m_View = seed;
        }

        void Focus(
            const Runtime::CameraFocusTarget target) noexcept override
        {
            m_View.Position = target.Center;
        }

        void Update(
            const Platform::Input::Context&,
            double) noexcept override
        {
            ++Updates;
        }

        [[nodiscard]] Graphics::CameraViewInput GetView(
            Core::Extent2D) const noexcept override
        {
            return m_View;
        }

        [[nodiscard]] Core::Config::CameraControllerKind Kind()
            const noexcept override
        {
            return Core::Config::CameraControllerKind::Fly;
        }

        std::uint32_t Updates{0u};

    private:
        Graphics::CameraViewInput m_View{};
    };

    struct DirectCameraModuleHarness
    {
        Runtime::CommandBus Commands{};
        Runtime::KernelEventBus Events{};
        Runtime::JobService Jobs{};
        Runtime::WorldRegistry Worlds{};
        Runtime::ServiceRegistry Services{};
        Runtime::RuntimeViewportInputHook ViewportHook{};
        Runtime::WorldHandle InitialWorld{};

        DirectCameraModuleHarness()
        {
            InitialWorld = Worlds.CreateWorld("Initial");
            Services.BeginRegistration();
        }

        [[nodiscard]] Core::Result Register(
            Runtime::CameraModule& module,
            const bool provideViewportRegistrar = true)
        {
            Runtime::EngineSetup::ViewportInputHookRegistrar
                viewportRegistrar{};
            if (provideViewportRegistrar)
            {
                viewportRegistrar =
                    [this](Runtime::RuntimeViewportInputHook hook)
                    {
                        ViewportHook = std::move(hook);
                    };
            }

            Runtime::EngineSetup setup(
                Commands,
                Events,
                Jobs,
                Worlds,
                Services,
                [](Runtime::SimSystemDesc) {},
                [](Runtime::FramePhase,
                   Runtime::RuntimeFrameHook) {},
                {},
                std::move(viewportRegistrar));
            return module.OnRegister(setup);
        }

        [[nodiscard]] Core::Result Resolve(
            Runtime::CameraModule& module)
        {
            Services.BeginResolution();
            Runtime::EngineSetup setup(
                Commands,
                Events,
                Jobs,
                Worlds,
                Services,
                [](Runtime::SimSystemDesc) {},
                [](Runtime::FramePhase,
                   Runtime::RuntimeFrameHook) {},
                {},
                [this](Runtime::RuntimeViewportInputHook hook)
                {
                    ViewportHook = std::move(hook);
                });
            return module.OnResolve(setup);
        }

        void Shutdown(Runtime::CameraModule& module)
        {
            Runtime::RuntimeModuleShutdownContext context{
                .Commands = Commands,
                .Events = Events,
                .Jobs = Jobs,
                .Worlds = Worlds,
                .Services = Services,
            };
            module.OnShutdown(context);
            ViewportHook = {};
        }

        void RunViewportHook(
            const Core::Config::EngineConfig& config,
            const Runtime::WorldHandle activeWorld,
            const Platform::Input::Context& input,
            const Runtime::EditorInputCaptureSnapshot& capture,
            Graphics::RenderFrameInput& renderInput,
            const double deltaSeconds = 1.0 / 60.0)
        {
            ASSERT_TRUE(static_cast<bool>(ViewportHook));
            Runtime::RuntimeViewportInputHookContext context{
                .Config = config,
                .ActiveWorldHandle = activeWorld,
                .Input = input,
                .Viewport = Core::Extent2D{1280, 720},
                .EditorCapture = capture,
                .RenderInput = renderInput,
                .FrameDeltaSeconds = deltaSeconds,
            };
            ViewportHook(context);
        }
    };

    [[nodiscard]] Core::Config::EngineConfig CameraConfig(
        const bool enabled = true)
    {
        Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = enabled;
        config.Camera.Controller =
            Core::Config::CameraControllerKind::Fly;
        config.Window.Backend =
            Core::Config::WindowBackend::Null;
        return config;
    }

    [[nodiscard]] bool IsFiniteCamera(
        const Graphics::CameraViewInput& camera)
    {
        if (!camera.Valid ||
            !std::isfinite(camera.Position.x) ||
            !std::isfinite(camera.Position.y) ||
            !std::isfinite(camera.Position.z))
        {
            return false;
        }
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (!std::isfinite(camera.View[column][row]) ||
                    !std::isfinite(
                        camera.Projection[column][row]))
                {
                    return false;
                }
            }
        }
        return true;
    }

    class OneFrameCameraApplication final
        : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            Registry =
                engine.Services()
                    .Find<Runtime::CameraControllerRegistry>();
            ASSERT_NE(Registry, nullptr);
            SeedResult = Registry->SetWorldSeed(
                engine.ActiveWorld(),
                SeedAt(glm::vec3{1.0f, 2.0f, 6.0f}));
        }

        void OnSimTick(Runtime::Engine&, double) override {}

        void OnVariableTick(
            Runtime::Engine& engine, double, double) override
        {
            ++VariableTicks;
            engine.RequestExit();
        }

        void OnShutdown(Runtime::Engine&) override {}

        Runtime::CameraControllerRegistry* Registry{nullptr};
        Core::Result SeedResult{Core::Ok()};
        std::uint32_t VariableTicks{0u};
    };

    class OneFramePassiveApplication final
        : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(
            Runtime::Engine& engine, double, double) override
        {
            ++VariableTicks;
            engine.RequestExit();
        }
        void OnShutdown(Runtime::Engine&) override {}

        std::uint32_t VariableTicks{0u};
    };
}

TEST(CameraControllerRegistryWorldBinding,
     SetWorldSeedRejectsInvalidUnboundAndWrongWorldWithoutMutation)
{
    Runtime::CameraControllerRegistry registry;
    const Runtime::WorldHandle worldA{3u, 7u};
    const Runtime::WorldHandle worldB{4u, 2u};
    const Graphics::CameraViewInput original =
        SeedAt(glm::vec3{1.0f, 2.0f, 3.0f});
    const Graphics::CameraViewInput replacement =
        SeedAt(glm::vec3{9.0f, 8.0f, 7.0f});

    const Core::Result invalidUnbound =
        registry.SetWorldSeed({}, original);
    ASSERT_FALSE(invalidUnbound.has_value());
    EXPECT_EQ(invalidUnbound.error(),
              Core::ErrorCode::InvalidState);

    const Core::Result validButUnbound =
        registry.SetWorldSeed(worldA, original);
    ASSERT_FALSE(validButUnbound.has_value());
    EXPECT_EQ(validButUnbound.error(),
              Core::ErrorCode::InvalidState);

    registry.ResetForWorld(worldA);
    ASSERT_TRUE(
        registry.SetWorldSeed(worldA, original).has_value());
    const Core::Result wrongWorld =
        registry.SetWorldSeed(worldB, replacement);
    ASSERT_FALSE(wrongWorld.has_value());
    EXPECT_EQ(wrongWorld.error(),
              Core::ErrorCode::InvalidState);

    const auto retained = registry.WorldSeedFor(worldA);
    ASSERT_TRUE(retained.has_value());
    EXPECT_EQ(retained->Position, original.Position);

    registry.ResetForWorld({});
    const Core::Result invalidBinding =
        registry.SetWorldSeed(worldA, replacement);
    ASSERT_FALSE(invalidBinding.has_value());
    EXPECT_EQ(invalidBinding.error(),
              Core::ErrorCode::InvalidState);
    EXPECT_FALSE(registry.WorldSeedFor(worldA).has_value());
}

TEST(CameraControllerRegistryWorldBinding,
     EqualHandleResetClearsSlotsTransitionAndSeed)
{
    Runtime::CameraControllerRegistry registry;
    const Runtime::WorldHandle world{2u, 9u};
    registry.ResetForWorld(world);
    ASSERT_TRUE(registry.SetWorldSeed(
        world, SeedAt(glm::vec3{2.0f, 3.0f, 4.0f}))
                    .has_value());
    registry.Register(
        Runtime::CameraControllerSlot::Main,
        std::make_unique<RecordingController>());
    ASSERT_NE(
        registry.ResolveOrNull(
            Runtime::CameraControllerSlot::Main),
        nullptr);
    ASSERT_TRUE(registry.ConsumeCameraTransition(
        Runtime::CameraControllerSlot::Main));
    registry.MarkCameraTransition(
        Runtime::CameraControllerSlot::Main);

    registry.ResetForWorld(world);

    EXPECT_EQ(registry.BoundWorld(), world);
    EXPECT_EQ(
        registry.ResolveOrNull(
            Runtime::CameraControllerSlot::Main),
        nullptr);
    EXPECT_FALSE(registry.ConsumeCameraTransition(
        Runtime::CameraControllerSlot::Main));
    EXPECT_FALSE(registry.WorldSeedFor(world).has_value());
}

TEST(CameraControllerRegistryWorldBinding,
     AwayAndBackNeverResurrectsControllerOrSeed)
{
    Runtime::CameraControllerRegistry registry;
    const Runtime::WorldHandle worldA{5u, 1u};
    const Runtime::WorldHandle worldB{6u, 1u};
    registry.ResetForWorld(worldA);
    ASSERT_TRUE(registry.SetWorldSeed(
        worldA, SeedAt(glm::vec3{1.0f}))
                    .has_value());
    registry.Register(
        Runtime::CameraControllerSlot::Main,
        std::make_unique<RecordingController>());

    registry.ResetForWorld(worldB);
    registry.ResetForWorld(worldA);

    EXPECT_EQ(
        registry.ResolveOrNull(
            Runtime::CameraControllerSlot::Main),
        nullptr);
    EXPECT_FALSE(registry.WorldSeedFor(worldA).has_value());
}

TEST(CameraModuleLifecycle,
     PublishesExactRegistryResolvesAndWithdrawsOnShutdown)
{
    DirectCameraModuleHarness harness;
    Runtime::CameraModule module;

    ASSERT_TRUE(harness.Register(module).has_value());
    Runtime::CameraControllerRegistry* published =
        harness.Services
            .Find<Runtime::CameraControllerRegistry>();
    ASSERT_NE(published, nullptr);
    EXPECT_EQ(published->BoundWorld(),
              harness.InitialWorld);
    ASSERT_TRUE(harness.Resolve(module).has_value());
    harness.Services.Lock();

    harness.Shutdown(module);

    EXPECT_EQ(
        harness.Services
            .Find<Runtime::CameraControllerRegistry>(),
        nullptr);
    EXPECT_FALSE(published->BoundWorld().IsValid());
}

TEST(CameraModuleLifecycle,
     DuplicateForeignPublicationFailsWithoutMutatingForeignService)
{
    DirectCameraModuleHarness harness;
    Runtime::CameraControllerRegistry foreign;
    foreign.ResetForWorld(harness.InitialWorld);
    foreign.Register(
        Runtime::CameraControllerSlot::Preview,
        std::make_unique<RecordingController>());
    ASSERT_TRUE(harness.Services
                    .Provide<Runtime::CameraControllerRegistry>(
                        foreign, "Foreign")
                    .has_value());
    Runtime::CameraModule module;

    const Core::Result result = harness.Register(module);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidState);
    EXPECT_EQ(harness.Services
                  .Find<Runtime::CameraControllerRegistry>(),
              &foreign);
    EXPECT_NE(
        foreign.ResolveOrNull(
            Runtime::CameraControllerSlot::Preview),
        nullptr);
    EXPECT_FALSE(
        static_cast<bool>(harness.ViewportHook));
}

TEST(CameraModuleLifecycle,
     MissingViewportRegistrarRollsBackPublicationAndSubscriptions)
{
    DirectCameraModuleHarness harness;
    Runtime::CameraModule module;

    const Core::Result failed =
        harness.Register(module, false);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error(), Core::ErrorCode::InvalidState);
    EXPECT_EQ(harness.Services
                  .Find<Runtime::CameraControllerRegistry>(),
              nullptr);

    harness.Events.Publish(Runtime::ActiveWorldChanged{
        .Previous = harness.InitialWorld,
        .Current = Runtime::WorldHandle{7u, 4u},
    });
    (void)harness.Events.Pump();
    EXPECT_EQ(
        harness.Events.Stats().LastPumpListenerInvocations,
        0u);

    ASSERT_TRUE(harness.Register(module, true).has_value());
    EXPECT_NE(harness.Services
                  .Find<Runtime::CameraControllerRegistry>(),
              nullptr);
    harness.Shutdown(module);
}

TEST(CameraModuleLifecycle,
     ShutdownWithdrawsOnlyOwnedInstance)
{
    DirectCameraModuleHarness harness;
    Runtime::CameraModule module;
    ASSERT_TRUE(harness.Register(module).has_value());

    harness.Services.BeginRegistration();
    Runtime::CameraControllerRegistry foreign;
    foreign.ResetForWorld(harness.InitialWorld);
    ASSERT_TRUE(harness.Services
                    .Provide<Runtime::CameraControllerRegistry>(
                        foreign, "Foreign")
                    .has_value());

    harness.Shutdown(module);

    EXPECT_EQ(harness.Services
                  .Find<Runtime::CameraControllerRegistry>(),
              &foreign);
}

TEST(CameraModuleLifecycle,
     ShutdownReinitializeWithRecycledBitsStartsEmpty)
{
    DirectCameraModuleHarness harness;
    Runtime::CameraModule module;
    ASSERT_TRUE(harness.Register(module).has_value());
    Runtime::CameraControllerRegistry* registry =
        harness.Services
            .Find<Runtime::CameraControllerRegistry>();
    ASSERT_NE(registry, nullptr);
    registry->Register(
        Runtime::CameraControllerSlot::Main,
        std::make_unique<RecordingController>());
    ASSERT_TRUE(registry->SetWorldSeed(
        harness.InitialWorld,
        SeedAt(glm::vec3{4.0f, 5.0f, 6.0f}))
                    .has_value());

    harness.Shutdown(module);
    EXPECT_FALSE(registry->BoundWorld().IsValid());
    EXPECT_EQ(registry->ResolveOrNull(
                  Runtime::CameraControllerSlot::Main),
              nullptr);

    harness.Worlds.Clear();
    const Runtime::WorldHandle recycled =
        harness.Worlds.CreateWorld("Recycled");
    EXPECT_EQ(recycled, harness.InitialWorld);
    harness.Services.BeginRegistration();
    ASSERT_TRUE(harness.Register(module).has_value());
    Runtime::CameraControllerRegistry* republished =
        harness.Services
            .Find<Runtime::CameraControllerRegistry>();
    ASSERT_EQ(republished, registry);
    EXPECT_EQ(republished->BoundWorld(), recycled);
    EXPECT_EQ(republished->ResolveOrNull(
                  Runtime::CameraControllerSlot::Main),
              nullptr);
    EXPECT_FALSE(
        republished->WorldSeedFor(recycled).has_value());

    harness.Shutdown(module);
}

TEST(CameraModuleWorldLifecycle,
     ActiveWorldChangeAndRetirementClearBoundState)
{
    DirectCameraModuleHarness harness;
    Runtime::CameraModule module;
    ASSERT_TRUE(harness.Register(module).has_value());
    auto* registry = harness.Services
                         .Find<
                             Runtime::CameraControllerRegistry>();
    ASSERT_NE(registry, nullptr);
    registry->Register(
        Runtime::CameraControllerSlot::Main,
        std::make_unique<RecordingController>());

    const Runtime::WorldHandle next{8u, 3u};
    harness.Events.Publish(Runtime::ActiveWorldChanged{
        .Previous = harness.InitialWorld,
        .Current = next,
    });
    (void)harness.Events.Pump();
    EXPECT_EQ(registry->BoundWorld(), next);
    EXPECT_EQ(registry->ResolveOrNull(
                  Runtime::CameraControllerSlot::Main),
              nullptr);

    ASSERT_TRUE(registry->SetWorldSeed(
        next, SeedAt(glm::vec3{7.0f}))
                    .has_value());
    harness.Events.Publish(Runtime::WorldWillBeDestroyed{
        .World = next,
        .DebugName = "Next",
    });
    (void)harness.Events.Pump();
    EXPECT_FALSE(registry->BoundWorld().IsValid());
    EXPECT_FALSE(registry->WorldSeedFor(next).has_value());

    harness.Shutdown(module);
}

TEST(CameraModuleViewportHook,
     HandleMismatchResetsBeforeDisabledConfigReturns)
{
    DirectCameraModuleHarness harness;
    Runtime::CameraModule module;
    ASSERT_TRUE(harness.Register(module).has_value());
    auto* registry = harness.Services
                         .Find<
                             Runtime::CameraControllerRegistry>();
    ASSERT_NE(registry, nullptr);
    registry->Register(
        Runtime::CameraControllerSlot::Main,
        std::make_unique<RecordingController>());
    ASSERT_TRUE(registry->SetWorldSeed(
        harness.InitialWorld, SeedAt(glm::vec3{3.0f}))
                    .has_value());

    const Runtime::WorldHandle delayedCurrent{9u, 2u};
    Platform::Input::Context input;
    Runtime::EditorInputCaptureSnapshot capture{};
    Graphics::RenderFrameInput renderInput{};
    harness.RunViewportHook(
        CameraConfig(false),
        delayedCurrent,
        input,
        capture,
        renderInput);

    EXPECT_EQ(registry->BoundWorld(), delayedCurrent);
    EXPECT_EQ(registry->ResolveOrNull(
                  Runtime::CameraControllerSlot::Main),
              nullptr);
    EXPECT_FALSE(
        registry->WorldSeedFor(delayedCurrent).has_value());
    EXPECT_FALSE(renderInput.Camera.Valid);

    harness.Shutdown(module);
}

TEST(CameraModuleViewportHook,
     DisabledThenHotEnabledUsesWorldSeedAndConsumesTransitionOnce)
{
    DirectCameraModuleHarness harness;
    Runtime::CameraModule module;
    ASSERT_TRUE(harness.Register(module).has_value());
    auto* registry = harness.Services
                         .Find<
                             Runtime::CameraControllerRegistry>();
    ASSERT_NE(registry, nullptr);
    const Graphics::CameraViewInput seed =
        SeedAt(glm::vec3{2.0f, 4.0f, 8.0f});
    ASSERT_TRUE(registry->SetWorldSeed(
        harness.InitialWorld, seed)
                    .has_value());

    Platform::Input::Context input;
    Runtime::EditorInputCaptureSnapshot capture{};
    Graphics::RenderFrameInput disabledInput{};
    harness.RunViewportHook(
        CameraConfig(false),
        harness.InitialWorld,
        input,
        capture,
        disabledInput);
    EXPECT_FALSE(disabledInput.Camera.Valid);
    EXPECT_EQ(registry->ResolveOrNull(
                  Runtime::CameraControllerSlot::Main),
              nullptr);

    Graphics::RenderFrameInput enabledInput{};
    harness.RunViewportHook(
        CameraConfig(true),
        harness.InitialWorld,
        input,
        capture,
        enabledInput);
    EXPECT_TRUE(IsFiniteCamera(enabledInput.Camera));
    EXPECT_EQ(enabledInput.Camera.Position, seed.Position);
    EXPECT_TRUE(
        enabledInput.Camera.ExplicitCameraTransition);

    Graphics::RenderFrameInput nextInput{};
    harness.RunViewportHook(
        CameraConfig(true),
        harness.InitialWorld,
        input,
        capture,
        nextInput);
    EXPECT_FALSE(
        nextInput.Camera.ExplicitCameraTransition);

    harness.Shutdown(module);
}

TEST(CameraModuleViewportHook,
     EditorCaptureSuppressesUpdateButStillPublishesView)
{
    DirectCameraModuleHarness harness;
    Runtime::CameraModule module;
    ASSERT_TRUE(harness.Register(module).has_value());
    auto* registry = harness.Services
                         .Find<
                             Runtime::CameraControllerRegistry>();
    ASSERT_NE(registry, nullptr);
    auto controller = std::make_unique<RecordingController>(
        SeedAt(glm::vec3{3.0f, 2.0f, 1.0f}));
    RecordingController* recorder = controller.get();
    registry->Register(
        Runtime::CameraControllerSlot::Main,
        std::move(controller));

    Platform::Input::Context input;
    Runtime::EditorInputCaptureSnapshot captured{
        .CapturedKeyboard = true,
    };
    Graphics::RenderFrameInput capturedInput{};
    harness.RunViewportHook(
        CameraConfig(true),
        harness.InitialWorld,
        input,
        captured,
        capturedInput);
    EXPECT_EQ(recorder->Updates, 0u);
    EXPECT_TRUE(capturedInput.Camera.Valid);

    Runtime::EditorInputCaptureSnapshot available{};
    Graphics::RenderFrameInput availableInput{};
    harness.RunViewportHook(
        CameraConfig(true),
        harness.InitialWorld,
        input,
        available,
        availableInput);
    EXPECT_EQ(recorder->Updates, 1u);
    EXPECT_TRUE(availableInput.Camera.Valid);

    harness.Shutdown(module);
}

TEST(CameraModuleComposition,
     OptionalOmissionPublishesNoService)
{
    auto app =
        std::make_unique<OneFramePassiveApplication>();
    OneFramePassiveApplication* appPtr = app.get();
    Runtime::Engine engine(
        CameraConfig(true), std::move(app));
    engine.Initialize();
    EXPECT_EQ(engine.Services()
                  .Find<Runtime::CameraControllerRegistry>(),
              nullptr);
    engine.Run();
    EXPECT_EQ(appPtr->VariableTicks, 1u);
    EXPECT_EQ(engine.Services()
                  .Find<Runtime::CameraControllerRegistry>(),
              nullptr);
    engine.Shutdown();
}

TEST(CameraModuleComposition,
     ComposedNullWindowFirstFrameBuildsFiniteCamera)
{
    auto app =
        std::make_unique<OneFrameCameraApplication>();
    OneFrameCameraApplication* appPtr = app.get();
    Runtime::Engine engine(
        CameraConfig(true), std::move(app));
    engine.EmplaceModule<Runtime::CameraModule>();
    engine.Initialize();
    ASSERT_NE(appPtr->Registry, nullptr);
    ASSERT_TRUE(appPtr->SeedResult.has_value());

    engine.Run();

    EXPECT_EQ(appPtr->VariableTicks, 1u);
    Runtime::ICameraController* controller =
        appPtr->Registry->ResolveOrNull(
            Runtime::CameraControllerSlot::Main);
    ASSERT_NE(controller, nullptr);
    EXPECT_TRUE(IsFiniteCamera(
        controller->GetView(Core::Extent2D{1280, 720})));

    engine.Shutdown();
}
