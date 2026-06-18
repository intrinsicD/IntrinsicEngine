// RUNTIME-116 — contract coverage for the reusable focus-camera-on-objects
// command: the pure center-of-mass + enclosing-extent aggregation, the
// scene-driven gather of world bounding spheres, and the apply/command wrappers
// that drive a camera controller slot. The `F`-key binding into
// `Engine::RunFrame` is thin glue over these tested pieces plus the
// already-contracted `IsKeyJustPressed` / `ICameraController::Focus` paths.

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Platform.Input;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.SelectionController;
import Geometry.Sphere;

using namespace Extrinsic;

namespace
{
    constexpr float kMinimumFocusRadius = 0.05f;

    // Records the last Focus() target and the number of Focus() calls so tests
    // can assert the command routed the aggregated target to the slot.
    class RecordingController final : public Runtime::ICameraController
    {
    public:
        void Seed(const Graphics::CameraViewInput& /*seed*/) noexcept override {}

        void Focus(Runtime::CameraFocusTarget target) noexcept override
        {
            LastFocus = target;
            ++FocusCalls;
        }

        void Update(const Platform::Input::Context& /*input*/,
                    double /*deltaSeconds*/) noexcept override {}

        [[nodiscard]] Graphics::CameraViewInput GetView(Core::Extent2D /*viewport*/)
            const noexcept override
        {
            return Graphics::CameraViewInput{};
        }

        [[nodiscard]] Core::Config::CameraControllerKind Kind() const noexcept override
        {
            return Core::Config::CameraControllerKind::Orbit;
        }

        Runtime::CameraFocusTarget LastFocus{};
        std::uint32_t              FocusCalls{0u};
    };

    [[nodiscard]] Geometry::Sphere Sphere(const glm::vec3 center, const float radius)
    {
        return Geometry::Sphere{.Center = center, .Radius = radius};
    }

    [[nodiscard]] ECS::EntityHandle MakeBounded(ECS::Scene::Registry& scene,
                                                const glm::vec3       center,
                                                const float           radius)
    {
        const ECS::EntityHandle entity = scene.Create();
        ECS::Components::Culling::World::Bounds bounds{};
        bounds.WorldBoundingSphere.Center = center;
        bounds.WorldBoundingSphere.Radius = radius;
        scene.Raw().emplace<ECS::Components::Culling::World::Bounds>(entity, bounds);
        return entity;
    }

    // Every contributing sphere must lie within the computed focus sphere, which
    // is what "completely visible" reduces to once the controller frames it.
    void ExpectEnclosesAll(const Runtime::CameraFocusTarget&       target,
                           const std::span<const Geometry::Sphere> spheres)
    {
        for (const Geometry::Sphere& sphere : spheres)
        {
            const float reach = glm::length(sphere.Center - target.Center) + sphere.Radius;
            EXPECT_LE(reach, target.Radius + 1.0e-4f);
        }
    }
}

// ── Pure aggregation ────────────────────────────────────────────────────────

TEST(RuntimeCameraFocusCommand, EmptyInputYieldsNoTarget)
{
    EXPECT_FALSE(Runtime::ComputeFocusTargetForBoundingSpheres({}).has_value());
}

TEST(RuntimeCameraFocusCommand, SingleSphereFocusesItsCenterAndRadius)
{
    const std::array<Geometry::Sphere, 1> spheres{Sphere({5.0f, 5.0f, 5.0f}, 2.0f)};
    const auto target = Runtime::ComputeFocusTargetForBoundingSpheres(spheres);
    ASSERT_TRUE(target.has_value());
    EXPECT_NEAR(target->Center.x, 5.0f, 1.0e-5f);
    EXPECT_NEAR(target->Center.y, 5.0f, 1.0e-5f);
    EXPECT_NEAR(target->Center.z, 5.0f, 1.0e-5f);
    EXPECT_NEAR(target->Radius, 2.0f, 1.0e-5f);
    ExpectEnclosesAll(*target, spheres);
}

TEST(RuntimeCameraFocusCommand, TwoSeparatedSpheresUseCenterOfMassAndEnclosingExtent)
{
    // Spheres at x = ±2, radius 1 each: center of mass at the origin, and the
    // enclosing radius reaches each center (2) plus its radius (1) = 3.
    const std::array<Geometry::Sphere, 2> spheres{
        Sphere({-2.0f, 0.0f, 0.0f}, 1.0f),
        Sphere({2.0f, 0.0f, 0.0f}, 1.0f),
    };
    const auto target = Runtime::ComputeFocusTargetForBoundingSpheres(spheres);
    ASSERT_TRUE(target.has_value());
    EXPECT_NEAR(target->Center.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(target->Center.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(target->Center.z, 0.0f, 1.0e-5f);
    EXPECT_NEAR(target->Radius, 3.0f, 1.0e-5f);
    ExpectEnclosesAll(*target, spheres);
}

TEST(RuntimeCameraFocusCommand, AsymmetricSpheresStayEnclosed)
{
    const std::array<Geometry::Sphere, 3> spheres{
        Sphere({0.0f, 0.0f, 0.0f}, 0.5f),
        Sphere({10.0f, 0.0f, 0.0f}, 3.0f),
        Sphere({0.0f, 4.0f, 0.0f}, 1.0f),
    };
    const auto target = Runtime::ComputeFocusTargetForBoundingSpheres(spheres);
    ASSERT_TRUE(target.has_value());
    // Center of mass is the mean of the three centers.
    EXPECT_NEAR(target->Center.x, 10.0f / 3.0f, 1.0e-4f);
    EXPECT_NEAR(target->Center.y, 4.0f / 3.0f, 1.0e-4f);
    ExpectEnclosesAll(*target, spheres);
}

TEST(RuntimeCameraFocusCommand, DegenerateZeroRadiusIsFlooredToMinimum)
{
    const std::array<Geometry::Sphere, 1> spheres{Sphere({0.0f, 0.0f, 0.0f}, 0.0f)};
    const auto target = Runtime::ComputeFocusTargetForBoundingSpheres(spheres);
    ASSERT_TRUE(target.has_value());
    EXPECT_NEAR(target->Radius, kMinimumFocusRadius, 1.0e-6f);
}

TEST(RuntimeCameraFocusCommand, NonFiniteSpheresAreSkipped)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<Geometry::Sphere, 2> spheres{
        Sphere({nan, 0.0f, 0.0f}, 1.0f),
        Sphere({3.0f, 0.0f, 0.0f}, 1.0f),
    };
    const auto target = Runtime::ComputeFocusTargetForBoundingSpheres(spheres);
    ASSERT_TRUE(target.has_value());
    // Only the finite sphere contributes.
    EXPECT_NEAR(target->Center.x, 3.0f, 1.0e-5f);
    EXPECT_NEAR(target->Radius, 1.0f, 1.0e-5f);
}

// ── Scene-driven gather ─────────────────────────────────────────────────────

TEST(RuntimeCameraFocusCommand, GathersWorldBoundsFromEntities)
{
    ECS::Scene::Registry scene;
    const ECS::EntityHandle a = MakeBounded(scene, {-2.0f, 0.0f, 0.0f}, 1.0f);
    const ECS::EntityHandle b = MakeBounded(scene, {2.0f, 0.0f, 0.0f}, 1.0f);

    const std::array<ECS::EntityHandle, 2> entities{a, b};
    const auto target = Runtime::ComputeFocusTargetForEntities(scene, entities);
    ASSERT_TRUE(target.has_value());
    EXPECT_NEAR(target->Center.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(target->Radius, 3.0f, 1.0e-5f);
}

TEST(RuntimeCameraFocusCommand, SkipsEntitiesWithoutWorldBounds)
{
    ECS::Scene::Registry scene;
    const ECS::EntityHandle bounded = MakeBounded(scene, {7.0f, 0.0f, 0.0f}, 2.0f);
    const ECS::EntityHandle plain   = scene.Create(); // no bounds component

    const std::array<ECS::EntityHandle, 2> entities{bounded, plain};
    const auto target = Runtime::ComputeFocusTargetForEntities(scene, entities);
    ASSERT_TRUE(target.has_value());
    EXPECT_NEAR(target->Center.x, 7.0f, 1.0e-5f);
    EXPECT_NEAR(target->Radius, 2.0f, 1.0e-5f);
}

TEST(RuntimeCameraFocusCommand, NoBoundedEntitiesYieldsNoTarget)
{
    ECS::Scene::Registry scene;
    const ECS::EntityHandle plain = scene.Create();
    const std::array<ECS::EntityHandle, 1> entities{plain};
    EXPECT_FALSE(Runtime::ComputeFocusTargetForEntities(scene, entities).has_value());
}

// ── Apply / command wrappers ────────────────────────────────────────────────

TEST(RuntimeCameraFocusCommand, FocusOnEntitiesDrivesControllerAndMarksTransition)
{
    ECS::Scene::Registry scene;
    const ECS::EntityHandle a = MakeBounded(scene, {-2.0f, 0.0f, 0.0f}, 1.0f);
    const ECS::EntityHandle b = MakeBounded(scene, {2.0f, 0.0f, 0.0f}, 1.0f);

    Runtime::CameraControllerRegistry cameras;
    auto controller = std::make_unique<RecordingController>();
    RecordingController* recorder = controller.get();
    cameras.Register(Runtime::CameraControllerSlot::Main, std::move(controller));

    const std::array<ECS::EntityHandle, 2> entities{a, b};
    EXPECT_TRUE(Runtime::FocusCameraOnEntities(cameras, scene, entities));

    EXPECT_EQ(recorder->FocusCalls, 1u);
    EXPECT_NEAR(recorder->LastFocus.Center.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(recorder->LastFocus.Radius, 3.0f, 1.0e-5f);
    EXPECT_TRUE(cameras.ConsumeCameraTransition(Runtime::CameraControllerSlot::Main));
}

TEST(RuntimeCameraFocusCommand, FocusOnEntitiesWithoutControllerReturnsFalse)
{
    ECS::Scene::Registry scene;
    const ECS::EntityHandle a = MakeBounded(scene, {0.0f, 0.0f, 0.0f}, 1.0f);
    Runtime::CameraControllerRegistry cameras; // empty: no controller registered

    const std::array<ECS::EntityHandle, 1> entities{a};
    EXPECT_FALSE(Runtime::FocusCameraOnEntities(cameras, scene, entities));
}

TEST(RuntimeCameraFocusCommand, FocusOnSelectionFramesTheSelectedEntity)
{
    ECS::Scene::Registry scene;
    const ECS::EntityHandle entity = MakeBounded(scene, {4.0f, 0.0f, 0.0f}, 2.0f);

    Runtime::SelectionController selection;
    ASSERT_TRUE(selection.SetSelectedEntity(scene, entity));

    Runtime::CameraControllerRegistry cameras;
    auto controller = std::make_unique<RecordingController>();
    RecordingController* recorder = controller.get();
    cameras.Register(Runtime::CameraControllerSlot::Main, std::move(controller));

    EXPECT_TRUE(Runtime::FocusCameraOnSelection(cameras, selection, scene));
    EXPECT_EQ(recorder->FocusCalls, 1u);
    EXPECT_NEAR(recorder->LastFocus.Center.x, 4.0f, 1.0e-5f);
    EXPECT_NEAR(recorder->LastFocus.Radius, 2.0f, 1.0e-5f);
}

TEST(RuntimeCameraFocusCommand, FocusOnEmptySelectionIsNoOp)
{
    ECS::Scene::Registry scene;
    Runtime::SelectionController selection; // nothing selected

    Runtime::CameraControllerRegistry cameras;
    auto controller = std::make_unique<RecordingController>();
    RecordingController* recorder = controller.get();
    cameras.Register(Runtime::CameraControllerSlot::Main, std::move(controller));

    EXPECT_FALSE(Runtime::FocusCameraOnSelection(cameras, selection, scene));
    EXPECT_EQ(recorder->FocusCalls, 0u);
}
