#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.Reconstruction;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.TextureManager;

#include "MockRHI.hpp"

using namespace Extrinsic;

namespace
{
    [[nodiscard]] Graphics::ReconstructionColorView ColorView(const std::vector<glm::vec4>& pixels,
                                                              const Core::Extent2D extent) noexcept
    {
        return Graphics::ReconstructionColorView{.Pixels = pixels, .Extent = extent};
    }

    [[nodiscard]] Graphics::ReconstructionDepthView DepthView(const std::vector<float>& pixels,
                                                              const Core::Extent2D extent) noexcept
    {
        return Graphics::ReconstructionDepthView{.Pixels = pixels, .Extent = extent};
    }

    [[nodiscard]] Graphics::ReconstructionMotionVectorView MotionView(const std::vector<glm::vec2>& pixels,
                                                                      const Core::Extent2D extent) noexcept
    {
        return Graphics::ReconstructionMotionVectorView{.Pixels = pixels, .Extent = extent};
    }

    [[nodiscard]] Graphics::ReconstructionOutputView OutputView(std::vector<glm::vec4>& pixels,
                                                                const Core::Extent2D extent) noexcept
    {
        return Graphics::ReconstructionOutputView{.Pixels = pixels, .Extent = extent};
    }

    [[nodiscard]] Graphics::ReconstructionHints Hints(const Core::Extent2D extent) noexcept
    {
        return Graphics::ReconstructionHints{
            .Sharpness = 0.0f,
            .Exposure = 1.0f,
            .JitterOffset = {0.f, 0.f},
            .FrameIndex = 7u,
            .InputExtent = extent,
            .OutputExtent = extent,
            .Reset = false,
        };
    }
}

TEST(GraphicsReconstructionContract, InterfaceRejectsInvalidInputsFailClosed)
{
    constexpr Core::Extent2D extent{2u, 2u};
    std::vector<glm::vec4> current(4u, glm::vec4{0.25f, 0.25f, 0.25f, 1.f});
    std::vector<float> depth(3u, 1.f);
    std::vector<glm::vec2> motion(4u, glm::vec2{0.f});
    std::vector<glm::vec4> history(4u, glm::vec4{0.5f, 0.5f, 0.5f, 1.f});
    std::vector<glm::vec4> output(4u, glm::vec4{-1.f});

    Graphics::ReferenceTAAReconstructor reconstructor;
    const Graphics::ReconstructionResult result = reconstructor.Apply(
        ColorView(current, extent),
        DepthView(depth, extent),
        MotionView(motion, extent),
        ColorView(history, extent),
        OutputView(output, extent),
        Hints(extent));

    EXPECT_FALSE(result.Applied);
    EXPECT_EQ(result.FailReason, Graphics::ReconstructionFailReason::InvalidInput);
    EXPECT_EQ(output[0], glm::vec4{-1.f});
}

TEST(GraphicsReconstructionContract, ResetInvalidatesHistoryAndCopiesCurrent)
{
    constexpr Core::Extent2D extent{2u, 2u};
    std::vector<glm::vec4> current{
        {0.1f, 0.2f, 0.3f, 1.f},
        {0.2f, 0.3f, 0.4f, 1.f},
        {0.3f, 0.4f, 0.5f, 1.f},
        {0.4f, 0.5f, 0.6f, 1.f},
    };
    std::vector<float> depth(4u, 1.f);
    std::vector<glm::vec2> motion(4u, glm::vec2{0.f});
    std::vector<glm::vec4> history(4u, glm::vec4{10.f, 10.f, 10.f, 1.f});
    std::vector<glm::vec4> output(4u, glm::vec4{0.f});
    auto hints = Hints(extent);
    hints.Reset = true;

    Graphics::ReferenceTAAReconstructor reconstructor;
    const Graphics::ReconstructionResult result = reconstructor.Apply(
        ColorView(current, extent),
        DepthView(depth, extent),
        MotionView(motion, extent),
        ColorView(history, extent),
        OutputView(output, extent),
        hints);

    EXPECT_FALSE(result.Applied);
    EXPECT_EQ(result.FailReason, Graphics::ReconstructionFailReason::ResetHistory);
    EXPECT_EQ(result.DisoccludedPixelCount, 4u);
    EXPECT_FLOAT_EQ(result.DisocclusionPercent, 1.f);
    EXPECT_EQ(output, current);
}

TEST(GraphicsReconstructionContract, ReferenceTAAClipsHistoryAgainstFiveByFiveYCoCgNeighborhood)
{
    constexpr Core::Extent2D extent{5u, 5u};
    std::vector<glm::vec4> current(25u, glm::vec4{0.25f, 0.25f, 0.25f, 1.f});
    std::vector<float> depth(25u, 1.f);
    std::vector<glm::vec2> motion(25u, glm::vec2{0.f});
    std::vector<glm::vec4> history(25u, glm::vec4{8.f, 8.f, 8.f, 1.f});
    std::vector<glm::vec4> output(25u, glm::vec4{0.f});

    Graphics::ReferenceTAAReconstructor reconstructor;
    const Graphics::ReconstructionResult result = reconstructor.Apply(
        ColorView(current, extent),
        DepthView(depth, extent),
        MotionView(motion, extent),
        ColorView(history, extent),
        OutputView(output, extent),
        Hints(extent));

    ASSERT_TRUE(result.Applied);
    EXPECT_EQ(result.FailReason, Graphics::ReconstructionFailReason::None);
    const glm::vec4 center = output[12u];
    EXPECT_NEAR(center.r, 0.25f, 0.0001f);
    EXPECT_NEAR(center.g, 0.25f, 0.0001f);
    EXPECT_NEAR(center.b, 0.25f, 0.0001f);
}

TEST(GraphicsReconstructionContract, ExposureWeightingReducesHistoryContribution)
{
    constexpr Core::Extent2D extent{5u, 5u};
    std::vector<glm::vec4> current(25u, glm::vec4{0.45f, 0.45f, 0.45f, 1.f});
    for (std::size_t i = 0u; i < current.size(); i += 2u)
    {
        current[i] = glm::vec4{0.65f, 0.65f, 0.65f, 1.f};
    }
    current[12u] = glm::vec4{0.45f, 0.45f, 0.45f, 1.f};

    std::vector<float> depth(25u, 1.f);
    std::vector<glm::vec2> motion(25u, glm::vec2{0.f});
    std::vector<glm::vec4> history(25u, glm::vec4{0.70f, 0.70f, 0.70f, 1.f});
    std::vector<glm::vec4> lowExposureOutput(25u, glm::vec4{0.f});
    std::vector<glm::vec4> highExposureOutput(25u, glm::vec4{0.f});

    Graphics::ReferenceTAAReconstructor reconstructor;
    auto lowHints = Hints(extent);
    lowHints.Exposure = 0.5f;
    auto highHints = Hints(extent);
    highHints.Exposure = 8.0f;

    ASSERT_TRUE(reconstructor.Apply(
        ColorView(current, extent),
        DepthView(depth, extent),
        MotionView(motion, extent),
        ColorView(history, extent),
        OutputView(lowExposureOutput, extent),
        lowHints).Applied);
    ASSERT_TRUE(reconstructor.Apply(
        ColorView(current, extent),
        DepthView(depth, extent),
        MotionView(motion, extent),
        ColorView(history, extent),
        OutputView(highExposureOutput, extent),
        highHints).Applied);

    EXPECT_GT(lowExposureOutput[12u].r, highExposureOutput[12u].r)
        << "Higher exposure should reduce history contribution when luminance differs.";
    EXPECT_GT(highExposureOutput[12u].r, current[12u].r);
}

TEST(GraphicsReconstructionContract, DisocclusionFallbackReportsFraction)
{
    constexpr Core::Extent2D extent{2u, 1u};
    std::vector<glm::vec4> current{
        {0.1f, 0.2f, 0.3f, 1.f},
        {0.4f, 0.5f, 0.6f, 1.f},
    };
    std::vector<float> depth(2u, 1.f);
    std::vector<glm::vec2> motion{
        {0.f, 0.f},
        {3.f, 0.f},
    };
    std::vector<glm::vec4> history(2u, glm::vec4{0.9f, 0.9f, 0.9f, 1.f});
    std::vector<glm::vec4> output(2u, glm::vec4{0.f});

    Graphics::ReferenceTAAReconstructor reconstructor;
    const Graphics::ReconstructionResult result = reconstructor.Apply(
        ColorView(current, extent),
        DepthView(depth, extent),
        MotionView(motion, extent),
        ColorView(history, extent),
        OutputView(output, extent),
        Hints(extent));

    EXPECT_TRUE(result.Applied);
    EXPECT_EQ(result.DisoccludedPixelCount, 1u);
    EXPECT_FLOAT_EQ(result.DisocclusionPercent, 0.5f);
    EXPECT_EQ(output[1u], current[1u]);
}

TEST(GraphicsReconstructionHistory, PingPongHistoryRetiresOldPairThroughWindow)
{
    constexpr std::uint32_t kFramesInFlight = 2u;
    Tests::MockDevice device;
    RHI::TextureManager textureMgr{device, device.GetBindlessHeap()};

    Graphics::ReconstructionHistorySystem history;
    history.Initialize(device, textureMgr);
    EXPECT_TRUE(history.IsInitialized());
    EXPECT_FALSE(history.IsAllocated());

    ASSERT_TRUE(history.EnsureAllocated(1280u, 720u, 0u));
    EXPECT_TRUE(history.IsAllocated());
    EXPECT_EQ(history.GetAllocatedDesc().Fmt, RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(device.CreateTextureCount, 2);
    const RHI::TextureHandle current0 = history.CurrentHistory();
    const RHI::TextureHandle previous0 = history.PreviousHistory();
    EXPECT_TRUE(current0.IsValid());
    EXPECT_TRUE(previous0.IsValid());
    EXPECT_NE(current0, previous0);

    history.AdvanceFrame();
    EXPECT_EQ(history.CurrentHistory(), previous0);
    EXPECT_EQ(history.PreviousHistory(), current0);

    ASSERT_TRUE(history.EnsureAllocated(1920u, 1080u, 5u));
    EXPECT_EQ(device.CreateTextureCount, 4);
    EXPECT_EQ(device.DestroyTextureCount, 0);
    EXPECT_EQ(history.GetDiagnostics().ReallocationCount, 1u);

    history.Tick(5u + kFramesInFlight - 1u, kFramesInFlight);
    EXPECT_EQ(device.DestroyTextureCount, 0);
    EXPECT_EQ(history.GetDiagnostics().PendingRetireCount, 2u);

    history.Tick(5u + kFramesInFlight, kFramesInFlight);
    EXPECT_EQ(device.DestroyTextureCount, 2);
    EXPECT_EQ(history.GetDiagnostics().RetiredTextureCount, 2u);
    EXPECT_EQ(history.GetDiagnostics().PendingRetireCount, 0u);

    history.Shutdown();
    EXPECT_EQ(device.DestroyTextureCount, 4);
    EXPECT_FALSE(history.IsAllocated());
    EXPECT_FALSE(history.IsInitialized());
}
