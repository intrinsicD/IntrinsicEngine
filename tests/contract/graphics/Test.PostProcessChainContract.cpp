#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.Pass.PostProcess.Bloom;
import Extrinsic.Graphics.Pass.PostProcess.FXAA;
import Extrinsic.Graphics.Pass.PostProcess.Histogram;
import Extrinsic.Graphics.Pass.PostProcess.SMAA;
import Extrinsic.Graphics.Pass.PostProcess.ToneMap;
import Extrinsic.Graphics.PostProcessSystem;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

using namespace Extrinsic;

namespace
{
    enum class EventKind
    {
        BindPipeline,
        PushConstants,
        Draw,
        Dispatch,
    };

    struct Event
    {
        EventKind Kind{};
    };

    class RecordingCommandContext final : public RHI::ICommandContext
    {
    public:
        std::vector<Event> Events{};
        RHI::PipelineHandle LastPipeline{};
        std::uint32_t LastDrawVertexCount{0u};
        std::uint32_t LastDispatchX{0u};
        std::uint32_t LastPushConstantSize{0u};
        std::vector<std::byte> LastPushConstants{};

        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle pipeline) override
        {
            Events.push_back({.Kind = EventKind::BindPipeline});
            LastPipeline = pipeline;
        }
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, RHI::IndexType) override {}
        void PushConstants(const void* data, std::uint32_t size, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::PushConstants});
            LastPushConstantSize = size;
            LastPushConstants.resize(size);
            if (data != nullptr && size > 0u)
            {
                std::memcpy(LastPushConstants.data(), data, size);
            }
        }
        void Draw(std::uint32_t vertexCount, std::uint32_t, std::uint32_t, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::Draw});
            LastDrawVertexCount = vertexCount;
        }
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void Dispatch(std::uint32_t groupX, std::uint32_t, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::Dispatch});
            LastDispatchX = groupX;
        }
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess, RHI::MemoryAccess) override {}
        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
    };

    [[nodiscard]] Graphics::FrameRecipeImports MakeImports()
    {
        return Graphics::FrameRecipeImports{
            .Backbuffer = RHI::TextureHandle{1u, 1u},
            .SceneTable = RHI::BufferHandle{2u, 1u},
            .InstanceStatic = RHI::BufferHandle{3u, 1u},
            .InstanceDynamic = RHI::BufferHandle{4u, 1u},
            .EntityConfig = RHI::BufferHandle{5u, 1u},
            .GeometryRecords = RHI::BufferHandle{6u, 1u},
            .Bounds = RHI::BufferHandle{7u, 1u},
            .Lights = RHI::BufferHandle{8u, 1u},
            .MaterialBuffer = RHI::BufferHandle{9u, 1u},
            .SurfaceOpaqueIndexedArgs = RHI::BufferHandle{10u, 1u},
            .SurfaceOpaqueCount = RHI::BufferHandle{11u, 1u},
            .LinesIndexedArgs = RHI::BufferHandle{12u, 1u},
            .LinesCount = RHI::BufferHandle{13u, 1u},
            .PointsNonIndexedArgs = RHI::BufferHandle{14u, 1u},
            .PointsCount = RHI::BufferHandle{15u, 1u},
        };
    }

    [[nodiscard]] const Graphics::FrameRecipePassDeclaration* FindPass(const Graphics::FrameRecipeIntrospection& description,
                                                                       const Graphics::FrameRecipePassKind kind)
    {
        for (const Graphics::FrameRecipePassDeclaration& pass : description.Passes)
        {
            if (pass.Kind == kind)
            {
                return &pass;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool HasResource(const Graphics::FrameRecipeIntrospection& description,
                                   const Graphics::FrameRecipeResourceKind kind,
                                   const bool enabled)
    {
        for (const Graphics::FrameRecipeResourceDeclaration& resource : description.Resources)
        {
            if (resource.Kind == kind)
            {
                return resource.Enabled == enabled;
            }
        }
        return false;
    }

    [[nodiscard]] bool HasResourceName(const Graphics::FrameRecipeIntrospection& description,
                                       const std::string_view name,
                                       const bool enabled)
    {
        for (const Graphics::FrameRecipeResourceDeclaration& resource : description.Resources)
        {
            if (resource.Name == name)
            {
                return resource.Enabled == enabled;
            }
        }
        return false;
    }

    [[nodiscard]] bool Contains(const std::vector<std::string_view>& values, const std::string_view value)
    {
        for (const std::string_view candidate : values)
        {
            if (candidate == value)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::vector<std::string> OrderedPassNames(const Graphics::CompiledRenderGraph& compiled)
    {
        std::vector<std::string> names{};
        for (const std::uint32_t index : compiled.TopologicalOrder)
        {
            names.push_back(compiled.PassNames[index]);
        }
        return names;
    }
}

TEST(GraphicsPostProcessChainContract, FrameRecipeDeclaresHdrToLdrResources)
{
    const Graphics::FrameRecipeIntrospection description = Graphics::DescribeDefaultFrameRecipe(Graphics::FrameRecipeFeatures{});

    const auto* post = FindPass(description, Graphics::FrameRecipePassKind::PostProcess);
    ASSERT_NE(post, nullptr);
    EXPECT_TRUE(post->Enabled);
    EXPECT_TRUE(Contains(post->Reads, "SceneColorHDR"));
    EXPECT_TRUE(Contains(post->Writes, "SceneColorLDR"));
    EXPECT_TRUE(Contains(post->Writes, "PostProcess.BloomScratch"));
    EXPECT_TRUE(Contains(post->Writes, "PostProcess.Histogram"));
    EXPECT_TRUE(Contains(post->Writes, "PostProcess.AATemp"));

    EXPECT_TRUE(HasResource(description, Graphics::FrameRecipeResourceKind::SceneColorLDR, true));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.BloomScratch", true));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.Histogram", true));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.AATemp", true));
}

TEST(GraphicsPostProcessChainContract, PostprocessGateControlsPresentSource)
{
    Graphics::FrameRecipeFeatures features{};
    features.EnablePostProcess = false;

    const Graphics::FrameRecipeIntrospection description = Graphics::DescribeDefaultFrameRecipe(features);
    const auto* post = FindPass(description, Graphics::FrameRecipePassKind::PostProcess);
    ASSERT_NE(post, nullptr);
    EXPECT_FALSE(post->Enabled);
    EXPECT_TRUE(HasResource(description, Graphics::FrameRecipeResourceKind::SceneColorLDR, false));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.BloomScratch", false));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.Histogram", false));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.AATemp", false));

    Graphics::RenderGraph graph;
    const Graphics::FrameRecipeBuildResult build = Graphics::BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        Graphics::FrameRecipeSizing{.Width = 320u, .Height = 180u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    const auto& compileResult = graph.GetLastCompileValidationResult();
    ASSERT_TRUE(compiled.has_value())
        << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    const std::vector<std::string> order = OrderedPassNames(*compiled);
    EXPECT_EQ(std::find(order.begin(), order.end(), "PostProcessPass"), order.end());
    EXPECT_NE(std::find(order.begin(), order.end(), "Present"), order.end());
}

TEST(GraphicsPostProcessChainContract, PassesRecordOnlyEnabledStages)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .EnableHistogram = true,
        .EnableBloom = true,
        .AntiAliasing = Graphics::PostProcessAntiAliasing::FXAA,
        .Exposure = 1.25f,
    });

    RHI::CameraUBO camera{};

    Graphics::PostProcessToneMapPass toneMap{post};
    toneMap.SetPipeline(RHI::PipelineHandle{20u, 1u});
    RecordingCommandContext toneMapCmd;
    toneMap.Execute(toneMapCmd, camera);
    ASSERT_EQ(toneMapCmd.Events.size(), 3u);
    EXPECT_EQ(toneMapCmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(toneMapCmd.Events[1].Kind, EventKind::PushConstants);
    EXPECT_EQ(toneMapCmd.Events[2].Kind, EventKind::Draw);
    EXPECT_EQ(toneMapCmd.LastDrawVertexCount, 3u);
    // GRAPHICS-075 Slice A — the pass body now pushes the pass-local
    // `PostProcessToneMapPushConstants` block (80 bytes, std430-matched to
    // `post_tonemap.frag`) so the shader actually sees `Exposure` /
    // `BloomIntensity` plus deterministic ACES / no-grading defaults
    // instead of the canonical 20-byte block aliasing
    // `HistogramBinCount`/`StageKind` onto `ColorGradingOn`/`Saturation`.
    EXPECT_EQ(toneMapCmd.LastPushConstantSize,
              sizeof(Graphics::PostProcessToneMapPushConstants));
    ASSERT_EQ(toneMapCmd.LastPushConstants.size(),
              sizeof(Graphics::PostProcessToneMapPushConstants));
    Graphics::PostProcessToneMapPushConstants observed{};
    std::memcpy(&observed, toneMapCmd.LastPushConstants.data(),
                sizeof(Graphics::PostProcessToneMapPushConstants));
    EXPECT_FLOAT_EQ(observed.Exposure, 1.25f);
    EXPECT_FLOAT_EQ(observed.BloomIntensity, 0.05f);
    EXPECT_EQ(observed.Operator, 0);          // ACES
    EXPECT_EQ(observed.ColorGradingOn, 0);    // grading off by default
    EXPECT_FLOAT_EQ(observed.Saturation, 1.0f);
    EXPECT_FLOAT_EQ(observed.Contrast, 1.0f);
    EXPECT_FLOAT_EQ(observed.Lift[0], 0.0f);
    EXPECT_FLOAT_EQ(observed.Gamma[0], 1.0f);
    EXPECT_FLOAT_EQ(observed.Gain[0], 1.0f);

    // GRAPHICS-075 Slice B.1 — bloom pass now records a downsample +
    // upsample bind/push/draw pair (one fullscreen draw per stage) using
    // the per-shader push-constant blocks. Slice B.2 expands this to per-
    // mip iteration; for B.1 the helper records the single-step
    // placeholder shape so the CPU contract gate observes both pipelines
    // are bound. The push payloads pack `vec2 InvSrcResolution + float
    // Threshold + int IsFirstMip` for downsample (16B) and `vec2
    // InvCoarserResolution + float FilterRadius + float _pad0` for
    // upsample (16B), matching the shader std430 layouts. The camera UBO
    // carries non-zero viewport dimensions so the per-stage builders feed
    // the shaders real per-tap kernel offsets (`InvSrcResolution =
    // 1 / vec2(W, H)` etc.) — feeding zero dimensions would collapse
    // every sample tap onto the same texel and silently break the
    // spatial filter shape.
    RHI::CameraUBO bloomCamera{};
    bloomCamera.ViewportWidth = 1280.0f;
    bloomCamera.ViewportHeight = 720.0f;

    Graphics::PostProcessBloomPass bloom{post};
    bloom.SetDownsamplePipeline(RHI::PipelineHandle{21u, 1u});
    bloom.SetUpsamplePipeline(RHI::PipelineHandle{25u, 1u});
    RecordingCommandContext bloomCmd;
    bloom.Execute(bloomCmd, bloomCamera);
    ASSERT_EQ(bloomCmd.Events.size(), 6u);
    EXPECT_EQ(bloomCmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(bloomCmd.Events[1].Kind, EventKind::PushConstants);
    EXPECT_EQ(bloomCmd.Events[2].Kind, EventKind::Draw);
    EXPECT_EQ(bloomCmd.Events[3].Kind, EventKind::BindPipeline);
    EXPECT_EQ(bloomCmd.Events[4].Kind, EventKind::PushConstants);
    EXPECT_EQ(bloomCmd.Events[5].Kind, EventKind::Draw);
    EXPECT_EQ(bloomCmd.LastDrawVertexCount, 3u);
    EXPECT_EQ(bloomCmd.LastPushConstantSize,
              sizeof(Graphics::PostProcessBloomUpsamplePushConstants));
    // The last push payload is the upsample stage's block; it must carry
    // a strictly positive `InvCoarserResolution` so the shader's 9-tap
    // tent filter spans the coarser mip rather than re-reading the
    // origin texel nine times.
    ASSERT_EQ(bloomCmd.LastPushConstants.size(),
              sizeof(Graphics::PostProcessBloomUpsamplePushConstants));
    Graphics::PostProcessBloomUpsamplePushConstants observedUp{};
    std::memcpy(&observedUp, bloomCmd.LastPushConstants.data(),
                sizeof(Graphics::PostProcessBloomUpsamplePushConstants));
    EXPECT_GT(observedUp.InvCoarserResolution[0], 0.0f)
        << "Bloom upsample push must feed non-zero `InvCoarserResolution.x` "
           "so the tent filter spans the coarser mip; a zero offset would "
           "collapse every tap onto the same texel.";
    EXPECT_GT(observedUp.InvCoarserResolution[1], 0.0f)
        << "Bloom upsample push must feed non-zero `InvCoarserResolution.y`.";
    EXPECT_FLOAT_EQ(observedUp.FilterRadius, 1.0f);

    Graphics::PostProcessHistogramPass histogram{post};
    histogram.SetPipeline(RHI::PipelineHandle{22u, 1u});
    RecordingCommandContext histogramCmd;
    histogram.Execute(histogramCmd, camera);
    ASSERT_EQ(histogramCmd.Events.size(), 3u);
    EXPECT_EQ(histogramCmd.Events[2].Kind, EventKind::Dispatch);
    EXPECT_EQ(histogramCmd.LastDispatchX, 1u);

    Graphics::PostProcessSMAAPass smaa{post};
    smaa.SetPipeline(RHI::PipelineHandle{23u, 1u});
    RecordingCommandContext smaaCmd;
    smaa.Execute(smaaCmd, camera);
    EXPECT_TRUE(smaaCmd.Events.empty());

    Graphics::PostProcessFXAAPass fxaa{post};
    fxaa.SetPipeline(RHI::PipelineHandle{24u, 1u});
    RecordingCommandContext fxaaCmd;
    fxaa.Execute(fxaaCmd, camera);
    ASSERT_EQ(fxaaCmd.Events.size(), 3u);
    EXPECT_EQ(fxaaCmd.Events[2].Kind, EventKind::Draw);
}

// GRAPHICS-075 Slice B.1 — the downsample push block must carry the
// shader's real per-tap kernel offsets. A zero `InvSrcResolution` would
// make every one of the 13 taps in `post_bloom_downsample.frag` read the
// origin texel, silently collapsing the box filter into a no-op. This
// test exercises the downsample stage in isolation (only the downsample
// pipeline is set) so the recorded push payload is unambiguously the
// downsample block.
TEST(GraphicsPostProcessChainContract, BloomDownsamplePushFeedsNonZeroInvSrcResolution)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .EnableBloom = true,
    });

    RHI::CameraUBO camera{};
    camera.ViewportWidth = 1920.0f;
    camera.ViewportHeight = 1080.0f;

    Graphics::PostProcessBloomPass bloom{post};
    bloom.SetDownsamplePipeline(RHI::PipelineHandle{40u, 1u});
    // Intentionally leave the upsample pipeline unset.
    RecordingCommandContext cmd;
    bloom.Execute(cmd, camera);

    ASSERT_EQ(cmd.Events.size(), 3u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[2].Kind, EventKind::Draw);
    EXPECT_EQ(cmd.LastPushConstantSize,
              sizeof(Graphics::PostProcessBloomDownsamplePushConstants));
    ASSERT_EQ(cmd.LastPushConstants.size(),
              sizeof(Graphics::PostProcessBloomDownsamplePushConstants));

    Graphics::PostProcessBloomDownsamplePushConstants observed{};
    std::memcpy(&observed, cmd.LastPushConstants.data(),
                sizeof(Graphics::PostProcessBloomDownsamplePushConstants));
    EXPECT_GT(observed.InvSrcResolution[0], 0.0f)
        << "Bloom downsample push must feed non-zero `InvSrcResolution.x` so the "
           "13-tap kernel spans real source-mip texels; a zero offset would "
           "collapse every tap onto the same texel.";
    EXPECT_GT(observed.InvSrcResolution[1], 0.0f)
        << "Bloom downsample push must feed non-zero `InvSrcResolution.y`.";
    EXPECT_FLOAT_EQ(observed.InvSrcResolution[0], 1.0f / 1920.0f);
    EXPECT_FLOAT_EQ(observed.InvSrcResolution[1], 1.0f / 1080.0f);
    EXPECT_EQ(observed.IsFirstMip, 1)
        << "The first downsample step must enable the soft-threshold knee.";
}

// GRAPHICS-075 Slice B.1 — when only one of the two bloom pipelines is
// available (e.g. one fragment shader failed to compile), the helper must
// still record the surviving stage rather than collapse the whole bloom
// leg. Asserting in both directions catches the regression where the
// renderer-side guard required both leases to be valid.
TEST(GraphicsPostProcessChainContract, BloomRecordsSurvivingStageWhenOnePipelineMissing)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .EnableBloom = true,
    });

    RHI::CameraUBO camera{};
    camera.ViewportWidth = 640.0f;
    camera.ViewportHeight = 360.0f;

    {
        Graphics::PostProcessBloomPass downsampleOnly{post};
        downsampleOnly.SetDownsamplePipeline(RHI::PipelineHandle{50u, 1u});
        RecordingCommandContext cmd;
        downsampleOnly.Execute(cmd, camera);
        ASSERT_EQ(cmd.Events.size(), 3u);
        EXPECT_EQ(cmd.LastPushConstantSize,
                  sizeof(Graphics::PostProcessBloomDownsamplePushConstants));
    }

    {
        Graphics::PostProcessBloomPass upsampleOnly{post};
        upsampleOnly.SetUpsamplePipeline(RHI::PipelineHandle{51u, 1u});
        RecordingCommandContext cmd;
        upsampleOnly.Execute(cmd, camera);
        ASSERT_EQ(cmd.Events.size(), 3u);
        EXPECT_EQ(cmd.LastPushConstantSize,
                  sizeof(Graphics::PostProcessBloomUpsamplePushConstants));
    }
}

TEST(GraphicsPostProcessChainContract, PassesSkipMissingPipelineOrDisabledChain)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{.Enabled = false});

    RHI::CameraUBO camera{};
    Graphics::PostProcessToneMapPass disabled{post};
    disabled.SetPipeline(RHI::PipelineHandle{30u, 1u});
    RecordingCommandContext disabledCmd;
    disabled.Execute(disabledCmd, camera);
    EXPECT_TRUE(disabledCmd.Events.empty());

    post.SetSettings(Graphics::PostProcessSettings{.Enabled = true});
    Graphics::PostProcessToneMapPass missingPipeline{post};
    RecordingCommandContext missingPipelineCmd;
    missingPipeline.Execute(missingPipelineCmd, camera);
    EXPECT_TRUE(missingPipelineCmd.Events.empty());
}




