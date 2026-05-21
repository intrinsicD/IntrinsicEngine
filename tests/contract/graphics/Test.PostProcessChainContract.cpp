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
        // GRAPHICS-075 Slice B.2 — the bloom mip-chain barrier-sequence
        // contract test needs to observe the inline
        // `ColorAttachment ↔ ShaderRead` transitions interleaved with
        // the per-mip bind/push/draw triples; surface the barrier as a
        // first-class event so the assertion can walk a single
        // chronological event stream.
        TextureBarrier,
    };

    struct Event
    {
        EventKind Kind{};
    };

    struct TextureBarrierRecord
    {
        RHI::TextureHandle Texture{};
        RHI::TextureLayout Before{RHI::TextureLayout::Undefined};
        RHI::TextureLayout After{RHI::TextureLayout::Undefined};
    };

    class RecordingCommandContext final : public RHI::ICommandContext
    {
    public:
        std::vector<Event> Events{};
        std::vector<TextureBarrierRecord> TextureBarrierCalls{};
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
        void TextureBarrier(RHI::TextureHandle texture,
                            RHI::TextureLayout before,
                            RHI::TextureLayout after) override
        {
            Events.push_back({.Kind = EventKind::TextureBarrier});
            TextureBarrierCalls.push_back({.Texture = texture, .Before = before, .After = after});
        }
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

    // GRAPHICS-075 Slice B.2 — bloom pass now records the canonical
    // `kBloomMipChainLevels`-mip pyramid: `N-1` downsamples (mip 0 → 1,
    // 1 → 2, …, N-2 → N-1) followed by `N-1` upsamples (mip N-1 → N-2,
    // …, 1 → 0). Each step records a `BindPipeline / PushConstants /
    // Draw` triple. No `m_BloomScratch` handle is set here so the
    // barrier-emission code path is skipped (its own contract test
    // below — `BloomMipChainBarrierSequence` — exercises the barrier
    // sequence with a synthetic handle). The push payloads pack
    // `vec2 InvSrcResolution + float Threshold + int IsFirstMip` for
    // downsample (16B) and `vec2 InvCoarserResolution + float
    // FilterRadius + float _pad0` for upsample (16B), matching the
    // shader std430 layouts. The camera UBO carries non-zero viewport
    // dimensions so the per-mip builders feed the shaders real per-tap
    // kernel offsets (`InvSrcResolution = 1 / vec2(W, H)` derived from
    // the per-mip extent `max(1, base >> mip)`) — feeding zero
    // dimensions would collapse every sample tap onto the same texel
    // and silently break the spatial filter shape.
    RHI::CameraUBO bloomCamera{};
    bloomCamera.ViewportWidth = 1280.0f;
    bloomCamera.ViewportHeight = 720.0f;

    Graphics::PostProcessBloomPass bloom{post};
    bloom.SetDownsamplePipeline(RHI::PipelineHandle{21u, 1u});
    bloom.SetUpsamplePipeline(RHI::PipelineHandle{25u, 1u});
    RecordingCommandContext bloomCmd;
    bloom.Execute(bloomCmd, bloomCamera);
    constexpr std::size_t kBloomChainSteps = Graphics::kBloomMipChainLevels - 1u;
    constexpr std::size_t kEventsPerStep = 3u;
    constexpr std::size_t kTotalBloomEvents = 2u * kBloomChainSteps * kEventsPerStep;
    ASSERT_EQ(bloomCmd.Events.size(), kTotalBloomEvents);
    for (std::size_t step = 0; step < 2u * kBloomChainSteps; ++step)
    {
        EXPECT_EQ(bloomCmd.Events[step * kEventsPerStep + 0u].Kind, EventKind::BindPipeline);
        EXPECT_EQ(bloomCmd.Events[step * kEventsPerStep + 1u].Kind, EventKind::PushConstants);
        EXPECT_EQ(bloomCmd.Events[step * kEventsPerStep + 2u].Kind, EventKind::Draw);
    }
    EXPECT_EQ(bloomCmd.LastDrawVertexCount, 3u);
    // The last push payload is the final upsample step's block (mip 1 →
    // mip 0); it must carry a strictly positive `InvCoarserResolution`
    // so the shader's 9-tap tent filter spans the coarser mip rather
    // than re-reading the origin texel nine times. The coarser mip for
    // the final upsample is mip 1 (extent `max(1, base >> 1)`), so
    // `InvCoarserResolution = 1 / vec2(640, 360)` for a 1280×720
    // viewport.
    EXPECT_EQ(bloomCmd.LastPushConstantSize,
              sizeof(Graphics::PostProcessBloomUpsamplePushConstants));
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
    EXPECT_FLOAT_EQ(observedUp.InvCoarserResolution[0], 1.0f / 640.0f);
    EXPECT_FLOAT_EQ(observedUp.InvCoarserResolution[1], 1.0f / 360.0f);

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
// downsample block. Slice B.2 expands the recording to per-mip
// iteration: the first push payload is the mip 0 → mip 1 step (the
// `SceneColorHDR`-sourced read that must carry `IsFirstMip = 1` and
// the full-viewport `InvSrcResolution`).
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

    constexpr std::size_t kBloomChainSteps = Graphics::kBloomMipChainLevels - 1u;
    ASSERT_EQ(cmd.Events.size(), kBloomChainSteps * 3u);
    for (std::size_t step = 0; step < kBloomChainSteps; ++step)
    {
        EXPECT_EQ(cmd.Events[step * 3u + 0u].Kind, EventKind::BindPipeline);
        EXPECT_EQ(cmd.Events[step * 3u + 1u].Kind, EventKind::PushConstants);
        EXPECT_EQ(cmd.Events[step * 3u + 2u].Kind, EventKind::Draw);
    }
    // The first push payload is the mip 0 → mip 1 step: it reads
    // `SceneColorHDR` at full-viewport resolution and must carry
    // `IsFirstMip = 1` so the soft-threshold knee fires only on the
    // highest-resolution read. `RecordingCommandContext` retains only
    // the *last* payload, so we re-run the build with the published
    // builder to verify the contract directly (the last-payload
    // assertion below covers the final step's payload separately).
    const Graphics::PostProcessBloomDownsamplePushConstants firstStep =
        Graphics::BuildPostProcessBloomDownsamplePushConstants(
            post.GetSettings(),
            static_cast<std::uint32_t>(camera.ViewportWidth),
            static_cast<std::uint32_t>(camera.ViewportHeight),
            /*isFirstMip=*/true);
    EXPECT_FLOAT_EQ(firstStep.InvSrcResolution[0], 1.0f / 1920.0f);
    EXPECT_FLOAT_EQ(firstStep.InvSrcResolution[1], 1.0f / 1080.0f);
    EXPECT_EQ(firstStep.IsFirstMip, 1)
        << "The first downsample step must enable the soft-threshold knee.";

    // The last-recorded push payload is the final downsample step
    // (mip N-2 → mip N-1, reading mip N-2 at `max(1, base >> (N-2))`);
    // it must still carry strictly-positive per-tap offsets and
    // `IsFirstMip = 0` so the soft-threshold knee does not fire on the
    // coarser pyramid mips.
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
    EXPECT_EQ(observed.IsFirstMip, 0)
        << "Only the mip 0 → mip 1 downsample step enables the soft-threshold knee.";
}

// GRAPHICS-075 Slice B.1 — when only one of the two bloom pipelines is
// available (e.g. one fragment shader failed to compile), the helper must
// still record the surviving stage rather than collapse the whole bloom
// leg. Asserting in both directions catches the regression where the
// renderer-side guard required both leases to be valid. Slice B.2 — the
// surviving stage records the full per-mip chain (`N-1` iterations of
// the bind/push/draw triple) rather than a single placeholder step.
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

    constexpr std::size_t kBloomChainSteps = Graphics::kBloomMipChainLevels - 1u;
    constexpr std::size_t kEventsPerStage = kBloomChainSteps * 3u;

    {
        Graphics::PostProcessBloomPass downsampleOnly{post};
        downsampleOnly.SetDownsamplePipeline(RHI::PipelineHandle{50u, 1u});
        RecordingCommandContext cmd;
        downsampleOnly.Execute(cmd, camera);
        ASSERT_EQ(cmd.Events.size(), kEventsPerStage);
        EXPECT_EQ(cmd.LastPushConstantSize,
                  sizeof(Graphics::PostProcessBloomDownsamplePushConstants));
    }

    {
        Graphics::PostProcessBloomPass upsampleOnly{post};
        upsampleOnly.SetUpsamplePipeline(RHI::PipelineHandle{51u, 1u});
        RecordingCommandContext cmd;
        upsampleOnly.Execute(cmd, camera);
        ASSERT_EQ(cmd.Events.size(), kEventsPerStage);
        EXPECT_EQ(cmd.LastPushConstantSize,
                  sizeof(Graphics::PostProcessBloomUpsamplePushConstants));
    }
}

// GRAPHICS-075 Slice B.2 — when the renderer publishes a valid
// `PostProcess.BloomScratch` transient handle to the bloom pass, the
// per-mip iteration must emit `ColorAttachment ↔ ShaderRead` barriers
// between each step so the downstream tonemap pass sees the bloom
// pyramid in `ShaderReadOnly` and intermediate mips transition layouts
// in lock-step with the down/up chain. The recorded shape (in event
// order) is:
//   - `N-1` downsamples: per step `Bind / Push / Draw / Barrier C→S`.
//   - `N-1` upsamples:   per step `Barrier S→C / Bind / Push / Draw /
//                                  Barrier C→S`.
// The trailing `Barrier C→S` on the final upsample (mip 1 → mip 0)
// leaves mip 0 in `ShaderReadOnly` so the tonemap pass that reads the
// bloom pyramid root samples the right layout without an extra
// framegraph-emitted barrier.
TEST(GraphicsPostProcessChainContract, BloomMipChainBarrierSequence)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .EnableBloom = true,
    });

    RHI::CameraUBO camera{};
    camera.ViewportWidth = 1280.0f;
    camera.ViewportHeight = 720.0f;

    const RHI::TextureHandle bloomScratch{0xB10D'F00Du, 1u};

    Graphics::PostProcessBloomPass bloom{post};
    bloom.SetDownsamplePipeline(RHI::PipelineHandle{60u, 1u});
    bloom.SetUpsamplePipeline(RHI::PipelineHandle{61u, 1u});
    bloom.SetBloomScratch(bloomScratch);

    RecordingCommandContext cmd;
    bloom.Execute(cmd, camera);

    constexpr std::size_t kBloomChainSteps = Graphics::kBloomMipChainLevels - 1u;
    // Per-step event counts: downsample = 4 (Bind, Push, Draw, Barrier
    // C→S), upsample = 5 (Barrier S→C, Bind, Push, Draw, Barrier C→S).
    constexpr std::size_t kDownsampleStepEvents = 4u;
    constexpr std::size_t kUpsampleStepEvents = 5u;
    constexpr std::size_t kExpectedEvents =
        kBloomChainSteps * kDownsampleStepEvents + kBloomChainSteps * kUpsampleStepEvents;
    constexpr std::size_t kExpectedBarriers =
        kBloomChainSteps + 2u * kBloomChainSteps; // 1 trailing per down + (1 leading + 1 trailing) per up
    ASSERT_EQ(cmd.Events.size(), kExpectedEvents);
    ASSERT_EQ(cmd.TextureBarrierCalls.size(), kExpectedBarriers);

    // Walk the downsample steps: each should appear as
    // `Bind / Push / Draw / Barrier C→S`.
    std::size_t eventIndex = 0;
    std::size_t barrierIndex = 0;
    for (std::size_t step = 0; step < kBloomChainSteps; ++step)
    {
        EXPECT_EQ(cmd.Events[eventIndex++].Kind, EventKind::BindPipeline) << "down step " << step;
        EXPECT_EQ(cmd.Events[eventIndex++].Kind, EventKind::PushConstants) << "down step " << step;
        EXPECT_EQ(cmd.Events[eventIndex++].Kind, EventKind::Draw) << "down step " << step;
        ASSERT_EQ(cmd.Events[eventIndex++].Kind, EventKind::TextureBarrier) << "down step " << step;
        const TextureBarrierRecord& barrier = cmd.TextureBarrierCalls[barrierIndex++];
        EXPECT_EQ(barrier.Texture.Index, bloomScratch.Index) << "down step " << step;
        EXPECT_EQ(barrier.Before, RHI::TextureLayout::ColorAttachment) << "down step " << step;
        EXPECT_EQ(barrier.After, RHI::TextureLayout::ShaderReadOnly) << "down step " << step;
    }

    // Walk the upsample steps: each should appear as
    // `Barrier S→C / Bind / Push / Draw / Barrier C→S`.
    for (std::size_t step = 0; step < kBloomChainSteps; ++step)
    {
        ASSERT_EQ(cmd.Events[eventIndex++].Kind, EventKind::TextureBarrier) << "up step " << step << " leading";
        const TextureBarrierRecord& leading = cmd.TextureBarrierCalls[barrierIndex++];
        EXPECT_EQ(leading.Texture.Index, bloomScratch.Index) << "up step " << step << " leading";
        EXPECT_EQ(leading.Before, RHI::TextureLayout::ShaderReadOnly) << "up step " << step << " leading";
        EXPECT_EQ(leading.After, RHI::TextureLayout::ColorAttachment) << "up step " << step << " leading";

        EXPECT_EQ(cmd.Events[eventIndex++].Kind, EventKind::BindPipeline) << "up step " << step;
        EXPECT_EQ(cmd.Events[eventIndex++].Kind, EventKind::PushConstants) << "up step " << step;
        EXPECT_EQ(cmd.Events[eventIndex++].Kind, EventKind::Draw) << "up step " << step;

        ASSERT_EQ(cmd.Events[eventIndex++].Kind, EventKind::TextureBarrier) << "up step " << step << " trailing";
        const TextureBarrierRecord& trailing = cmd.TextureBarrierCalls[barrierIndex++];
        EXPECT_EQ(trailing.Texture.Index, bloomScratch.Index) << "up step " << step << " trailing";
        EXPECT_EQ(trailing.Before, RHI::TextureLayout::ColorAttachment) << "up step " << step << " trailing";
        EXPECT_EQ(trailing.After, RHI::TextureLayout::ShaderReadOnly) << "up step " << step << " trailing";
    }

    EXPECT_EQ(eventIndex, cmd.Events.size());
    EXPECT_EQ(barrierIndex, cmd.TextureBarrierCalls.size());
}

// GRAPHICS-075 Slice B.2 — the canonical bloom mip-chain depth is six
// per `docs/architecture/rendering-three-pass.md` ("capped at six
// mips, truncating at extents below 8x8"). Both the recipe-side
// `PostProcess.BloomScratch` allocation and `PostProcessBloomPass::
// Execute`'s per-mip iteration consume `kBloomMipChainLevels`, so
// changing one site without the other would silently desync the
// pyramid storage from the iteration count. This static_assert pins
// the canonical value at compile time; a deliberate cap change must
// update the architecture doc + the slice plan + this assertion in
// lock-step.
static_assert(Graphics::kBloomMipChainLevels == 6u,
              "Bloom mip-chain depth is six mips per "
              "docs/architecture/rendering-three-pass.md; updating the cap "
              "requires synchronising the architecture doc, the GRAPHICS-075 "
              "slice plan, and this assertion.");

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




