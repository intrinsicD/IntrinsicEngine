// Test.RegistrationAlignment.cpp — runtime ICP alignment helper (GEOM-055 /
// editor visualization slice). Verifies that Extrinsic::Runtime::AlignPointClouds
// runs ICP, captures the per-iteration convergence trajectory via the AlignICP
// observer seam, and that TrajectoryPose scrubs that trajectory. Headless
// (runtime -> geometry only; no GPU/ECS/UI).

#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

import Extrinsic.Runtime.RegistrationAlignment;
import Geometry.Registration;

namespace Runtime = Extrinsic::Runtime;
namespace Reg = Geometry::Registration;

namespace
{
    // A small deterministic point set (sphere shell) with distinct points so ICP
    // has a well-conditioned correspondence problem.
    std::vector<glm::vec3> MakeSpherePoints(int nLat = 8, int nLon = 12)
    {
        std::vector<glm::vec3> points;
        for (int i = 1; i < nLat; ++i)
        {
            const float theta =
                static_cast<float>(i) / nLat * std::numbers::pi_v<float>;
            for (int j = 0; j < nLon; ++j)
            {
                const float phi =
                    static_cast<float>(j) / nLon * 2.0f * std::numbers::pi_v<float>;
                points.emplace_back(
                    std::sin(theta) * std::cos(phi),
                    std::sin(theta) * std::sin(phi),
                    std::cos(theta));
            }
        }
        points.emplace_back(0.0f, 0.0f, 1.0f);
        points.emplace_back(0.0f, 0.0f, -1.0f);
        return points;
    }

    std::vector<glm::vec3> TransformPoints(const std::vector<glm::vec3>& points,
                                           const glm::mat4& transform)
    {
        std::vector<glm::vec3> out;
        out.reserve(points.size());
        for (const glm::vec3& p : points)
        {
            const glm::vec4 tp = transform * glm::vec4(p, 1.0f);
            out.emplace_back(tp.x, tp.y, tp.z);
        }
        return out;
    }
}

TEST(RuntimeRegistrationAlignment, RejectsDegenerateInput)
{
    const Runtime::RegistrationAlignmentOutcome outcome =
        Runtime::AlignPointClouds({}, {});
    EXPECT_FALSE(outcome.HasResult);
    EXPECT_TRUE(outcome.Traces.empty());
    // Trajectory of an empty outcome is always identity.
    const glm::mat4 pose = Runtime::TrajectoryPose(outcome, 3);
    EXPECT_FLOAT_EQ(pose[0][0], 1.0f);
    EXPECT_FLOAT_EQ(pose[3][0], 0.0f);
}

TEST(RuntimeRegistrationAlignment, CapturesConvergenceTrajectory)
{
    const auto target = MakeSpherePoints();
    const glm::mat4 perturb =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.05f, -0.03f, 0.02f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(8.0f),
                    glm::normalize(glm::vec3(1, 1, 0)));
    const auto source = TransformPoints(target, perturb);

    Reg::RegistrationParams params;
    params.Variant = Reg::ICPVariant::PointToPoint;
    params.MaxIterations = 100;

    const Runtime::RegistrationAlignmentOutcome outcome =
        Runtime::AlignPointClouds(source, target, {}, params);

    ASSERT_TRUE(outcome.HasResult);

    // One trace per completed iteration, matching the result's own accounting.
    ASSERT_EQ(outcome.Traces.size(), outcome.Result.IterationsPerformed);
    ASSERT_EQ(outcome.IterationCount(), outcome.Result.RMSEHistory.size());
    ASSERT_GT(outcome.IterationCount(), 0u);

    for (std::size_t i = 0; i < outcome.Traces.size(); ++i)
    {
        EXPECT_EQ(outcome.Traces[i].Iteration, i);
        EXPECT_TRUE(std::isfinite(outcome.Traces[i].RMSE));
        EXPECT_DOUBLE_EQ(outcome.Traces[i].RMSE, outcome.Result.RMSEHistory[i]);
    }

    // ICP should meaningfully reduce the alignment error on this easy case.
    EXPECT_LT(outcome.Result.FinalRMSE, 0.05);
}

TEST(RuntimeRegistrationAlignment, TrajectoryPoseScrubsTheTrajectory)
{
    const auto target = MakeSpherePoints();
    const auto source = TransformPoints(
        target, glm::translate(glm::mat4(1.0f), glm::vec3(0.04f, 0.0f, 0.0f)));

    Reg::RegistrationParams params;
    params.Variant = Reg::ICPVariant::PointToPoint;
    params.MaxIterations = 50;

    const Runtime::RegistrationAlignmentOutcome outcome =
        Runtime::AlignPointClouds(source, target, {}, params);
    ASSERT_TRUE(outcome.HasResult);
    ASSERT_GT(outcome.IterationCount(), 0u);

    // Step 0 is the un-registered starting pose (identity).
    const glm::mat4 start = Runtime::TrajectoryPose(outcome, 0);
    EXPECT_FLOAT_EQ(start[0][0], 1.0f);
    EXPECT_FLOAT_EQ(start[1][1], 1.0f);
    EXPECT_FLOAT_EQ(start[3][0], 0.0f);

    // The last step equals the final cumulative transform.
    const glm::mat4 last = Runtime::TrajectoryPose(outcome, outcome.IterationCount());
    const glm::mat4 expected = glm::mat4(outcome.Result.Transform);
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            EXPECT_FLOAT_EQ(last[col][row], expected[col][row]);

    // Out-of-range clamps to the final pose.
    const glm::mat4 clamped =
        Runtime::TrajectoryPose(outcome, outcome.IterationCount() + 100u);
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            EXPECT_FLOAT_EQ(clamped[col][row], last[col][row]);
}
