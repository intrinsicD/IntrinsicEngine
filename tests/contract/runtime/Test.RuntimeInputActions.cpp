// RUNTIME-144 — registered input actions route frame-loop commands through
// callbacks. The sandbox default `F` focus binding is exercised through the
// real Null-window RunFrame path so the test covers input edge detection,
// post-flush dispatch, selection lookup, and same-frame camera focus.

#include <cstdint>
#include <memory>
#include <optional>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.SandboxDefaultPolicies;
import Extrinsic.Runtime.SelectionController;

namespace
{
    namespace Core = Extrinsic::Core;
    namespace ECS = Extrinsic::ECS;
    namespace Runtime = Extrinsic::Runtime;
    namespace World = Extrinsic::ECS::Components::Culling::World;

    class RecordingCameraController final : public Runtime::ICameraController
    {
    public:
        void Seed(const Extrinsic::Graphics::CameraViewInput& seed) noexcept override
        {
            if (seed.Valid)
                m_View = seed;
        }

        void Focus(const Runtime::CameraFocusTarget target) noexcept override
        {
            LastFocus = target;
            m_View.Position = target.Center + glm::vec3{0.0f, 0.0f, 4.0f};
            m_View.Valid = true;
            ++FocusCalls;
        }

        void Update(const Extrinsic::Platform::Input::Context& /*input*/,
                    double /*deltaSeconds*/) noexcept override
        {
            ++Updates;
        }

        [[nodiscard]] Extrinsic::Graphics::CameraViewInput GetView(
            Core::Extent2D /*viewport*/) const noexcept override
        {
            return m_View;
        }

        [[nodiscard]] Core::Config::CameraControllerKind Kind() const noexcept override
        {
            return Core::Config::CameraControllerKind::Orbit;
        }

        std::optional<Runtime::CameraFocusTarget> LastFocus{};
        std::uint32_t FocusCalls{0u};
        std::uint32_t Updates{0u};

    private:
        Extrinsic::Graphics::CameraViewInput m_View{
            Runtime::DefaultCameraControllerSeed()};
    };

    [[nodiscard]] Core::Config::EngineConfig InputActionConfig()
    {
        Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = true;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }

    class PressFocusKeyApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            Entity = engine.GetScene().Create();
            World::Bounds bounds{};
            bounds.WorldBoundingSphere.Center = glm::vec3{4.0f, 2.0f, -1.0f};
            bounds.WorldBoundingSphere.Radius = 2.5f;
            engine.GetScene().Raw().emplace<World::Bounds>(Entity, bounds);
            SelectionApplied =
                engine.GetSelectionController().SetSelectedEntity(
                    engine.GetScene(),
                    Entity);

            auto controller = std::make_unique<RecordingCameraController>();
            Controller = controller.get();
            engine.GetCameraControllerRegistry().Register(
                Runtime::CameraControllerSlot::Main,
                std::move(controller));
        }

        void OnSimTick(Runtime::Engine& /*engine*/, double /*fixedDt*/) override {}

        void OnVariableTick(Runtime::Engine& engine,
                            double /*alpha*/,
                            double /*dt*/) override
        {
            ++VariableTicks;
            const Extrinsic::Platform::IWindow& window = engine.GetWindow();
            auto& input = const_cast<Extrinsic::Platform::Input::Context&>(
                window.GetInput());
            input.SetKeyState(Extrinsic::Platform::Input::Key::F, true);
            engine.RequestExit();
        }

        void OnShutdown(Runtime::Engine& /*engine*/) override {}

        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        RecordingCameraController* Controller{nullptr};
        bool SelectionApplied{false};
        std::uint32_t VariableTicks{0u};
    };
}

TEST(RuntimeInputActions, DefaultFocusKeyDispatchesRegisteredAction)
{
    auto app = std::make_unique<PressFocusKeyApplication>();
    auto* appPtr = app.get();
    Runtime::Engine engine(InputActionConfig(), std::move(app));
    engine.Initialize();
    (void)Runtime::RegisterSandboxDefaultRuntimePolicies(engine);

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "Null window backend should keep Engine::Run() drivable";

    engine.Run();

    ASSERT_NE(appPtr->Controller, nullptr);
    EXPECT_TRUE(appPtr->SelectionApplied);
    EXPECT_EQ(appPtr->VariableTicks, 1u);
    EXPECT_EQ(appPtr->Controller->Updates, 1u);
    EXPECT_EQ(appPtr->Controller->FocusCalls, 1u);
    ASSERT_TRUE(appPtr->Controller->LastFocus.has_value());
    EXPECT_NEAR(appPtr->Controller->LastFocus->Center.x, 4.0f, 1.0e-5f);
    EXPECT_NEAR(appPtr->Controller->LastFocus->Center.y, 2.0f, 1.0e-5f);
    EXPECT_NEAR(appPtr->Controller->LastFocus->Center.z, -1.0f, 1.0e-5f);
    EXPECT_NEAR(appPtr->Controller->LastFocus->Radius, 2.5f, 1.0e-5f);

    engine.Shutdown();
}

TEST(RuntimeInputActions, NoDefaultInputActionsLeaveFocusKeyNoOp)
{
    auto app = std::make_unique<PressFocusKeyApplication>();
    auto* appPtr = app.get();
    Runtime::Engine engine(InputActionConfig(), std::move(app));
    engine.Initialize();

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "Null window backend should keep Engine::Run() drivable";

    engine.Run();

    ASSERT_NE(appPtr->Controller, nullptr);
    EXPECT_TRUE(appPtr->SelectionApplied);
    EXPECT_EQ(appPtr->VariableTicks, 1u);
    EXPECT_EQ(appPtr->Controller->Updates, 1u);
    EXPECT_EQ(appPtr->Controller->FocusCalls, 0u);
    EXPECT_FALSE(appPtr->Controller->LastFocus.has_value());

    engine.Shutdown();
}
