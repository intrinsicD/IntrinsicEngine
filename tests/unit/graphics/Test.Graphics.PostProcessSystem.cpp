#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

import Extrinsic.Graphics.PostProcessSystem;

using namespace Extrinsic;

TEST(GraphicsPostProcessSystem, DefaultChainToneMapsHdrToLdr)
{
    Graphics::PostProcessSystem post;
    post.Initialize();

    const Graphics::PostProcessChainDesc chain = post.DescribeChain();

    ASSERT_TRUE(chain.Enabled);
    ASSERT_TRUE(chain.WritesLDR);
    ASSERT_EQ(chain.Stages.size(), 1u);
    EXPECT_EQ(chain.Stages[0].Kind, Graphics::PostProcessStageKind::ToneMap);
    EXPECT_TRUE(chain.Stages[0].ReadsHDR);
    EXPECT_TRUE(chain.Stages[0].WritesLDR);
    EXPECT_TRUE(post.IsStageEnabled(Graphics::PostProcessStageKind::ToneMap));
    EXPECT_FALSE(post.IsStageEnabled(Graphics::PostProcessStageKind::Bloom));
}

TEST(GraphicsPostProcessSystem, OptionalStagesHaveDeterministicOrder)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .EnableHistogram = true,
        .EnableBloom = true,
        .AntiAliasing = Graphics::PostProcessAntiAliasing::SMAA,
        .Exposure = 1.5f,
        .Gamma = 2.4f,
        .BloomIntensity = 0.25f,
        .HistogramBinCount = 1024u,
    });

    const Graphics::PostProcessChainDesc chain = post.DescribeChain();
    const std::vector<Graphics::PostProcessStageKind> expected{
        Graphics::PostProcessStageKind::Histogram,
        Graphics::PostProcessStageKind::Bloom,
        Graphics::PostProcessStageKind::ToneMap,
        Graphics::PostProcessStageKind::SMAA,
    };

    ASSERT_EQ(chain.Stages.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        EXPECT_EQ(chain.Stages[i].Kind, expected[i]);
    }

    const Graphics::PostProcessPushConstants pc = post.BuildPushConstants(Graphics::PostProcessStageKind::ToneMap);
    EXPECT_FLOAT_EQ(pc.Exposure, 1.5f);
    EXPECT_FLOAT_EQ(pc.Gamma, 2.4f);
    EXPECT_FLOAT_EQ(pc.BloomIntensity, 0.25f);
    EXPECT_EQ(pc.HistogramBinCount, 1024u);
    EXPECT_EQ(pc.StageKind, static_cast<std::uint32_t>(Graphics::PostProcessStageKind::ToneMap));
}

TEST(GraphicsPostProcessSystem, DisabledChainReportsNoStagesOrLdrWrite)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{.Enabled = false});

    const Graphics::PostProcessChainDesc chain = post.DescribeChain();

    EXPECT_FALSE(chain.Enabled);
    EXPECT_FALSE(chain.WritesLDR);
    EXPECT_TRUE(chain.Stages.empty());
    EXPECT_FALSE(post.IsStageEnabled(Graphics::PostProcessStageKind::ToneMap));
    EXPECT_FALSE(post.GetDiagnostics().WritesLDR);
}

TEST(GraphicsPostProcessSystem, InvalidSettingsAreSanitizedAndDiagnosed)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .AntiAliasing = static_cast<Graphics::PostProcessAntiAliasing>(255u),
        .Exposure = -1.0f,
        .Gamma = std::numeric_limits<float>::quiet_NaN(),
        .BloomIntensity = -0.5f,
        .HistogramBinCount = 0u,
    });

    const Graphics::PostProcessSettings& settings = post.GetSettings();
    EXPECT_EQ(settings.AntiAliasing, Graphics::PostProcessAntiAliasing::None);
    EXPECT_FLOAT_EQ(settings.Exposure, 1.0f);
    EXPECT_FLOAT_EQ(settings.Gamma, 2.2f);
    EXPECT_FLOAT_EQ(settings.BloomIntensity, 0.05f);
    EXPECT_EQ(settings.HistogramBinCount, 256u);

    const Graphics::PostProcessDiagnostics diagnostics = post.GetDiagnostics();
    EXPECT_EQ(diagnostics.InvalidSettingCount, 4u);
    EXPECT_EQ(diagnostics.UnsupportedCombinationCount, 1u);
    EXPECT_TRUE(diagnostics.ChainEnabled);
    EXPECT_TRUE(diagnostics.WritesLDR);
}

