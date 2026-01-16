#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

import Runtime.Selection;
import Graphics;

namespace
{
    [[nodiscard]] bool IsFiniteVec3(const glm::vec3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
}

TEST(RuntimeSelection, RayFromNDC_IsSane)
{
    Graphics::CameraComponent cam{};
    cam.ViewMatrix = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    cam.ProjectionMatrix = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

    const auto ray = Runtime::Selection::RayFromNDC(cam, glm::vec2(0.0f));

    EXPECT_TRUE(IsFiniteVec3(ray.Origin));
    EXPECT_TRUE(IsFiniteVec3(ray.Direction));

    const float len = glm::length(ray.Direction);
    EXPECT_NEAR(len, 1.0f, 1e-3f);
}
