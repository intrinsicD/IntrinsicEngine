#include <cmath>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Platform.Input;
import Extrinsic.Runtime.CameraControllers;

using namespace Extrinsic;

namespace
{
    [[nodiscard]] Graphics::CameraViewInput MakeSeed()
    {
        Graphics::CameraViewInput seed{};
        seed.Position = {0.0f, 0.0f, 3.0f};
        seed.Forward = {0.0f, 0.0f, -1.0f};
        seed.Up = {0.0f, 1.0f, 0.0f};
        seed.NearPlane = 0.1f;
        seed.FarPlane = 100.0f;
        seed.Valid = true;
        return seed;
    }

    [[nodiscard]] bool IsFinite(const glm::vec3 value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    void ExpectValidCameraView(const Graphics::CameraViewInput& input)
    {
        EXPECT_TRUE(input.Valid);
        EXPECT_TRUE(IsFinite(input.Position));
        EXPECT_TRUE(IsFinite(input.Forward));
        EXPECT_TRUE(IsFinite(input.Up));
        EXPECT_GT(glm::length(input.Forward), 0.0001f);
        EXPECT_GT(glm::length(input.Up), 0.0001f);

        const Graphics::CameraViewSnapshot snapshot = Graphics::BuildCameraViewSnapshot(
            input,
            Core::Extent2D{1280, 720});
        EXPECT_TRUE(snapshot.Valid);
        EXPECT_TRUE(snapshot.FrustumPlanes[static_cast<std::uint32_t>(Graphics::FrustumPlaneIndex::Near)].Valid);
    }

    void ExpectWorldPointCentered(const Graphics::CameraViewInput& input,
                                  const glm::vec3 point)
    {
        ExpectValidCameraView(input);
        const glm::vec4 clip = input.Projection * input.View * glm::vec4(point, 1.0f);
        ASSERT_TRUE(std::isfinite(clip.w));
        ASSERT_GT(std::abs(clip.w), 0.000001f);
        EXPECT_NEAR(clip.x / clip.w, 0.0f, 0.0001f);
        EXPECT_NEAR(clip.y / clip.w, 0.0f, 0.0001f);
    }

    void ExpectWorldPointInsideClipSpace(const Graphics::CameraViewInput& input,
                                         const glm::vec3 point)
    {
        ExpectValidCameraView(input);
        const glm::vec4 clip = input.Projection * input.View * glm::vec4(point, 1.0f);
        ASSERT_TRUE(std::isfinite(clip.w));
        ASSERT_GT(std::abs(clip.w), 0.000001f);
        const glm::vec3 ndc = glm::vec3{clip} / clip.w;
        EXPECT_GE(ndc.x, -1.0f);
        EXPECT_LE(ndc.x, 1.0f);
        EXPECT_GE(ndc.y, -1.0f);
        EXPECT_LE(ndc.y, 1.0f);
        EXPECT_GE(ndc.z, 0.0f);
        EXPECT_LE(ndc.z, 1.0f);
    }

    void ExpectReferenceTriangleInsideClipSpace(const Graphics::CameraViewInput& input)
    {
        ExpectWorldPointCentered(input, glm::vec3{0.0f, 0.0f, 0.0f});
        ExpectWorldPointInsideClipSpace(input, glm::vec3{-0.5f, -0.5f, 0.0f});
        ExpectWorldPointInsideClipSpace(input, glm::vec3{ 0.5f, -0.5f, 0.0f});
        ExpectWorldPointInsideClipSpace(input, glm::vec3{ 0.0f,  0.5f, 0.0f});
    }
}

TEST(RuntimeCameraControllers, OrbitProducesValidFiniteViewAndClampsRadius)
{
    Runtime::OrbitCameraController controller{MakeSeed()};
    Platform::Input::Context input{};
    input.AccumulateScroll(0.0f, 10000.0f);
    input.Update();

    controller.Update(input, 1.0 / 60.0);

    EXPECT_GE(controller.Radius(), controller.MinRadius());
    ExpectValidCameraView(controller.GetView(Core::Extent2D{1280, 720}));
}

TEST(RuntimeCameraControllers, OrbitYawWrapsDuringMouseDrag)
{
    Runtime::OrbitCameraController controller{MakeSeed()};
    Platform::Input::Context input{};
    input.SetMouseButtonState(1, true);
    input.SetMousePosition(0.0f, 0.0f);
    controller.Update(input, 1.0 / 60.0);

    input.SetMousePosition(-100000.0f, 0.0f);
    controller.Update(input, 1.0 / 60.0);

    EXPECT_GE(controller.YawRadians(), 0.0f);
    EXPECT_LT(controller.YawRadians(), 6.2831855f);
    ExpectValidCameraView(controller.GetView(Core::Extent2D{1280, 720}));
}

TEST(RuntimeCameraControllers, OrbitAlsoRotatesWithMiddleMouseDrag)
{
    Runtime::OrbitCameraController controller{MakeSeed()};
    Platform::Input::Context input{};
    input.SetMouseButtonState(2, true);
    input.SetMousePosition(0.0f, 0.0f);
    controller.Update(input, 1.0 / 60.0);

    input.SetMousePosition(64.0f, 0.0f);
    controller.Update(input, 1.0 / 60.0);

    EXPECT_NE(controller.YawRadians(), 0.0f);
    ExpectValidCameraView(controller.GetView(Core::Extent2D{1280, 720}));
}

TEST(RuntimeCameraControllers, FlyMovementScalesWithDeltaTime)
{
    Runtime::FlyCameraController singleStep{MakeSeed()};
    Runtime::FlyCameraController twoSteps{MakeSeed()};
    Platform::Input::Context input{};
    input.SetKeyState(Platform::Input::Key::W, true);

    singleStep.Update(input, 1.0);
    twoSteps.Update(input, 0.5);
    twoSteps.Update(input, 0.5);

    const Graphics::CameraViewInput a = singleStep.GetView(Core::Extent2D{1280, 720});
    const Graphics::CameraViewInput b = twoSteps.GetView(Core::Extent2D{1280, 720});
    EXPECT_NEAR(a.Position.x, b.Position.x, 0.0001f);
    EXPECT_NEAR(a.Position.y, b.Position.y, 0.0001f);
    EXPECT_NEAR(a.Position.z, b.Position.z, 0.0001f);
    ExpectValidCameraView(a);
    ExpectValidCameraView(b);
}

TEST(RuntimeCameraControllers, FlyMouseLookUsesLegacyRightButtonYawPitchSign)
{
    Runtime::FlyCameraController controller{MakeSeed()};
    Platform::Input::Context input{};
    input.SetMouseButtonState(1, true);
    input.SetMousePosition(0.0f, 0.0f);
    controller.Update(input, 1.0 / 60.0);

    input.SetMousePosition(100.0f, 50.0f);
    controller.Update(input, 1.0 / 60.0);

    const Graphics::CameraViewInput view = controller.GetView(Core::Extent2D{1280, 720});
    EXPECT_LT(view.Forward.x, -0.0001f)
        << "Legacy fly camera subtracts positive mouse X from yaw.";
    EXPECT_LT(view.Forward.y, -0.0001f)
        << "Legacy fly camera subtracts positive mouse Y from pitch.";
    ExpectValidCameraView(view);
}

TEST(RuntimeCameraControllers, FlyKeyboardMappingUsesLegacyAxesAndShiftScalar)
{
    Runtime::FlyCameraController forward{MakeSeed()};
    Platform::Input::Context forwardInput{};
    forwardInput.SetKeyState(Platform::Input::Key::W, true);
    forward.Update(forwardInput, 1.0);
    const Graphics::CameraViewInput forwardView = forward.GetView(Core::Extent2D{1280, 720});
    EXPECT_NEAR(forwardView.Position.x, 0.0f, 0.0001f);
    EXPECT_NEAR(forwardView.Position.y, 0.0f, 0.0001f);
    EXPECT_NEAR(forwardView.Position.z, -2.0f, 0.0001f);

    Runtime::FlyCameraController right{MakeSeed()};
    Platform::Input::Context rightInput{};
    rightInput.SetKeyState(Platform::Input::Key::D, true);
    right.Update(rightInput, 1.0);
    const Graphics::CameraViewInput rightView = right.GetView(Core::Extent2D{1280, 720});
    EXPECT_NEAR(rightView.Position.x, 5.0f, 0.0001f);
    EXPECT_NEAR(rightView.Position.y, 0.0f, 0.0001f);
    EXPECT_NEAR(rightView.Position.z, 3.0f, 0.0001f);

    Runtime::FlyCameraController up{MakeSeed()};
    Platform::Input::Context upInput{};
    upInput.SetKeyState(Platform::Input::Key::Space, true);
    up.Update(upInput, 1.0);
    const Graphics::CameraViewInput upView = up.GetView(Core::Extent2D{1280, 720});
    EXPECT_NEAR(upView.Position.x, 0.0f, 0.0001f);
    EXPECT_NEAR(upView.Position.y, 5.0f, 0.0001f);
    EXPECT_NEAR(upView.Position.z, 3.0f, 0.0001f);

    Runtime::FlyCameraController accelerated{MakeSeed()};
    Platform::Input::Context acceleratedInput{};
    acceleratedInput.SetKeyState(Platform::Input::Key::W, true);
    acceleratedInput.SetKeyState(Platform::Input::Key::LeftShift, true);
    accelerated.Update(acceleratedInput, 1.0);
    const Graphics::CameraViewInput acceleratedView =
        accelerated.GetView(Core::Extent2D{1280, 720});
    EXPECT_NEAR(acceleratedView.Position.z, -7.0f, 0.0001f)
        << "Legacy fly camera doubles movement while left shift is pressed.";
}

TEST(RuntimeCameraControllers, RegistryReplacementCanSeedNextControllerFromTerminalView)
{
    Runtime::CameraControllerRegistry registry;
    registry.Register(Runtime::CameraControllerSlot::Main,
                      Runtime::CreateCameraController(Core::Config::CameraControllerKind::Orbit, MakeSeed()));

    Platform::Input::Context input{};
    input.SetKeyState(Platform::Input::Key::D, true);
    registry.Resolve(Runtime::CameraControllerSlot::Main).Update(input, 0.5);
    const Graphics::CameraViewInput terminal = registry.Resolve(Runtime::CameraControllerSlot::Main)
        .GetView(Core::Extent2D{1280, 720});

    registry.Replace(Runtime::CameraControllerSlot::Main,
                     Runtime::CreateCameraController(Core::Config::CameraControllerKind::Fly, terminal));
    const Graphics::CameraViewInput replacement = registry.Resolve(Runtime::CameraControllerSlot::Main)
        .GetView(Core::Extent2D{1280, 720});

    EXPECT_EQ(registry.Resolve(Runtime::CameraControllerSlot::Main).Kind(), Core::Config::CameraControllerKind::Fly);
    EXPECT_NEAR(replacement.Position.x, terminal.Position.x, 0.0001f);
    EXPECT_NEAR(replacement.Position.y, terminal.Position.y, 0.0001f);
    EXPECT_NEAR(replacement.Position.z, terminal.Position.z, 0.0001f);
    ExpectValidCameraView(replacement);
}

TEST(RuntimeCameraControllers, RegistryCameraTransitionFlagIsOneShotPerSlot)
{
    Runtime::CameraControllerRegistry registry;
    registry.Register(Runtime::CameraControllerSlot::Main,
                      Runtime::CreateCameraController(Core::Config::CameraControllerKind::Orbit, MakeSeed()));

    EXPECT_TRUE(registry.ConsumeCameraTransition(Runtime::CameraControllerSlot::Main));
    EXPECT_FALSE(registry.ConsumeCameraTransition(Runtime::CameraControllerSlot::Main));

    registry.MarkCameraTransition(Runtime::CameraControllerSlot::Main);
    EXPECT_TRUE(registry.ConsumeCameraTransition(Runtime::CameraControllerSlot::Main));
    EXPECT_FALSE(registry.ConsumeCameraTransition(Runtime::CameraControllerSlot::Main));

    registry.Replace(Runtime::CameraControllerSlot::Main,
                     Runtime::CreateCameraController(Core::Config::CameraControllerKind::Fly, MakeSeed()));
    EXPECT_TRUE(registry.ConsumeCameraTransition(Runtime::CameraControllerSlot::Main));
    EXPECT_FALSE(registry.ConsumeCameraTransition(Runtime::CameraControllerSlot::Main));
    EXPECT_FALSE(registry.ConsumeCameraTransition(Runtime::CameraControllerSlot::Preview));
}

TEST(RuntimeCameraControllers, FactoryCreatesEveryControllerKindWithValidView)
{
    constexpr Core::Config::CameraControllerKind kinds[] = {
        Core::Config::CameraControllerKind::Orbit,
        Core::Config::CameraControllerKind::Fly,
        Core::Config::CameraControllerKind::FreeLook,
        Core::Config::CameraControllerKind::TopDown,
    };

    Platform::Input::Context input{};
    input.SetMouseButtonState(1, true);
    input.SetMousePosition(50.0f, 25.0f);
    input.SetKeyState(Platform::Input::Key::W, true);
    input.SetKeyState(Platform::Input::Key::Q, true);
    input.AccumulateScroll(0.0f, 1.0f);
    input.Update();

    for (const Core::Config::CameraControllerKind kind : kinds)
    {
        std::unique_ptr<Runtime::ICameraController> controller = Runtime::CreateCameraController(kind, MakeSeed());
        ASSERT_NE(controller, nullptr);
        EXPECT_EQ(controller->Kind(), kind);
        controller->Update(input, 1.0 / 30.0);
        ExpectValidCameraView(controller->GetView(Core::Extent2D{1280, 720}));
    }
}

TEST(RuntimeCameraControllers, FactorySeedsEveryControllerKindOnReferenceTriangleFocus)
{
    constexpr Core::Config::CameraControllerKind kinds[] = {
        Core::Config::CameraControllerKind::Orbit,
        Core::Config::CameraControllerKind::Fly,
        Core::Config::CameraControllerKind::FreeLook,
        Core::Config::CameraControllerKind::TopDown,
    };

    for (const Core::Config::CameraControllerKind kind : kinds)
    {
        SCOPED_TRACE(static_cast<int>(kind));
        std::unique_ptr<Runtime::ICameraController> controller = Runtime::CreateCameraController(kind, MakeSeed());
        ASSERT_NE(controller, nullptr);
        ExpectReferenceTriangleInsideClipSpace(controller->GetView(Core::Extent2D{1280, 720}));
    }
}

TEST(RuntimeCameraControllers, TopDownUsesOrthographicProjectionAndClampsZoom)
{
    Runtime::TopDownCameraController controller{MakeSeed()};
    Platform::Input::Context input{};
    input.AccumulateScroll(0.0f, 10000.0f);
    input.Update();

    controller.Update(input, 1.0 / 60.0);

    EXPECT_GE(controller.OrthographicHeight(), controller.MinOrthographicHeight());
    const Graphics::CameraViewInput view = controller.GetView(Core::Extent2D{1280, 720});
    EXPECT_NEAR(view.Forward.x, 0.0f, 0.0001f);
    EXPECT_NEAR(view.Forward.y, -1.0f, 0.0001f);
    EXPECT_NEAR(view.Forward.z, 0.0f, 0.0001f);
    ExpectValidCameraView(view);
}

TEST(RuntimeCameraControllers, RegistryCanSeedTopDownFromTerminalReferenceView)
{
    Runtime::CameraControllerRegistry registry;
    registry.Register(Runtime::CameraControllerSlot::Main,
                      Runtime::CreateCameraController(Core::Config::CameraControllerKind::Orbit, MakeSeed()));

    const Graphics::CameraViewInput terminal = registry.Resolve(Runtime::CameraControllerSlot::Main)
        .GetView(Core::Extent2D{1280, 720});

    registry.Replace(Runtime::CameraControllerSlot::Main,
                     Runtime::CreateCameraController(Core::Config::CameraControllerKind::TopDown, terminal));
    const Graphics::CameraViewInput replacement = registry.Resolve(Runtime::CameraControllerSlot::Main)
        .GetView(Core::Extent2D{1280, 720});

    EXPECT_EQ(registry.Resolve(Runtime::CameraControllerSlot::Main).Kind(), Core::Config::CameraControllerKind::TopDown);
    ExpectWorldPointCentered(replacement, glm::vec3{0.0f, 0.0f, 0.0f});
}

TEST(RuntimeCameraControllers, FreeLookRollChangesUpVectorAndStaysValid)
{
    Runtime::FreeLookCameraController controller{MakeSeed()};
    const Graphics::CameraViewInput before = controller.GetView(Core::Extent2D{1280, 720});

    Platform::Input::Context input{};
    input.SetKeyState(Platform::Input::Key::Q, true);
    controller.Update(input, 0.25);

    const Graphics::CameraViewInput after = controller.GetView(Core::Extent2D{1280, 720});
    EXPECT_GT(glm::length(after.Up - before.Up), 0.0001f);
    ExpectValidCameraView(after);
}

TEST(RuntimeCameraControllers, RegistrySupportsMultipleCameraSlots)
{
    Runtime::CameraControllerRegistry registry;
    registry.Register(Runtime::CameraControllerSlot::Main,
                      Runtime::CreateCameraController(Core::Config::CameraControllerKind::Orbit, MakeSeed()));
    registry.Register(Runtime::CameraControllerSlot::Preview,
                      Runtime::CreateCameraController(Core::Config::CameraControllerKind::Fly, MakeSeed()));
    registry.Register(Runtime::CameraControllerSlot::TopDown,
                      Runtime::CreateCameraController(Core::Config::CameraControllerKind::TopDown, MakeSeed()));
    registry.Register(Runtime::CameraControllerSlot::EditorSecondary,
                      Runtime::CreateCameraController(Core::Config::CameraControllerKind::FreeLook, MakeSeed()));

    EXPECT_EQ(registry.Resolve(Runtime::CameraControllerSlot::Main).Kind(), Core::Config::CameraControllerKind::Orbit);
    EXPECT_EQ(registry.Resolve(Runtime::CameraControllerSlot::Preview).Kind(), Core::Config::CameraControllerKind::Fly);
    EXPECT_EQ(registry.Resolve(Runtime::CameraControllerSlot::TopDown).Kind(), Core::Config::CameraControllerKind::TopDown);
    EXPECT_EQ(registry.Resolve(Runtime::CameraControllerSlot::EditorSecondary).Kind(), Core::Config::CameraControllerKind::FreeLook);
}
