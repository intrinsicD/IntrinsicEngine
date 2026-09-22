#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

#include "MockRHI.hpp"

import Extrinsic.Graphics.RenderDiagnostics;

#include "GraphicsTestSupport.hpp"

namespace
{
    using namespace Extrinsic::Tests::GraphicsSupport;
    using Extrinsic::RHI::Format;
    static_assert(IsSrgbFormat(Format::RGBA8_SRGB) && IsSrgbFormat(Format::BGRA8_SRGB));
    static_assert(!IsSrgbFormat(Format::RGBA8_UNORM) && !IsSrgbFormat(Format::BGRA8_UNORM));

    void ExpectPixel(Readback::ExpectedPixel actual, Readback::ExpectedPixel expected)
    {
        EXPECT_EQ(actual.R, expected.R);
        EXPECT_EQ(actual.G, expected.G);
        EXPECT_EQ(actual.B, expected.B);
        EXPECT_EQ(actual.A, expected.A);
    }
}

TEST(GraphicsTestSupport, CommandPassLookupReturnsFirstMatchOrNull)
{
    Extrinsic::Graphics::RenderGraphFrameStats stats{};
    EXPECT_EQ(FindCommandPass(stats, "Surface"), nullptr);
    stats.CommandRecords.Passes.resize(3);
    stats.CommandRecords.Passes[0].Name = "Surface";
    stats.CommandRecords.Passes[1].Name = "Present";
    stats.CommandRecords.Passes[2].Name = "Surface";
    EXPECT_EQ(FindCommandPass(stats, "Surface"), &stats.CommandRecords.Passes[0]);
    EXPECT_EQ(FindCommandPass(stats, "Present"), &stats.CommandRecords.Passes[1]);
    const std::string_view suffix = "PresentSuffix";
    EXPECT_EQ(FindCommandPass(stats, suffix.substr(0, 7)), &stats.CommandRecords.Passes[1]);
    EXPECT_EQ(FindCommandPass(stats, "surface"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "Missing"), nullptr);
}

TEST(GraphicsTestSupport, ChannelOrderPreservesAlphaAndUnknownFormatFallback)
{
    for (const auto format : {Format::RGBA8_UNORM, Format::RGBA8_SRGB})
        ExpectPixel(ReorderToRgba(format, 11, 72, 203, 39), {11, 72, 203, 39});
    for (const auto format : {Format::BGRA8_UNORM, Format::BGRA8_SRGB})
        ExpectPixel(ReorderToRgba(format, 11, 72, 203, 39), {203, 72, 11, 39});
    ExpectPixel(ReorderToRgba(static_cast<Format>(0xffff), 11, 72, 203, 39),
                {11, 72, 203, 39});
}

TEST(GraphicsTestSupport, SrgbConversionRetainsUnormAndLinearAlpha)
{
    constexpr Readback::ExpectedPixel input{64, 128, 192, 128};
    for (const auto format : {Format::RGBA8_UNORM, Format::BGRA8_UNORM})
    {
        EXPECT_FALSE(IsSrgbFormat(format));
        ExpectPixel(SrgbToLinearPixel(format, input), input);
    }
    for (const auto format : {Format::RGBA8_SRGB, Format::BGRA8_SRGB})
    {
        EXPECT_TRUE(IsSrgbFormat(format));
        ExpectPixel(SrgbToLinearPixel(format, input), {13, 55, 134, 128});
    }
    ExpectPixel(SrgbToLinearPixel(static_cast<Format>(0xffff), input), input);
    EXPECT_EQ(SrgbByteToLinearByte(0), 0);
    EXPECT_EQ(SrgbByteToLinearByte(10), 1);
    EXPECT_EQ(SrgbByteToLinearByte(11), 1);
    EXPECT_EQ(SrgbByteToLinearByte(255), 255);
    for (unsigned value = 1; value < 256; ++value)
        EXPECT_GE(SrgbByteToLinearByte(static_cast<std::uint8_t>(value)),
                  SrgbByteToLinearByte(static_cast<std::uint8_t>(value - 1)));
}

TEST(GraphicsTestSupport, IndirectCountRecordingKeepsIndependentArgumentsAndOffsets)
{
    using namespace Extrinsic;
    Tests::MockCommandContext cmd;
    const RHI::BufferHandle arguments{11u, 3u};
    const RHI::BufferHandle count{12u, 4u};
    const RHI::BufferHandle indexedArguments{21u, 5u};
    const RHI::BufferHandle indexedCount{22u, 6u};
    cmd.DrawIndirectCount(arguments, 16u, count, 32u, 7u);
    cmd.DrawIndexedIndirectCount(indexedArguments, 48u, indexedCount, 64u, 9u);

    const auto& plain = cmd.LastDrawIndirectCount;
    EXPECT_EQ(plain.ArgumentBuffer, arguments);
    EXPECT_EQ(plain.ArgumentOffset, 16u);
    EXPECT_EQ(plain.CountBuffer, count);
    EXPECT_EQ(plain.CountOffset, 32u);
    EXPECT_EQ(plain.MaxDrawCount, 7u);
    const auto& indexed = cmd.LastDrawIndexedIndirectCount;
    EXPECT_EQ(indexed.ArgumentBuffer, indexedArguments);
    EXPECT_EQ(indexed.ArgumentOffset, 48u);
    EXPECT_EQ(indexed.CountBuffer, indexedCount);
    EXPECT_EQ(indexed.CountOffset, 64u);
    EXPECT_EQ(indexed.MaxDrawCount, 9u);
    EXPECT_EQ(cmd.DrawIndirectCountCalls, 1);
    EXPECT_EQ(cmd.DrawIndexedIndirectCountCalls, 1);
    EXPECT_EQ(cmd.LastMaxDrawCount, 9u);
    ASSERT_EQ(cmd.Events.size(), 2u);
    EXPECT_EQ(cmd.Events[0], Tests::MockCommandContext::EventKind::DrawIndirectCount);
    EXPECT_EQ(cmd.Events[1], Tests::MockCommandContext::EventKind::DrawIndexedIndirectCount);
}
