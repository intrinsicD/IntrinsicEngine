#include <gtest/gtest.h>
#include <cstdint>
#include <cmath>

import Graphics;

using namespace Graphics;

// =============================================================================
// Colormap — unit tests for scientific colormaps.
// =============================================================================

// Helper: extract R channel from packed ABGR.
static uint8_t R(uint32_t c) { return static_cast<uint8_t>(c & 0xFF); }
static uint8_t G(uint32_t c) { return static_cast<uint8_t>((c >> 8) & 0xFF); }
static uint8_t B(uint32_t c) { return static_cast<uint8_t>((c >> 16) & 0xFF); }
static uint8_t A(uint32_t c) { return static_cast<uint8_t>((c >> 24) & 0xFF); }

TEST(Colormap, AllMapsReturnFullAlpha)
{
    for (uint32_t m = 0; m < Colormap::Count; ++m)
    {
        auto map = static_cast<Colormap::Type>(m);
        for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        {
            uint32_t c = Colormap::Sample(map, t);
            EXPECT_EQ(A(c), 255) << "Map " << Colormap::Name(map) << " t=" << t;
        }
    }
}

TEST(Colormap, ClampsOutOfRange)
{
    auto c_neg = Colormap::Sample(Colormap::Type::Viridis, -1.0f);
    auto c_zero = Colormap::Sample(Colormap::Type::Viridis, 0.0f);
    EXPECT_EQ(c_neg, c_zero);

    auto c_over = Colormap::Sample(Colormap::Type::Viridis, 2.0f);
    auto c_one = Colormap::Sample(Colormap::Type::Viridis, 1.0f);
    EXPECT_EQ(c_over, c_one);
}

TEST(Colormap, ViridisEndpoints)
{
    // Viridis starts dark purple, ends bright yellow.
    auto c0 = Colormap::Sample(Colormap::Type::Viridis, 0.0f);
    auto c1 = Colormap::Sample(Colormap::Type::Viridis, 1.0f);

    // t=0: dark, t=1: bright
    float lum0 = 0.299f * R(c0) + 0.587f * G(c0) + 0.114f * B(c0);
    float lum1 = 0.299f * R(c1) + 0.587f * G(c1) + 0.114f * B(c1);
    EXPECT_LT(lum0, lum1) << "Viridis should go dark to bright";
}

TEST(Colormap, JetIsCyanAtQuarter)
{
    // Jet at t=0.375 should be near cyan (G and B high, R low).
    auto c = Colormap::Sample(Colormap::Type::Jet, 0.375f);
    EXPECT_LT(R(c), 50);
    EXPECT_GT(G(c), 150);
    EXPECT_GT(B(c), 150);
}

TEST(Colormap, CoolwarmDivergingCenter)
{
    // Coolwarm at t=0.5 should be near white/grey.
    auto c = Colormap::Sample(Colormap::Type::Coolwarm, 0.5f);
    // All channels should be close to each other and bright.
    EXPECT_GT(R(c), 180);
    EXPECT_GT(G(c), 180);
    EXPECT_GT(B(c), 180);
}

TEST(Colormap, SampleBinnedQuantizes)
{
    // With 2 bins, any t in [0, 0.5) should map to the same color,
    // and any t in [0.5, 1.0] to another.
    auto c_low1 = Colormap::SampleBinned(Colormap::Type::Viridis, 0.1f, 2);
    auto c_low2 = Colormap::SampleBinned(Colormap::Type::Viridis, 0.4f, 2);
    EXPECT_EQ(c_low1, c_low2);

    auto c_hi1 = Colormap::SampleBinned(Colormap::Type::Viridis, 0.6f, 2);
    auto c_hi2 = Colormap::SampleBinned(Colormap::Type::Viridis, 0.9f, 2);
    EXPECT_EQ(c_hi1, c_hi2);

    EXPECT_NE(c_low1, c_hi1);
}

TEST(Colormap, SampleBinnedZeroBinsIsContinuous)
{
    auto c_binned = Colormap::SampleBinned(Colormap::Type::Inferno, 0.33f, 0);
    auto c_cont = Colormap::Sample(Colormap::Type::Inferno, 0.33f);
    EXPECT_EQ(c_binned, c_cont);
}

TEST(Colormap, NameReturnsNonNull)
{
    for (uint32_t m = 0; m < Colormap::Count; ++m)
    {
        auto map = static_cast<Colormap::Type>(m);
        const char* name = Colormap::Name(map);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::strlen(name), 0u);
    }
}

TEST(Colormap, HeatEndpoints)
{
    // Heat at t=0 should be blue (B high, R/G low).
    auto c0 = Colormap::Sample(Colormap::Type::Heat, 0.0f);
    EXPECT_GT(B(c0), 200);
    EXPECT_LT(R(c0), 50);

    // Heat at t=1 should be red (R high, G/B low).
    auto c1 = Colormap::Sample(Colormap::Type::Heat, 1.0f);
    EXPECT_GT(R(c1), 200);
    EXPECT_LT(G(c1), 50);
}
