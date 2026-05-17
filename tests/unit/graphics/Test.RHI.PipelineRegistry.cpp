#include <gtest/gtest.h>

import Extrinsic.Core.Error;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.PipelineRegistry;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.Transfer;
import Extrinsic.Core.Config.Render;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    RHI::PipelineDesc GraphicsPipeline(const char* fragment = "shaders/test.frag.spv")
    {
        RHI::PipelineDesc desc{};
        desc.VertexShaderPath = "shaders/test.vert.spv";
        desc.FragmentShaderPath = fragment;
        desc.ColorTargetFormats[0] = RHI::Format::RGBA8_UNORM;
        desc.DepthTargetFormat = RHI::Format::D32_FLOAT;
        desc.PushConstantSize = 16u;
        desc.DebugName = "registry-test-pipeline";
        return desc;
    }

    RHI::PipelineDesc ComputePipeline()
    {
        RHI::PipelineDesc desc{};
        desc.ComputeShaderPath = "shaders/test.comp.spv";
        desc.DebugName = "registry-test-compute";
        return desc;
    }
}

TEST(RHIPipelineRegistry, MakePipelineKeyIsStableAndGenerationSensitive)
{
    const auto desc = GraphicsPipeline();

    const auto a = RHI::MakePipelineKey(desc, 1u, 2u);
    const auto b = RHI::MakePipelineKey(desc, 1u, 2u);
    auto c = RHI::MakePipelineKey(desc, 1u, 3u);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);

    c = RHI::MakePipelineKey(desc, 1u, 2u);
    c.State.PushConstantSize = 32u;
    EXPECT_NE(a, c);
}

TEST(RHIPipelineRegistry, GetOrCreateCachesPipelineByStableKey)
{
    MockDevice device;
    RHI::PipelineManager manager{device};
    RHI::PipelineRegistry registry{manager};
    const auto desc = GraphicsPipeline();
    const auto key = RHI::MakePipelineKey(desc);

    const auto first = registry.GetOrCreatePipeline(key, desc);
    ASSERT_TRUE(first.has_value());
    const auto second = registry.GetOrCreatePipeline(key, desc);
    ASSERT_TRUE(second.has_value());

    EXPECT_EQ(*first, *second);
    EXPECT_EQ(device.CreatePipelineCount, 1);

    const auto diagnostics = registry.GetDiagnostics();
    EXPECT_EQ(diagnostics.CacheMissCount, 1u);
    EXPECT_EQ(diagnostics.CacheHitCount, 1u);
    EXPECT_EQ(diagnostics.LivePipelineCount, 1u);
}

TEST(RHIPipelineRegistry, MissingShaderAndMismatchedKeyAreDeterministicFailures)
{
    MockDevice device;
    RHI::PipelineManager manager{device};
    RHI::PipelineRegistry registry{manager};

    auto desc = GraphicsPipeline();
    auto missing = RHI::MakePipelineKey(desc);
    missing.Fragment.Path.clear();

    const auto missingResult = registry.GetOrCreatePipeline(missing, desc);
    ASSERT_FALSE(missingResult.has_value());
    EXPECT_EQ(missingResult.error(), Core::ErrorCode::ResourceNotFound);

    auto mismatched = RHI::MakePipelineKey(desc);
    desc.FragmentShaderPath = "shaders/other.frag.spv";
    const auto mismatchResult = registry.GetOrCreatePipeline(mismatched, desc);
    ASSERT_FALSE(mismatchResult.has_value());
    EXPECT_EQ(mismatchResult.error(), Core::ErrorCode::InvalidArgument);

    const auto diagnostics = registry.GetDiagnostics();
    EXPECT_EQ(diagnostics.MissingShaderCount, 1u);
    EXPECT_EQ(diagnostics.InvalidKeyCount, 1u);
    EXPECT_EQ(device.CreatePipelineCount, 0);
}

TEST(RHIPipelineRegistry, PipelineCreationFailuresAreDiagnostic)
{
    MockDevice device;
    device.FailNextPipelineCreate = true;
    RHI::PipelineManager manager{device};
    RHI::PipelineRegistry registry{manager};
    const auto desc = GraphicsPipeline();

    const auto result = registry.GetOrCreatePipeline(RHI::MakePipelineKey(desc), desc);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::PipelineCreationFailed);

    const auto diagnostics = registry.GetDiagnostics();
    EXPECT_EQ(diagnostics.CacheMissCount, 1u);
    EXPECT_EQ(diagnostics.PipelineCreationFailureCount, 1u);
    EXPECT_EQ(diagnostics.LivePipelineCount, 0u);
}

TEST(RHIPipelineRegistry, ShaderInvalidationDropsAffectedCacheEntries)
{
    MockDevice device;
    RHI::PipelineManager manager{device};
    RHI::PipelineRegistry registry{manager};

    const auto surfaceDesc = GraphicsPipeline();
    const auto lineDesc = GraphicsPipeline("shaders/line.frag.spv");
    const auto surface = registry.GetOrCreatePipeline(RHI::MakePipelineKey(surfaceDesc), surfaceDesc);
    const auto line = registry.GetOrCreatePipeline(RHI::MakePipelineKey(lineDesc), lineDesc);
    ASSERT_TRUE(surface.has_value());
    ASSERT_TRUE(line.has_value());
    ASSERT_NE(*surface, *line);

    EXPECT_EQ(registry.InvalidateShaderPath("shaders/test.frag.spv"), 1u);
    EXPECT_EQ(device.DestroyPipelineCount, 1);
    EXPECT_EQ(registry.GetDiagnostics().LivePipelineCount, 1u);

    const auto recreated = registry.GetOrCreatePipeline(RHI::MakePipelineKey(surfaceDesc, 0u, 1u), surfaceDesc);
    ASSERT_TRUE(recreated.has_value());
    EXPECT_NE(*surface, *recreated);
    EXPECT_EQ(device.CreatePipelineCount, 3);

    const auto diagnostics = registry.GetDiagnostics();
    EXPECT_EQ(diagnostics.ReloadInvalidationCount, 1u);
    EXPECT_EQ(diagnostics.LivePipelineCount, 2u);
}

TEST(RHIPipelineRegistry, ComputePipelineRequiresOnlyComputeShader)
{
    MockDevice device;
    RHI::PipelineManager manager{device};
    RHI::PipelineRegistry registry{manager};
    const auto desc = ComputePipeline();

    const auto result = registry.GetOrCreatePipeline(RHI::MakePipelineKey(desc, 0u, 0u, 7u), desc);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    EXPECT_EQ(device.CreatePipelineCount, 1);
}


