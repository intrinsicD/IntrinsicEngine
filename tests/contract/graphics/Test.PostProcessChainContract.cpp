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
        // GRAPHICS-075 Slice E.1 — the histogram clear-before-dispatch
        // contract test needs to observe the `FillBuffer` zero-clear
        // and the matching `BufferBarrier(TransferWrite → ShaderWrite)`
        // emitted before the bind/push/dispatch triple.
        FillBuffer,
        BufferBarrier,
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

    struct FillBufferRecord
    {
        RHI::BufferHandle Buffer{};
        std::uint64_t Offset{0u};
        std::uint64_t Size{0u};
        std::uint32_t Value{0u};
    };

    struct BufferBarrierRecord
    {
        RHI::BufferHandle Buffer{};
        RHI::MemoryAccess Before{RHI::MemoryAccess::None};
        RHI::MemoryAccess After{RHI::MemoryAccess::None};
    };

    class RecordingCommandContext final : public RHI::ICommandContext
    {
    public:
        std::vector<Event> Events{};
        std::vector<TextureBarrierRecord> TextureBarrierCalls{};
        std::vector<FillBufferRecord> FillBufferCalls{};
        std::vector<BufferBarrierRecord> BufferBarrierCalls{};
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
        void BufferBarrier(RHI::BufferHandle buffer,
                           RHI::MemoryAccess before,
                           RHI::MemoryAccess after) override
        {
            Events.push_back({.Kind = EventKind::BufferBarrier});
            BufferBarrierCalls.push_back({.Buffer = buffer, .Before = before, .After = after});
        }
        void SubmitBarriers(const RHI::BarrierBatchDesc& batch) override
        {
            for (const RHI::TextureBarrierDesc& barrier : batch.TextureBarriers)
            {
                TextureBarrier(barrier.Texture, barrier.BeforeLayout, barrier.AfterLayout);
            }
            for (const RHI::BufferBarrierDesc& barrier : batch.BufferBarriers)
            {
                BufferBarrier(barrier.Buffer, barrier.BeforeAccess, barrier.AfterAccess);
            }
        }
        void FillBuffer(RHI::BufferHandle buffer,
                        std::uint64_t offset,
                        std::uint64_t size,
                        std::uint32_t value) override
        {
            Events.push_back({.Kind = EventKind::FillBuffer});
            FillBufferCalls.push_back({.Buffer = buffer, .Offset = offset, .Size = size, .Value = value});
        }
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
    // GRAPHICS-075 Slice E.1 — the histogram compute dispatch moved
    // out of the `"PostProcessPass"` umbrella into its own ordered
    // graph pass `"PostProcessHistogramPass"`. Vulkan rejects
    // `vkCmdDispatch` inside an active render-pass scope (and
    // `"PostProcessPass"` is a render-pass-scope pass — bloom +
    // tonemap write color attachments), so the histogram cannot
    // share the umbrella's render-pass scope. The dedicated test
    // `FrameRecipeDeclaresPostProcessHistogramAsOrderedPass` pins
    // the new pass's read/write declarations.
    EXPECT_FALSE(Contains(post->Writes, "PostProcess.Histogram"))
        << "PostProcess.Histogram moved to its own ordered graph pass "
           "`PostProcessHistogramPass` because Vulkan forbids "
           "`vkCmdDispatch` inside an active render-pass scope.";
    // GRAPHICS-075 Slice D.2a — the AA umbrella split into three ordered
    // graph passes each owning one matched-format AA attachment; none of
    // them belong on `PostProcessPass` (which would re-introduce the
    // aliased-attachment hazard Slice C fixed).
    EXPECT_FALSE(Contains(post->Writes, "PostProcess.AATemp.Edges"))
        << "PostProcess.AATemp.Edges belongs to PostProcessAAEdgePass.";
    EXPECT_FALSE(Contains(post->Writes, "PostProcess.AATemp.Weights"))
        << "PostProcess.AATemp.Weights belongs to PostProcessAABlendPass.";
    EXPECT_FALSE(Contains(post->Writes, "PostProcess.AATemp.Resolved"))
        << "PostProcess.AATemp.Resolved belongs to PostProcessAAResolvePass.";

    EXPECT_TRUE(HasResource(description, Graphics::FrameRecipeResourceKind::SceneColorLDR, true));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.BloomScratch", true));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.Histogram", true));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.AATemp.Edges", true));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.AATemp.Weights", true));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.AATemp.Resolved", true));
}

// GRAPHICS-075 Slice E.1 — the histogram compute dispatch lives in its
// own ordered graph pass `"PostProcessHistogramPass"` before
// `"PostProcessPass"`. Vulkan rejects `vkCmdDispatch` inside an active
// render-pass scope and `"PostProcessPass"` is a render-pass-scope
// pass (bloom + tonemap write color attachments), so the histogram
// cannot share the umbrella's render-pass scope. The dedicated pass
// declares `Read(SceneColorHDR, ShaderRead) + Write(PostProcess.Histogram,
// BufferUsage::ShaderWrite)` so the framegraph compiler emits the
// read-after-write barrier the shader needs and the dispatch executes
// outside any render-pass scope.
TEST(GraphicsPostProcessChainContract, FrameRecipeDeclaresPostProcessHistogramAsOrderedPass)
{
    const Graphics::FrameRecipeIntrospection description = Graphics::DescribeDefaultFrameRecipe(Graphics::FrameRecipeFeatures{});

    const auto* histogram = FindPass(description, Graphics::FrameRecipePassKind::PostProcessHistogram);
    ASSERT_NE(histogram, nullptr);
    EXPECT_TRUE(histogram->Enabled);
    EXPECT_TRUE(Contains(histogram->Reads, "SceneColorHDR"));
    // GRAPHICS-075 Slice E.2 — the histogram pass writes two buffers (both
    // storage / transfer destinations; never color attachments): the
    // transient `PostProcess.Histogram` storage buffer the compute
    // dispatch atomically accumulates into, and the renderer-owned
    // host-visible `Histogram.Readback` buffer the executor copies into
    // after the dispatch. Both are *buffer* writes — the framegraph
    // compiler must still not infer any color-attachment write that
    // would force the dispatch into a render-pass scope on Vulkan.
    EXPECT_EQ(histogram->Writes.size(), 2u)
        << "PostProcessHistogramPass must declare exactly the two buffer "
           "writes (PostProcess.Histogram + Histogram.Readback) and no "
           "color attachments.";
    EXPECT_TRUE(Contains(histogram->Writes, "PostProcess.Histogram"));
    EXPECT_TRUE(Contains(histogram->Writes, "Histogram.Readback"));

    // Compiled order: PointPass → PostProcessHistogramPass → PostProcessPass.
    Graphics::RenderGraph graph;
    const Graphics::FrameRecipeBuildResult build = Graphics::BuildDefaultFrameRecipe(
        graph,
        Graphics::FrameRecipeFeatures{},
        MakeImports(),
        Graphics::FrameRecipeSizing{.Width = 320u, .Height = 180u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    const auto& compileResult = graph.GetLastCompileValidationResult();
    ASSERT_TRUE(compiled.has_value())
        << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    const std::vector<std::string> order = OrderedPassNames(*compiled);
    const auto pointIt = std::find(order.begin(), order.end(), "PointPass");
    const auto histogramIt = std::find(order.begin(), order.end(), "PostProcessHistogramPass");
    const auto postIt = std::find(order.begin(), order.end(), "PostProcessPass");
    ASSERT_NE(pointIt, order.end());
    ASSERT_NE(histogramIt, order.end());
    ASSERT_NE(postIt, order.end());
    EXPECT_LT(pointIt, histogramIt);
    EXPECT_LT(histogramIt, postIt);

    const std::size_t histogramOrderIndex =
        static_cast<std::size_t>(std::distance(order.begin(), histogramIt));
    ASSERT_LT(histogramOrderIndex, compiled->TopologicalOrder.size());
    const std::uint32_t histogramPassIndex = compiled->TopologicalOrder[histogramOrderIndex];
    ASSERT_LT(histogramPassIndex, compiled->PassQueues.size());
    EXPECT_EQ(compiled->PassQueues[histogramPassIndex], Graphics::RenderQueue::AsyncCompute)
        << "PostProcessHistogramPass is the default recipe's async-compute queue smoke pass.";
}

// GRAPHICS-075 Slice D.2a — the AA umbrella splits into three ordered
// graph passes (`PostProcessAAEdgePass`, `PostProcessAABlendPass`,
// `PostProcessAAResolvePass`) so edge / blend / resolve pipelines can
// target format-incompatible color attachments (`RG8_UNORM` /
// `RGBA8_UNORM` / backbuffer). Each pass declares a single matched-
// format `Write`; the framegraph compiler emits correct layout
// transitions between them. FXAA records under the resolve pass only;
// SMAA records under all three. With `features.EnableAntiAliasing`,
// `presentSource` flips to `PostProcess.AATemp.Resolved` so the AA
// output reaches present.
TEST(GraphicsPostProcessChainContract, FrameRecipeSplitsAAUmbrellaPerStage)
{
    const Graphics::FrameRecipeIntrospection description = Graphics::DescribeDefaultFrameRecipe(Graphics::FrameRecipeFeatures{});

    const auto* edge = FindPass(description, Graphics::FrameRecipePassKind::PostProcessAAEdge);
    const auto* blend = FindPass(description, Graphics::FrameRecipePassKind::PostProcessAABlend);
    const auto* resolve = FindPass(description, Graphics::FrameRecipePassKind::PostProcessAAResolve);
    ASSERT_NE(edge, nullptr);
    ASSERT_NE(blend, nullptr);
    ASSERT_NE(resolve, nullptr);
    EXPECT_TRUE(edge->Enabled);
    EXPECT_TRUE(blend->Enabled);
    EXPECT_TRUE(resolve->Enabled);

    EXPECT_TRUE(Contains(edge->Reads, "SceneColorLDR"));
    EXPECT_EQ(edge->Writes.size(), 1u)
        << "Edge pass must declare a single matched-format Write so the "
           "AA umbrella never aliases two pipelines with incompatible "
           "color formats inside one render-pass scope on Vulkan.";
    EXPECT_TRUE(Contains(edge->Writes, "PostProcess.AATemp.Edges"));

    EXPECT_TRUE(Contains(blend->Reads, "PostProcess.AATemp.Edges"));
    EXPECT_EQ(blend->Writes.size(), 1u);
    EXPECT_TRUE(Contains(blend->Writes, "PostProcess.AATemp.Weights"));

    EXPECT_TRUE(Contains(resolve->Reads, "SceneColorLDR"));
    EXPECT_TRUE(Contains(resolve->Reads, "PostProcess.AATemp.Weights"));
    EXPECT_EQ(resolve->Writes.size(), 1u);
    EXPECT_TRUE(Contains(resolve->Writes, "PostProcess.AATemp.Resolved"));

    // Compiled order: PostProcessPass → Edge → Blend → Resolve.
    Graphics::RenderGraph graph;
    const Graphics::FrameRecipeBuildResult build = Graphics::BuildDefaultFrameRecipe(
        graph,
        Graphics::FrameRecipeFeatures{},
        MakeImports(),
        Graphics::FrameRecipeSizing{.Width = 320u, .Height = 180u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    const auto& compileResult = graph.GetLastCompileValidationResult();
    ASSERT_TRUE(compiled.has_value())
        << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    const std::vector<std::string> order = OrderedPassNames(*compiled);
    const auto postIt = std::find(order.begin(), order.end(), "PostProcessPass");
    const auto edgeIt = std::find(order.begin(), order.end(), "PostProcessAAEdgePass");
    const auto blendIt = std::find(order.begin(), order.end(), "PostProcessAABlendPass");
    const auto resolveIt = std::find(order.begin(), order.end(), "PostProcessAAResolvePass");
    ASSERT_NE(postIt, order.end());
    ASSERT_NE(edgeIt, order.end());
    ASSERT_NE(blendIt, order.end());
    ASSERT_NE(resolveIt, order.end());
    EXPECT_LT(postIt, edgeIt);
    EXPECT_LT(edgeIt, blendIt);
    EXPECT_LT(blendIt, resolveIt);
}

// GRAPHICS-075 Slice D.2a — `presentSource` flips to
// `PostProcess.AATemp.Resolved` when `features.EnableAntiAliasing` is
// set, so the AA-resolved color reaches present rather than the
// un-resolved `SceneColorLDR`. The renderer plumbs this from
// `PostProcessSettings::AntiAliasing != None`; the recipe-level
// `Present` pass's `Read` declaration is the framegraph-visible
// observable.
TEST(GraphicsPostProcessChainContract, AntiAliasingFlipsPresentSourceToResolved)
{
    auto presentReadsResolvedFor = [](const bool enableAntiAliasing) {
        Graphics::FrameRecipeFeatures features{};
        features.EnableAntiAliasing = enableAntiAliasing;
        Graphics::RenderGraph graph;
        const Graphics::FrameRecipeBuildResult build = Graphics::BuildDefaultFrameRecipe(
            graph,
            features,
            MakeImports(),
            Graphics::FrameRecipeSizing{.Width = 320u, .Height = 180u});
        EXPECT_TRUE(build.Succeeded) << build.Diagnostic;

        const auto compiled = graph.Compile();
        EXPECT_TRUE(compiled.has_value());
        if (!compiled.has_value())
        {
            return false;
        }

        // Walk the compiled graph for the Present pass and check whether
        // it reads `PostProcess.AATemp.Resolved` (AA-on) or
        // `SceneColorLDR` (AA-off).
        for (const auto& decl : compiled->PassDeclarations)
        {
            if (decl.PassIndex >= compiled->PassNames.size() ||
                compiled->PassNames[decl.PassIndex] != std::string_view{"Present"})
            {
                continue;
            }
            for (const std::uint32_t resIdx : decl.ReadTextures)
            {
                if (resIdx >= compiled->TextureNames.size())
                {
                    continue;
                }
                if (compiled->TextureNames[resIdx] == std::string_view{"PostProcess.AATemp.Resolved"})
                {
                    return true;
                }
            }
            return false;
        }
        return false;
    };

    EXPECT_FALSE(presentReadsResolvedFor(false))
        << "With AA disabled, present must consume SceneColorLDR so the "
           "(cleared / undefined) AA-resolved attachment does not reach "
           "the backbuffer.";
    EXPECT_TRUE(presentReadsResolvedFor(true))
        << "With AA enabled, present must consume PostProcess.AATemp.Resolved "
           "so the AA-resolved color reaches the backbuffer.";
}

TEST(GraphicsPostProcessChainContract, PostprocessGateControlsPresentSource)
{
    Graphics::FrameRecipeFeatures features{};
    features.EnablePostProcess = false;

    const Graphics::FrameRecipeIntrospection description = Graphics::DescribeDefaultFrameRecipe(features);
    const auto* post = FindPass(description, Graphics::FrameRecipePassKind::PostProcess);
    ASSERT_NE(post, nullptr);
    EXPECT_FALSE(post->Enabled);
    // GRAPHICS-075 Slice D.2a — the three AA passes share the same
    // `EnablePostProcess` gate (FXAA / SMAA are postprocess stages),
    // so disabling postprocess also disables all three AA passes.
    const auto* edge = FindPass(description, Graphics::FrameRecipePassKind::PostProcessAAEdge);
    const auto* blend = FindPass(description, Graphics::FrameRecipePassKind::PostProcessAABlend);
    const auto* resolve = FindPass(description, Graphics::FrameRecipePassKind::PostProcessAAResolve);
    // GRAPHICS-075 Slice E.1 — the histogram graph pass shares the
    // same `EnablePostProcess` gate as the umbrella and AA passes.
    const auto* histogram = FindPass(description, Graphics::FrameRecipePassKind::PostProcessHistogram);
    ASSERT_NE(edge, nullptr);
    ASSERT_NE(blend, nullptr);
    ASSERT_NE(resolve, nullptr);
    ASSERT_NE(histogram, nullptr);
    EXPECT_FALSE(edge->Enabled);
    EXPECT_FALSE(blend->Enabled);
    EXPECT_FALSE(resolve->Enabled);
    EXPECT_FALSE(histogram->Enabled);
    EXPECT_TRUE(HasResource(description, Graphics::FrameRecipeResourceKind::SceneColorLDR, false));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.BloomScratch", false));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.Histogram", false));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.AATemp.Edges", false));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.AATemp.Weights", false));
    EXPECT_TRUE(HasResourceName(description, "PostProcess.AATemp.Resolved", false));

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
    EXPECT_EQ(std::find(order.begin(), order.end(), "PostProcessHistogramPass"), order.end());
    EXPECT_EQ(std::find(order.begin(), order.end(), "PostProcessPass"), order.end());
    EXPECT_EQ(std::find(order.begin(), order.end(), "PostProcessAAEdgePass"), order.end());
    EXPECT_EQ(std::find(order.begin(), order.end(), "PostProcessAABlendPass"), order.end());
    EXPECT_EQ(std::find(order.begin(), order.end(), "PostProcessAAResolvePass"), order.end());
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

    // GRAPHICS-075 Slice E.1 — `PostProcessHistogramPass::SetViewport`
    // publishes the runtime backbuffer extent so the compute dispatch
    // shape tracks the viewport (`ceil(W/16) x ceil(H/16) x 1`,
    // matching the shader's `local_size_x = local_size_y = 16` tile).
    // Without a published viewport the dispatch falls back to
    // `(1, 1, 1)` so headless tests that drive `Execute` directly
    // still observe the event shape. A viewport of `1920x1080` here
    // pins the executor-published path: `ceil(1920 / 16) = 120`,
    // `ceil(1080 / 16) = 68`.
    Graphics::PostProcessHistogramPass histogram{post};
    histogram.SetPipeline(RHI::PipelineHandle{22u, 1u});
    histogram.SetViewport(1920u, 1080u);
    RecordingCommandContext histogramCmd;
    histogram.Execute(histogramCmd, camera);
    ASSERT_EQ(histogramCmd.Events.size(), 3u);
    EXPECT_EQ(histogramCmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(histogramCmd.Events[1].Kind, EventKind::PushConstants);
    EXPECT_EQ(histogramCmd.Events[2].Kind, EventKind::Dispatch);
    EXPECT_EQ(histogramCmd.LastDispatchX, 120u);
    // The pass body pushes the pass-local 16-byte
    // `PostProcessHistogramPushConstants` block (std430-matched to
    // `post_histogram.comp`) so the shader actually sees `Width` /
    // `Height` + the canonical `[-10, +10]` log-luminance bounds.
    // The canonical 20-byte `PostProcessPushConstants` block shared
    // by other postprocess stages is intentionally not used here per
    // the standing shader-push-constant compatibility policy: under
    // std430 it would alias `Exposure` (1.0) onto `Width`
    // (`bit_cast<uint>(1.0f)` ≈ 1.07e9 pixels), producing a
    // degenerate out-of-bounds dispatch.
    EXPECT_EQ(histogramCmd.LastPushConstantSize,
              sizeof(Graphics::PostProcessHistogramPushConstants));
    ASSERT_EQ(histogramCmd.LastPushConstants.size(),
              sizeof(Graphics::PostProcessHistogramPushConstants));
    Graphics::PostProcessHistogramPushConstants observedHistogram{};
    std::memcpy(&observedHistogram, histogramCmd.LastPushConstants.data(),
                sizeof(Graphics::PostProcessHistogramPushConstants));
    EXPECT_EQ(observedHistogram.Width, 1920u);
    EXPECT_EQ(observedHistogram.Height, 1080u);
    EXPECT_FLOAT_EQ(observedHistogram.MinLogLum, -10.0f);
    EXPECT_FLOAT_EQ(observedHistogram.RangeLogLum, 1.0f / 20.0f);

    // GRAPHICS-075 Slice D.2a — the SMAA pass exposes per-stage Execute
    // methods (`ExecuteEdge`/`ExecuteBlend`/`ExecuteResolve`) so the
    // renderer can fan each stage out under its own ordered graph pass.
    // `AntiAliasing == FXAA` short-circuits every per-stage body to a
    // no-op via `IsStageEnabled(SMAA)`, so even with all three
    // pipelines bound the SMAA pass must stay silent here (mirroring
    // `SMAASkipsWhenAntiAliasingNotSMAA` below).
    Graphics::PostProcessSMAAPass smaa{post};
    smaa.SetEdgePipeline(RHI::PipelineHandle{23u, 1u});
    smaa.SetBlendPipeline(RHI::PipelineHandle{27u, 1u});
    smaa.SetResolvePipeline(RHI::PipelineHandle{28u, 1u});
    RecordingCommandContext smaaCmd;
    smaa.ExecuteEdge(smaaCmd, camera);
    smaa.ExecuteBlend(smaaCmd, camera);
    smaa.ExecuteResolve(smaaCmd, camera);
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
// GRAPHICS-075 Slice E.1 — when the executor publishes a valid
// histogram buffer handle, the pass must zero-fill the 256 bins
// before dispatch so the shader's `atomicAdd` accumulations start
// from a clean slate. Without the per-frame clear, transient
// allocator reuse from prior frames would contaminate the next
// frame's luminance distribution and corrupt the downstream
// exposure-adaptation readback Slice E.2 wires. The expected event
// shape is `FillBuffer → BufferBarrier(TransferWrite →
// ShaderWrite) → BindPipeline → PushConstants → Dispatch`,
// mirroring the `CullingSystem::ResetCounters` idiom that already
// handles the same atomic-add-on-stale-data hazard for the
// indirect-draw count buffers.
TEST(GraphicsPostProcessChainContract, HistogramClearsBinsBeforeDispatchWhenBufferPublished)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .EnableHistogram = true,
    });

    RHI::CameraUBO camera{};

    Graphics::PostProcessHistogramPass histogram{post};
    histogram.SetPipeline(RHI::PipelineHandle{200u, 1u});
    histogram.SetViewport(640u, 360u);
    const RHI::BufferHandle histogramBuffer{99u, 1u};
    histogram.SetHistogramBuffer(histogramBuffer);

    RecordingCommandContext cmd;
    histogram.Execute(cmd, camera);

    // FillBuffer → BufferBarrier → BindPipeline → PushConstants → Dispatch
    ASSERT_EQ(cmd.Events.size(), 5u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::FillBuffer);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::BufferBarrier);
    EXPECT_EQ(cmd.Events[2].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[3].Kind, EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[4].Kind, EventKind::Dispatch);

    ASSERT_EQ(cmd.FillBufferCalls.size(), 1u);
    EXPECT_EQ(cmd.FillBufferCalls[0].Buffer, histogramBuffer);
    EXPECT_EQ(cmd.FillBufferCalls[0].Offset, 0u);
    EXPECT_EQ(cmd.FillBufferCalls[0].Size, 256ull * sizeof(std::uint32_t))
        << "Fill must cover the full 256-bin histogram so atomicAdd starts "
           "from zero in every slot.";
    EXPECT_EQ(cmd.FillBufferCalls[0].Value, 0u);

    ASSERT_EQ(cmd.BufferBarrierCalls.size(), 1u);
    EXPECT_EQ(cmd.BufferBarrierCalls[0].Buffer, histogramBuffer);
    EXPECT_EQ(cmd.BufferBarrierCalls[0].Before, RHI::MemoryAccess::TransferWrite)
        << "Before-mask must mirror the fill's TransferWrite so the "
           "compute dispatch sees the zeroed bins.";
    EXPECT_EQ(cmd.BufferBarrierCalls[0].After, RHI::MemoryAccess::ShaderWrite)
        << "After-mask must be ShaderWrite so the dispatch's atomicAdds "
           "are properly ordered against the fill.";
}

// GRAPHICS-075 Slice E.1 — when no histogram buffer handle is
// published (headless contract tests driving Execute directly,
// or any future caller that hasn't wired the executor plumbing),
// the pass must skip the fill+barrier and fall back to the
// original bind/push/dispatch triple. This keeps the per-pass
// event-shape contract simple for tests that already rely on
// the 3-event shape (`PassesRecordOnlyEnabledStages` above).
TEST(GraphicsPostProcessChainContract, HistogramSkipsClearWhenNoBufferPublished)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .EnableHistogram = true,
    });

    RHI::CameraUBO camera{};

    Graphics::PostProcessHistogramPass histogram{post};
    histogram.SetPipeline(RHI::PipelineHandle{200u, 1u});
    histogram.SetViewport(640u, 360u);
    // Intentionally do not publish a histogram buffer handle.

    RecordingCommandContext cmd;
    histogram.Execute(cmd, camera);

    ASSERT_EQ(cmd.Events.size(), 3u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[2].Kind, EventKind::Dispatch);
    EXPECT_TRUE(cmd.FillBufferCalls.empty());
    EXPECT_TRUE(cmd.BufferBarrierCalls.empty());
}

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

// GRAPHICS-075 Slice B.2 — when the renderer publishes the per-frame
// `PostProcess.BloomScratch` transient handle to the bloom pass, the
// recorded shape iterates the canonical six-mip pyramid as:
//   - `M-1` downsamples: per step `Bind / Push / Draw`.
//   - `M-1` upsamples:   per step `Bind / Push / Draw`.
// where `M = ComputeBloomMipChainLevels(viewport.W, viewport.H)`,
// capped at `kBloomMipChainLevels = 6`. The pass body intentionally
// emits *no* `TextureBarrier(...)` calls — the renderer's
// `"PostProcessPass"` executor branch keeps a single umbrella render
// pass active across bloom + tonemap, and Vulkan rejects layout
// transitions issued inside a render-pass scope on the attachment
// currently being rendered. Correct per-mip subresource barriers
// require both an `ICommandContext::TextureBarrier(handle, mipRange,
// ...)` extension to the RHI and a per-mip render-pass restart between
// iterations; both are deferred to a follow-up slice. Until then the
// inter-pass barrier between the bloom and tonemap legs is emitted by
// the framegraph compiler from the recipe-level `Write(BloomScratch,
// ColorAttachmentWrite)` / `Read(BloomScratch, ShaderRead)`
// declarations, which is layout-safe for the whole-texture case.
TEST(GraphicsPostProcessChainContract, BloomMipChainPerMipIterationShape)
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
    bloom.SetBloomScratch(bloomScratch, Graphics::kBloomMipChainLevels);

    RecordingCommandContext cmd;
    bloom.Execute(cmd, camera);

    constexpr std::size_t kBloomChainSteps = Graphics::kBloomMipChainLevels - 1u;
    constexpr std::size_t kEventsPerStep = 3u;
    constexpr std::size_t kExpectedEvents = 2u * kBloomChainSteps * kEventsPerStep;
    ASSERT_EQ(cmd.Events.size(), kExpectedEvents);
    EXPECT_TRUE(cmd.TextureBarrierCalls.empty())
        << "Pass body must not emit TextureBarriers (the umbrella render "
           "pass is active when Execute(...) runs; layout transitions "
           "inside a render-pass scope are invalid on Vulkan). The "
           "inter-pass `ColorAttachment → ShaderReadOnly` transition is "
           "owned by the framegraph compiler.";

    for (std::size_t step = 0; step < 2u * kBloomChainSteps; ++step)
    {
        EXPECT_EQ(cmd.Events[step * kEventsPerStep + 0u].Kind, EventKind::BindPipeline) << "step " << step;
        EXPECT_EQ(cmd.Events[step * kEventsPerStep + 1u].Kind, EventKind::PushConstants) << "step " << step;
        EXPECT_EQ(cmd.Events[step * kEventsPerStep + 2u].Kind, EventKind::Draw) << "step " << step;
    }
}

// GRAPHICS-075 Slice B.2 — Vulkan's `VkImageCreateInfo::mipLevels`
// rule (`mipLevels <= floor(log2(max(W, H))) + 1`) forbids declaring
// six mips for tiny viewports: a 16x16 viewport only supports five
// legal mips, an 8x8 four, and a 1x1 a single mip. The recipe-side
// `BuildDefaultFrameRecipe` calls `ComputeBloomMipChainLevels(width,
// height)` to clamp `RHI::TextureDesc::MipLevels`, and the renderer
// passes the same clamped value to `SetBloomScratch(...)` so the
// pass's iteration count never exceeds the texture's actual mip
// range. This test exercises the helper across the boundary sizes
// that matter (large enough → cap at 6, small → clamped, 0 →
// degenerate single mip).
TEST(GraphicsPostProcessChainContract, BloomMipChainClampedToVulkanMipRule)
{
    // Large viewports: clamp at the canonical six-mip cap.
    EXPECT_EQ(Graphics::ComputeBloomMipChainLevels(1920u, 1080u), 6u);
    EXPECT_EQ(Graphics::ComputeBloomMipChainLevels(1280u, 720u), 6u);
    EXPECT_EQ(Graphics::ComputeBloomMipChainLevels(64u, 64u), 6u);
    EXPECT_EQ(Graphics::ComputeBloomMipChainLevels(32u, 32u), 6u);

    // Boundary case: 16x16 supports exactly 5 legal mips
    // (floor(log2(16)) + 1 = 5).
    EXPECT_EQ(Graphics::ComputeBloomMipChainLevels(16u, 16u), 5u);
    // Asymmetric tiny extent — the helper uses the *max* dimension.
    EXPECT_EQ(Graphics::ComputeBloomMipChainLevels(16u, 4u), 5u);
    EXPECT_EQ(Graphics::ComputeBloomMipChainLevels(4u, 16u), 5u);

    // Very small extents.
    EXPECT_EQ(Graphics::ComputeBloomMipChainLevels(8u, 8u), 4u);
    EXPECT_EQ(Graphics::ComputeBloomMipChainLevels(4u, 4u), 3u);
    EXPECT_EQ(Graphics::ComputeBloomMipChainLevels(2u, 2u), 2u);
    EXPECT_EQ(Graphics::ComputeBloomMipChainLevels(1u, 1u), 1u);

    // Degenerate / zero extents normalise to a single mip (matching
    // `BuildDefaultFrameRecipe`'s `ClampExtent` convention).
    EXPECT_EQ(Graphics::ComputeBloomMipChainLevels(0u, 0u), 1u);
}

// GRAPHICS-075 Slice B.2 — when the runtime extent is small enough
// that `ComputeBloomMipChainLevels` clamps below the canonical six,
// the pass's iteration count must follow the clamped storage; iterating
// more steps than the texture has mips would silently re-draw past the
// allocated range.
TEST(GraphicsPostProcessChainContract, BloomPassIterationFollowsClampedMipCount)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .EnableBloom = true,
    });

    RHI::CameraUBO camera{};
    camera.ViewportWidth = 16.0f;
    camera.ViewportHeight = 16.0f;

    const std::uint32_t mipLevels = Graphics::ComputeBloomMipChainLevels(16u, 16u);
    ASSERT_EQ(mipLevels, 5u);

    Graphics::PostProcessBloomPass bloom{post};
    bloom.SetDownsamplePipeline(RHI::PipelineHandle{70u, 1u});
    bloom.SetUpsamplePipeline(RHI::PipelineHandle{71u, 1u});
    bloom.SetBloomScratch(RHI::TextureHandle{0xB10Du, 1u}, mipLevels);

    RecordingCommandContext cmd;
    bloom.Execute(cmd, camera);

    const std::size_t chainSteps = mipLevels - 1u;  // 4 down + 4 up
    EXPECT_EQ(cmd.Events.size(), 2u * chainSteps * 3u);
    EXPECT_TRUE(cmd.TextureBarrierCalls.empty());

    // A single-mip degenerate pyramid leaves no down/up chain to
    // iterate; the pass must record nothing rather than emit a
    // self-overwriting placeholder draw.
    Graphics::PostProcessBloomPass degenerate{post};
    degenerate.SetDownsamplePipeline(RHI::PipelineHandle{72u, 1u});
    degenerate.SetUpsamplePipeline(RHI::PipelineHandle{73u, 1u});
    degenerate.SetBloomScratch(RHI::TextureHandle{0xB10Eu, 1u}, 1u);
    RecordingCommandContext degenerateCmd;
    degenerate.Execute(degenerateCmd, camera);
    EXPECT_TRUE(degenerateCmd.Events.empty());
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

// GRAPHICS-075 Slice C — when `AntiAliasing == FXAA` the FXAA pass body
// must push a `PostProcessFXAAPushConstants` block whose `InvResolution`
// matches `1 / vec2(viewportWidth, viewportHeight)`. A zero
// `InvResolution` would make every neighbour-tap UV in `post_fxaa.frag`
// collapse onto the center pixel, silently degenerating FXAA into a
// pass-through. This mirrors Slice B.1's
// `BloomDownsamplePushFeedsNonZeroInvSrcResolution` for the bloom
// per-tap kernel offsets.
TEST(GraphicsPostProcessChainContract, FXAAPushFeedsNonZeroInvResolution)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .AntiAliasing = Graphics::PostProcessAntiAliasing::FXAA,
    });

    RHI::CameraUBO camera{};
    camera.ViewportWidth = 1920.0f;
    camera.ViewportHeight = 1080.0f;

    Graphics::PostProcessFXAAPass fxaa{post};
    fxaa.SetPipeline(RHI::PipelineHandle{80u, 1u});
    RecordingCommandContext cmd;
    fxaa.Execute(cmd, camera);

    ASSERT_EQ(cmd.Events.size(), 3u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[2].Kind, EventKind::Draw);
    EXPECT_EQ(cmd.LastDrawVertexCount, 3u);
    // The push payload must be the FXAA-shaped 20-byte block, not the
    // canonical 20-byte `PostProcessPushConstants` block (which would
    // alias Exposure/Gamma/etc. onto the FXAA shader's InvResolution/
    // ContrastThreshold/etc. and produce visually-meaningless output
    // even though the wire size happens to match).
    EXPECT_EQ(cmd.LastPushConstantSize,
              sizeof(Graphics::PostProcessFXAAPushConstants));
    ASSERT_EQ(cmd.LastPushConstants.size(),
              sizeof(Graphics::PostProcessFXAAPushConstants));
    Graphics::PostProcessFXAAPushConstants observed{};
    std::memcpy(&observed, cmd.LastPushConstants.data(),
                sizeof(Graphics::PostProcessFXAAPushConstants));
    EXPECT_FLOAT_EQ(observed.InvResolution[0], 1.0f / 1920.0f);
    EXPECT_FLOAT_EQ(observed.InvResolution[1], 1.0f / 1080.0f);
    EXPECT_FLOAT_EQ(observed.ContrastThreshold, 0.0312f);
    EXPECT_FLOAT_EQ(observed.RelativeThreshold, 0.063f);
    EXPECT_FLOAT_EQ(observed.SubpixelBlending, 0.75f);

    // Cross-check the published builder produces the same byte shape so
    // future settings extensions cannot drift the pass body away from
    // the contract.
    const Graphics::PostProcessFXAAPushConstants built =
        Graphics::BuildPostProcessFXAAPushConstants(post.GetSettings(),
                                                    camera.ViewportWidth,
                                                    camera.ViewportHeight);
    EXPECT_FLOAT_EQ(built.InvResolution[0], observed.InvResolution[0]);
    EXPECT_FLOAT_EQ(built.InvResolution[1], observed.InvResolution[1]);
    EXPECT_FLOAT_EQ(built.ContrastThreshold, observed.ContrastThreshold);
}

// GRAPHICS-075 Slice C — `PostProcessSettings::AntiAliasing` is the
// branch selector for the typed AA stages: `FXAA` enables FXAA and
// keeps SMAA off; `SMAA` does the reverse; `None` disables both. The
// FXAA pass body must respect the selector and emit no bind/push/draw
// when the stage is gated off, mirroring the existing SMAA-skipped
// behavior in `PassesRecordOnlyEnabledStages` (which sets
// `AntiAliasing = FXAA` and expects the SMAA pass to record empty).
TEST(GraphicsPostProcessChainContract, FXAASkipsWhenAntiAliasingNotFXAA)
{
    Graphics::PostProcessSystem post;
    post.Initialize();

    RHI::CameraUBO camera{};
    camera.ViewportWidth = 1280.0f;
    camera.ViewportHeight = 720.0f;

    // AntiAliasing == None — FXAA must stay silent.
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .AntiAliasing = Graphics::PostProcessAntiAliasing::None,
    });
    Graphics::PostProcessFXAAPass fxaaNone{post};
    fxaaNone.SetPipeline(RHI::PipelineHandle{81u, 1u});
    RecordingCommandContext noneCmd;
    fxaaNone.Execute(noneCmd, camera);
    EXPECT_TRUE(noneCmd.Events.empty());

    // AntiAliasing == SMAA — FXAA still stays silent (mutually exclusive
    // per `PostProcessSettings::AntiAliasing`).
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .AntiAliasing = Graphics::PostProcessAntiAliasing::SMAA,
    });
    Graphics::PostProcessFXAAPass fxaaSmaa{post};
    fxaaSmaa.SetPipeline(RHI::PipelineHandle{82u, 1u});
    RecordingCommandContext smaaCmd;
    fxaaSmaa.Execute(smaaCmd, camera);
    EXPECT_TRUE(smaaCmd.Events.empty());

    // AntiAliasing == FXAA — FXAA records.
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .AntiAliasing = Graphics::PostProcessAntiAliasing::FXAA,
    });
    Graphics::PostProcessFXAAPass fxaaActive{post};
    fxaaActive.SetPipeline(RHI::PipelineHandle{83u, 1u});
    RecordingCommandContext activeCmd;
    fxaaActive.Execute(activeCmd, camera);
    ASSERT_EQ(activeCmd.Events.size(), 3u);
    EXPECT_EQ(activeCmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(activeCmd.Events[1].Kind, EventKind::PushConstants);
    EXPECT_EQ(activeCmd.Events[2].Kind, EventKind::Draw);
}

// GRAPHICS-075 Slice D.1 — when `AntiAliasing == SMAA` the SMAA pass
// body must record three Bind/Push/Draw triples (edge → blend →
// resolve), each carrying a 16-byte std430 push block whose
// `InvResolution` matches `1 / vec2(viewportWidth, viewportHeight)`.
// A zero `InvResolution` would make every neighbour-tap UV in the
// SMAA shaders collapse onto the center pixel, silently degenerating
// SMAA into a pass-through (and the blend pipeline's `SearchTex` /
// `AreaTex` taps would degenerate completely). The recorded push
// payloads must be the SMAA-shaped 16-byte blocks, not the canonical
// 20-byte `PostProcessPushConstants` block — under std430 the latter
// would alias `Exposure` / `Gamma` / etc. onto each SMAA shader's
// `InvResolution.{x,y}` plus threshold scalars and produce
// visually-meaningless output.
TEST(GraphicsPostProcessChainContract, SMAAPushFeedsNonZeroInvResolutionForAllStages)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .AntiAliasing = Graphics::PostProcessAntiAliasing::SMAA,
    });

    RHI::CameraUBO camera{};
    camera.ViewportWidth = 1920.0f;
    camera.ViewportHeight = 1080.0f;

    Graphics::PostProcessSMAAPass smaa{post};
    smaa.SetEdgePipeline(RHI::PipelineHandle{90u, 1u});
    smaa.SetBlendPipeline(RHI::PipelineHandle{91u, 1u});
    smaa.SetResolvePipeline(RHI::PipelineHandle{92u, 1u});
    RecordingCommandContext cmd;
    smaa.ExecuteEdge(cmd, camera);
    smaa.ExecuteBlend(cmd, camera);
    smaa.ExecuteResolve(cmd, camera);

    // Three Bind/Push/Draw triples: edge, blend, resolve.
    ASSERT_EQ(cmd.Events.size(), 9u);
    for (std::size_t stage = 0; stage < 3u; ++stage)
    {
        EXPECT_EQ(cmd.Events[stage * 3u + 0u].Kind, EventKind::BindPipeline);
        EXPECT_EQ(cmd.Events[stage * 3u + 1u].Kind, EventKind::PushConstants);
        EXPECT_EQ(cmd.Events[stage * 3u + 2u].Kind, EventKind::Draw);
    }
    EXPECT_EQ(cmd.LastDrawVertexCount, 3u);

    // The last recorded push payload is the resolve stage's
    // `PostProcessSMAAResolvePushConstants` block (16 bytes mirroring
    // `vec2 InvResolution + float _pad0 + float _pad1`).
    EXPECT_EQ(cmd.LastPushConstantSize,
              sizeof(Graphics::PostProcessSMAAResolvePushConstants));
    ASSERT_EQ(cmd.LastPushConstants.size(),
              sizeof(Graphics::PostProcessSMAAResolvePushConstants));
    Graphics::PostProcessSMAAResolvePushConstants observedResolve{};
    std::memcpy(&observedResolve, cmd.LastPushConstants.data(),
                sizeof(Graphics::PostProcessSMAAResolvePushConstants));
    EXPECT_FLOAT_EQ(observedResolve.InvResolution[0], 1.0f / 1920.0f);
    EXPECT_FLOAT_EQ(observedResolve.InvResolution[1], 1.0f / 1080.0f);

    // Cross-check each published builder produces a non-zero
    // `InvResolution` plus SMAA reference defaults so future settings
    // extensions cannot drift the pass body away from the contract.
    const Graphics::PostProcessSMAAEdgePushConstants builtEdge =
        Graphics::BuildPostProcessSMAAEdgePushConstants(post.GetSettings(),
                                                       camera.ViewportWidth,
                                                       camera.ViewportHeight);
    EXPECT_FLOAT_EQ(builtEdge.InvResolution[0], 1.0f / 1920.0f);
    EXPECT_FLOAT_EQ(builtEdge.InvResolution[1], 1.0f / 1080.0f);
    EXPECT_FLOAT_EQ(builtEdge.EdgeThreshold, 0.1f);

    const Graphics::PostProcessSMAABlendPushConstants builtBlend =
        Graphics::BuildPostProcessSMAABlendPushConstants(post.GetSettings(),
                                                        camera.ViewportWidth,
                                                        camera.ViewportHeight);
    EXPECT_FLOAT_EQ(builtBlend.InvResolution[0], 1.0f / 1920.0f);
    EXPECT_FLOAT_EQ(builtBlend.InvResolution[1], 1.0f / 1080.0f);
    EXPECT_EQ(builtBlend.MaxSearchSteps, 16);
    EXPECT_EQ(builtBlend.MaxSearchStepsDiag, 8);

    const Graphics::PostProcessSMAAResolvePushConstants builtResolve =
        Graphics::BuildPostProcessSMAAResolvePushConstants(post.GetSettings(),
                                                          camera.ViewportWidth,
                                                          camera.ViewportHeight);
    EXPECT_FLOAT_EQ(builtResolve.InvResolution[0], 1.0f / 1920.0f);
    EXPECT_FLOAT_EQ(builtResolve.InvResolution[1], 1.0f / 1080.0f);
}

// GRAPHICS-075 Slice D.1 — `PostProcessSettings::AntiAliasing` is the
// branch selector for the typed AA stages; the SMAA pass body must
// respect the selector and emit no bind/push/draw when the stage is
// gated off, mirroring `FXAASkipsWhenAntiAliasingNotFXAA`. With
// `AntiAliasing == None` or `FXAA`, SMAA stays silent even with all
// three pipelines bound; with `AntiAliasing == SMAA`, SMAA records
// three Bind/Push/Draw triples. This contract also pins the FXAA/SMAA
// mutual-exclusion invariant from the SMAA side.
TEST(GraphicsPostProcessChainContract, SMAASkipsWhenAntiAliasingNotSMAA)
{
    Graphics::PostProcessSystem post;
    post.Initialize();

    RHI::CameraUBO camera{};
    camera.ViewportWidth = 1280.0f;
    camera.ViewportHeight = 720.0f;

    auto driveAllStages = [&](Graphics::PostProcessSMAAPass& pass, RecordingCommandContext& sink) {
        pass.ExecuteEdge(sink, camera);
        pass.ExecuteBlend(sink, camera);
        pass.ExecuteResolve(sink, camera);
    };

    // AntiAliasing == None — every SMAA per-stage body must stay silent.
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .AntiAliasing = Graphics::PostProcessAntiAliasing::None,
    });
    Graphics::PostProcessSMAAPass smaaNone{post};
    smaaNone.SetEdgePipeline(RHI::PipelineHandle{93u, 1u});
    smaaNone.SetBlendPipeline(RHI::PipelineHandle{94u, 1u});
    smaaNone.SetResolvePipeline(RHI::PipelineHandle{95u, 1u});
    RecordingCommandContext noneCmd;
    driveAllStages(smaaNone, noneCmd);
    EXPECT_TRUE(noneCmd.Events.empty());

    // AntiAliasing == FXAA — SMAA still stays silent (mutually exclusive
    // per `PostProcessSettings::AntiAliasing`).
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .AntiAliasing = Graphics::PostProcessAntiAliasing::FXAA,
    });
    Graphics::PostProcessSMAAPass smaaFxaa{post};
    smaaFxaa.SetEdgePipeline(RHI::PipelineHandle{96u, 1u});
    smaaFxaa.SetBlendPipeline(RHI::PipelineHandle{97u, 1u});
    smaaFxaa.SetResolvePipeline(RHI::PipelineHandle{98u, 1u});
    RecordingCommandContext fxaaCmd;
    driveAllStages(smaaFxaa, fxaaCmd);
    EXPECT_TRUE(fxaaCmd.Events.empty());

    // AntiAliasing == SMAA — SMAA records three Bind/Push/Draw triples
    // (one per per-stage Execute).
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .AntiAliasing = Graphics::PostProcessAntiAliasing::SMAA,
    });
    Graphics::PostProcessSMAAPass smaaActive{post};
    smaaActive.SetEdgePipeline(RHI::PipelineHandle{99u, 1u});
    smaaActive.SetBlendPipeline(RHI::PipelineHandle{100u, 1u});
    smaaActive.SetResolvePipeline(RHI::PipelineHandle{101u, 1u});
    RecordingCommandContext activeCmd;
    driveAllStages(smaaActive, activeCmd);
    ASSERT_EQ(activeCmd.Events.size(), 9u);
    EXPECT_EQ(activeCmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(activeCmd.Events[3].Kind, EventKind::BindPipeline);
    EXPECT_EQ(activeCmd.Events[6].Kind, EventKind::BindPipeline);
}

// GRAPHICS-075 Slice D.1 — partial SMAA pipeline outage: the pass body
// must independently gate each Bind/Push/Draw triple on its own
// pipeline's `IsValid()` so a missing edge/blend/resolve pipeline only
// drops the affected stage rather than collapsing the whole leg. This
// mirrors the bloom helper's per-stage early-skip on the downsample /
// upsample leases.
TEST(GraphicsPostProcessChainContract, SMAARecordsPerStageIndependently)
{
    Graphics::PostProcessSystem post;
    post.Initialize();
    post.SetSettings(Graphics::PostProcessSettings{
        .Enabled = true,
        .AntiAliasing = Graphics::PostProcessAntiAliasing::SMAA,
    });

    RHI::CameraUBO camera{};
    camera.ViewportWidth = 1280.0f;
    camera.ViewportHeight = 720.0f;

    // Only the edge pipeline is bound — the blend + resolve stages stay
    // silent. Driving every per-stage Execute mirrors the renderer's
    // three ordered AA graph passes; the missing pipelines short-circuit
    // their per-stage body.
    Graphics::PostProcessSMAAPass edgeOnly{post};
    edgeOnly.SetEdgePipeline(RHI::PipelineHandle{110u, 1u});
    RecordingCommandContext edgeOnlyCmd;
    edgeOnly.ExecuteEdge(edgeOnlyCmd, camera);
    edgeOnly.ExecuteBlend(edgeOnlyCmd, camera);
    edgeOnly.ExecuteResolve(edgeOnlyCmd, camera);
    ASSERT_EQ(edgeOnlyCmd.Events.size(), 3u);
    EXPECT_EQ(edgeOnlyCmd.LastPushConstantSize,
              sizeof(Graphics::PostProcessSMAAEdgePushConstants));

    // Only the resolve pipeline is bound — the edge + blend stages stay
    // silent and the single recorded push payload is the resolve block.
    Graphics::PostProcessSMAAPass resolveOnly{post};
    resolveOnly.SetResolvePipeline(RHI::PipelineHandle{111u, 1u});
    RecordingCommandContext resolveOnlyCmd;
    resolveOnly.ExecuteEdge(resolveOnlyCmd, camera);
    resolveOnly.ExecuteBlend(resolveOnlyCmd, camera);
    resolveOnly.ExecuteResolve(resolveOnlyCmd, camera);
    ASSERT_EQ(resolveOnlyCmd.Events.size(), 3u);
    EXPECT_EQ(resolveOnlyCmd.LastPushConstantSize,
              sizeof(Graphics::PostProcessSMAAResolvePushConstants));
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


