#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

import Graphics;

namespace
{
    constexpr float kEps = 1e-4f;

    [[nodiscard]] Graphics::CameraComponent MakeTestCamera()
    {
        Graphics::CameraComponent cam;
        cam.Position = glm::vec3(2.0f, -1.5f, 6.0f);
        cam.Orientation = glm::normalize(
            glm::angleAxis(glm::radians(-15.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
            glm::angleAxis(glm::radians(25.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        cam.Fov = 60.0f;
        cam.AspectRatio = 16.0f / 9.0f;
        cam.Near = 0.1f;
        cam.Far = 250.0f;
        Graphics::UpdateMatrices(cam, cam.AspectRatio);
        return cam;
    }

    void ExpectVec2Near(const glm::vec2& a, const glm::vec2& b, float eps = kEps)
    {
        EXPECT_NEAR(a.x, b.x, eps);
        EXPECT_NEAR(a.y, b.y, eps);
    }

    void ExpectVec3Near(const glm::vec3& a, const glm::vec3& b, float eps = kEps)
    {
        EXPECT_NEAR(a.x, b.x, eps);
        EXPECT_NEAR(a.y, b.y, eps);
        EXPECT_NEAR(a.z, b.z, eps);
    }
}

TEST(CameraSpaces, WorldViewPointRoundTrip)
{
    const auto cam = MakeTestCamera();
    const glm::vec3 worldPoint{3.5f, 0.25f, -4.0f};

    const glm::vec3 viewPoint = Graphics::WorldToView(cam, worldPoint);
    const glm::vec3 reconstructed = Graphics::ViewToWorld(cam, viewPoint);

    ExpectVec3Near(reconstructed, worldPoint);
}

TEST(CameraSpaces, WorldViewDirectionRoundTrip)
{
    const auto cam = MakeTestCamera();
    const glm::vec3 worldDir = glm::normalize(glm::vec3{0.4f, -0.2f, -0.9f});

    const glm::vec3 viewDir = Graphics::WorldDirToView(cam, worldDir);
    const glm::vec3 reconstructed = glm::normalize(Graphics::ViewDirToWorld(cam, viewDir));

    ExpectVec3Near(reconstructed, worldDir);
}

TEST(CameraSpaces, WorldNdcPointRoundTrip)
{
    const auto cam = MakeTestCamera();
    const glm::vec3 worldPoint = cam.Position + cam.GetForward() * 8.0f + cam.GetRight() * 1.2f + cam.GetUp() * 0.8f;

    const glm::vec3 ndc = Graphics::WorldToNDC(cam, worldPoint);
    const glm::vec3 reconstructed = Graphics::NDCToWorld(cam, ndc);

    ExpectVec3Near(reconstructed, worldPoint, 2e-4f);
}

TEST(CameraSpaces, ViewNdcPointRoundTrip)
{
    const auto cam = MakeTestCamera();
    const glm::vec3 viewPoint{1.0f, -0.5f, -6.0f};

    const glm::vec3 ndc = Graphics::ViewToNDC(cam, viewPoint);
    const glm::vec3 reconstructed = Graphics::NDCToView(cam, ndc);

    ExpectVec3Near(reconstructed, viewPoint, 2e-4f);
}

TEST(CameraSpaces, ScreenNdcRoundTrip)
{
    const glm::vec2 screen{321.25f, 210.75f};
    constexpr float width = 1280.0f;
    constexpr float height = 720.0f;

    const glm::vec2 ndc = Graphics::ScreenToNDC(screen, width, height);
    const glm::vec2 reconstructed = Graphics::NDCToScreen(ndc, width, height);

    ExpectVec2Near(reconstructed, screen);
}

TEST(CameraSpaces, WorldScreenRoundTripWithDepth)
{
    const auto cam = MakeTestCamera();
    constexpr float width = 1600.0f;
    constexpr float height = 900.0f;
    const glm::vec3 worldPoint = cam.Position + cam.GetForward() * 12.0f + cam.GetRight() * 0.75f - cam.GetUp() * 0.35f;

    const glm::vec3 ndc = Graphics::WorldToNDC(cam, worldPoint);
    const glm::vec2 screen = Graphics::WorldToScreen(cam, worldPoint, width, height);
    const glm::vec3 reconstructed = Graphics::ScreenToWorld(cam, screen, width, height, ndc.z);

    ExpectVec3Near(reconstructed, worldPoint, 2e-4f);
}

TEST(CameraSpaces, ObjectWorldPointRoundTrip)
{
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3{4.0f, -2.0f, 7.0f});
    model *= glm::mat4_cast(glm::normalize(
        glm::angleAxis(glm::radians(35.0f), glm::vec3{0.0f, 1.0f, 0.0f}) *
        glm::angleAxis(glm::radians(-20.0f), glm::vec3{1.0f, 0.0f, 0.0f})));
    model = glm::scale(model, glm::vec3{2.0f, 1.5f, 0.75f});

    const glm::vec3 localPoint{0.25f, -1.0f, 2.0f};
    const glm::vec3 worldPoint = Graphics::ObjectToWorld(model, localPoint);
    const glm::vec3 reconstructed = Graphics::WorldToObject(model, worldPoint);

    ExpectVec3Near(reconstructed, localPoint, 2e-4f);
}

TEST(CameraSpaces, ObjectWorldDirectionRoundTrip)
{
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3{-3.0f, 5.0f, 1.0f});
    model *= glm::mat4_cast(glm::angleAxis(glm::radians(50.0f), glm::normalize(glm::vec3{1.0f, 2.0f, 0.5f})));
    model = glm::scale(model, glm::vec3{1.2f, 0.8f, 2.5f});

    const glm::vec3 localDir = glm::normalize(glm::vec3{0.3f, -0.7f, 0.6f});
    const glm::vec3 worldDir = Graphics::ObjectDirToWorld(model, localDir);
    const glm::vec3 reconstructed = Graphics::WorldDirToObject(model, worldDir);

    ExpectVec3Near(glm::normalize(reconstructed), localDir, 2e-4f);
}

TEST(CameraSpaces, RayFromNdcMatchesUnprojectedNearPointAndForwardDirection)
{
    const auto cam = MakeTestCamera();
    const Graphics::CameraRay ray = Graphics::RayFromNDC(cam, glm::vec2{0.0f, 0.0f});

    const glm::vec3 expectedOrigin = Graphics::NDCToWorld(cam, glm::vec3{0.0f, 0.0f, 0.0f});
    const glm::vec3 expectedFar = Graphics::NDCToWorld(cam, glm::vec3{0.0f, 0.0f, 1.0f - 1e-6f});
    const glm::vec3 expectedDir = glm::normalize(expectedFar - expectedOrigin);

    ExpectVec3Near(ray.Origin, expectedOrigin, 2e-4f);
    ExpectVec3Near(ray.Direction, expectedDir, 2e-4f);
}

TEST(CameraSpaces, RayFromScreenMatchesRayFromNdc)
{
    const auto cam = MakeTestCamera();
    constexpr float width = 1920.0f;
    constexpr float height = 1080.0f;
    const glm::vec2 screen{width * 0.37f, height * 0.61f};

    const Graphics::CameraRay rayFromScreen = Graphics::RayFromScreen(cam, screen, width, height);
    const Graphics::CameraRay rayFromNdc = Graphics::RayFromNDC(cam, Graphics::ScreenToNDC(screen, width, height));

    ExpectVec3Near(rayFromScreen.Origin, rayFromNdc.Origin, 2e-4f);
    ExpectVec3Near(rayFromScreen.Direction, rayFromNdc.Direction, 2e-4f);
}

