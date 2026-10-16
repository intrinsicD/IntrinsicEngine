// Compile-level renderer/RHI boundary checks. Source-text guards for this suite
// (imports, retired surfaces, backend mappings) live in tools/repo/source_rules.py.
#include <gtest/gtest.h>
#include <type_traits>

import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Types;

TEST(RendererRhiBoundary, RendererPublicSurfaceCompilesThroughRhiOnly)
{
    static_assert(std::is_polymorphic_v<Extrinsic::Graphics::IRenderer>);
    static_assert(std::is_polymorphic_v<Extrinsic::RHI::IDevice>);
    static_assert(std::is_polymorphic_v<Extrinsic::RHI::ICommandContext>);

    Extrinsic::RHI::FrameHandle frame{};
    frame.FrameIndex = 1;
    frame.SwapchainImageIndex = 2;

    EXPECT_EQ(frame.FrameIndex, 1u);
    EXPECT_EQ(frame.SwapchainImageIndex, 2u);
}

TEST(RendererRhiBoundary, SamplerBorderColorDefaultsToOpaqueBlack)
{
    const Extrinsic::RHI::SamplerDesc defaultDesc{};
    EXPECT_EQ(defaultDesc.BorderColor, Extrinsic::RHI::SamplerBorderColor::OpaqueBlackFloat);
}
