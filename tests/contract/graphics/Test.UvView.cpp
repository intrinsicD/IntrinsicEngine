#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.UvView;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

namespace
{
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;

    constexpr std::array<glm::vec3, 3> kPositions{{
        {-0.5f, -0.5f, 0.0f},
        { 0.5f, -0.5f, 0.0f},
        { 0.0f,  0.5f, 0.0f},
    }};

    constexpr std::array<glm::vec2, 3> kTexcoords{{
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {0.5f, 1.0f},
    }};

    constexpr std::array<std::uint32_t, 3> kTriangleIndices{{0u, 1u, 2u}};
    constexpr std::array<std::uint32_t, 6> kTriangleLineIndices{{
        0u, 1u,
        1u, 2u,
        2u, 0u,
    }};

    [[nodiscard]] Graphics::GpuWorld::InitDesc TinyWorldDesc()
    {
        Graphics::GpuWorld::InitDesc desc{};
        desc.MaxInstances = 1u;
        desc.MaxGeometryRecords = 4u;
        desc.MaxLights = 1u;
        desc.VertexBufferBytes = 4096u;
        desc.IndexBufferBytes = 4096u;
        desc.DeferredFreeFrames = 0u;
        return desc;
    }

    [[nodiscard]] Graphics::GpuWorld::GeometryUploadDesc TriangleUpload(
        const bool includeTexcoords = true)
    {
        return Graphics::GpuWorld::GeometryUploadDesc{
            .PackedVertexBytes = {},
            .PositionBytes = std::as_bytes(std::span<const glm::vec3>{kPositions}),
            .TexcoordBytes = includeTexcoords
                ? std::as_bytes(std::span<const glm::vec2>{kTexcoords})
                : std::span<const std::byte>{},
            .NormalBytes = {},
            .SurfaceIndices = std::span<const std::uint32_t>{kTriangleIndices},
            .LineIndices = {},
            .VertexCount = static_cast<std::uint32_t>(kPositions.size()),
            .LocalBounds = {},
            .DebugName = includeTexcoords
                ? "uv-view-contract-triangle"
                : "uv-view-contract-no-texcoords",
            .PackedVertexColors = {},
        };
    }

    [[nodiscard]] Graphics::UvViewRequest ValidRequest(
        const Graphics::GpuGeometryHandle geometry)
    {
        return Graphics::UvViewRequest{
            .Enabled = true,
            .RequestToken = 17u,
            .Geometry = geometry,
            .Width = 320u,
            .Height = 192u,
            .Bounds = {
                .MinU = 0.0f,
                .MinV = 0.0f,
                .MaxU = 1.0f,
                .MaxV = 1.0f,
            },
            .Background = Graphics::UvViewBackgroundMode::Grid,
            .BackgroundTexture = RHI::kInvalidBindlessIndex,
            .ShowDistortionHeatmap = false,
            .LineIndices = std::vector<std::uint32_t>{
                kTriangleLineIndices.begin(), kTriangleLineIndices.end()},
            .TriangleConformalDistortion = {},
        };
    }

    [[nodiscard]] bool HasTextureUsage(const RHI::TextureUsage flags,
                                       const RHI::TextureUsage bit) noexcept
    {
        return (static_cast<std::uint32_t>(flags) &
                static_cast<std::uint32_t>(bit)) != 0u;
    }

    struct UvViewFixture
    {
        Extrinsic::Tests::MockDevice Device;
        RHI::BufferManager Buffers{Device};
        RHI::SamplerManager Samplers{Device};
        RHI::TextureManager Textures{Device, Device.GetBindlessHeap()};
        RHI::PipelineManager Pipelines{Device};
        Graphics::GpuWorld World;
        Graphics::UvView View;

        [[nodiscard]] bool Initialize(const bool operational = true)
        {
            Device.Operational = operational;
            if (!World.Initialize(Device, Buffers, TinyWorldDesc()))
                return false;
            View.Initialize(Device, Buffers, Textures, Samplers, Pipelines);
            return true;
        }

        ~UvViewFixture()
        {
            View.Shutdown();
            World.Shutdown();
        }
    };

    void ExpectCpuFallback(const Graphics::UvViewOutput& output)
    {
        EXPECT_EQ(output.ActiveMode, Graphics::UvViewActiveMode::CpuLayout);
        EXPECT_FALSE(output.IsGpuReady());
        EXPECT_FALSE(output.Texture.IsValid());
        EXPECT_EQ(output.BindlessIndex, RHI::kInvalidBindlessIndex);
    }
}

TEST(GraphicsUvViewContract, DisabledAndNonOperationalRequestsStayOnCpuWithoutResources)
{
    UvViewFixture fixture;
    ASSERT_TRUE(fixture.Initialize(false));

    EXPECT_EQ(fixture.Device.CreateBufferCount, 0);
    EXPECT_EQ(fixture.Device.CreateSamplerCount, 0);
    EXPECT_EQ(fixture.Device.CreatePipelineCount, 0);
    EXPECT_EQ(fixture.Device.CreateTextureCount, 0);

    Graphics::UvViewRequest disabled{};
    disabled.RequestToken = 4u;
    disabled.Background = Graphics::UvViewBackgroundMode::Checker;
    fixture.View.Submit(std::move(disabled));
    fixture.View.Prepare(fixture.World);

    const auto& disabledOutput = fixture.View.GetOutput();
    EXPECT_EQ(disabledOutput.Status, Graphics::UvViewStatus::Disabled);
    EXPECT_EQ(disabledOutput.RequestToken, 4u);
    EXPECT_EQ(disabledOutput.RequestedBackground,
              Graphics::UvViewBackgroundMode::Checker);
    ExpectCpuFallback(disabledOutput);
    EXPECT_FALSE(fixture.View.ShouldRecord());

    Graphics::UvViewRequest requested{};
    requested.Enabled = true;
    requested.RequestToken = 5u;
    requested.Width = 128u;
    requested.Height = 128u;
    requested.Background = Graphics::UvViewBackgroundMode::Texture;
    fixture.View.Submit(std::move(requested));
    fixture.View.Prepare(fixture.World);

    const auto& fallbackOutput = fixture.View.GetOutput();
    EXPECT_EQ(fallbackOutput.Status,
              Graphics::UvViewStatus::CpuFallbackNonOperational);
    EXPECT_EQ(fallbackOutput.RequestToken, 5u);
    EXPECT_EQ(fallbackOutput.RequestedBackground,
              Graphics::UvViewBackgroundMode::Texture);
    ExpectCpuFallback(fallbackOutput);
    EXPECT_FALSE(fixture.View.ShouldRecord());

    EXPECT_EQ(fixture.Device.CreateBufferCount, 0);
    EXPECT_EQ(fixture.Device.CreateSamplerCount, 0);
    EXPECT_EQ(fixture.Device.CreatePipelineCount, 0);
    EXPECT_EQ(fixture.Device.CreateTextureCount, 0);
}

TEST(GraphicsUvViewContract, InvalidBoundsTopologyAndMissingTexcoordsFailClosed)
{
    UvViewFixture fixture;
    ASSERT_TRUE(fixture.Initialize());
    EXPECT_EQ(fixture.Device.CreateSamplerCount, 0)
        << "operational initialization must keep the optional UV view lazy";
    EXPECT_EQ(fixture.Device.CreatePipelineCount, 0)
        << "disabled optional work must not perturb renderer pipeline creation";
    const auto geometry = fixture.World.UploadGeometry(TriangleUpload());
    const auto geometryWithoutTexcoords =
        fixture.World.UploadGeometry(TriangleUpload(false));
    ASSERT_TRUE(geometry.IsValid());
    ASSERT_TRUE(geometryWithoutTexcoords.IsValid());

    auto request = ValidRequest(geometry);
    request.Bounds.MaxU = request.Bounds.MinU;
    fixture.View.Submit(std::move(request));
    fixture.View.Prepare(fixture.World);
    EXPECT_EQ(fixture.View.GetOutput().Status,
              Graphics::UvViewStatus::InvalidRequest);
    ExpectCpuFallback(fixture.View.GetOutput());
    EXPECT_FALSE(fixture.View.ShouldRecord());

    request = ValidRequest(geometry);
    request.RequestToken = 18u;
    request.LineIndices = {0u, 1u};
    fixture.View.Submit(std::move(request));
    fixture.View.Prepare(fixture.World);
    EXPECT_EQ(fixture.View.GetOutput().Status,
              Graphics::UvViewStatus::InvalidRequest);
    ExpectCpuFallback(fixture.View.GetOutput());
    EXPECT_FALSE(fixture.View.ShouldRecord());

    request = ValidRequest(geometryWithoutTexcoords);
    request.RequestToken = 19u;
    fixture.View.Submit(std::move(request));
    fixture.View.Prepare(fixture.World);
    EXPECT_EQ(fixture.View.GetOutput().Status,
              Graphics::UvViewStatus::WaitingForGeometry);
    ExpectCpuFallback(fixture.View.GetOutput());
    EXPECT_FALSE(fixture.View.ShouldRecord());

    EXPECT_FALSE(fixture.View.GetTarget().IsValid());
    EXPECT_EQ(fixture.Device.CreateTextureCount, 0);
}

TEST(GraphicsUvViewContract, ResidentTriangleCreatesRetainedSampledTargetAndRecordsAllDraws)
{
    UvViewFixture fixture;
    ASSERT_TRUE(fixture.Initialize());
    const auto geometry = fixture.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(geometry.IsValid());

    auto request = ValidRequest(geometry);
    request.RequestToken = 29u;
    const std::size_t bufferWritesBeforePrepare =
        fixture.Device.BufferWrites.size();
    fixture.View.Submit(request);
    fixture.View.Prepare(fixture.World);

    const Graphics::UvViewOutput firstOutput = fixture.View.GetOutput();
    ASSERT_EQ(firstOutput.Status, Graphics::UvViewStatus::Ready);
    EXPECT_EQ(firstOutput.ActiveMode, Graphics::UvViewActiveMode::GpuShaded);
    EXPECT_FALSE(firstOutput.IsGpuReady());
    EXPECT_FALSE(firstOutput.HasCompletedContents);
    EXPECT_EQ(firstOutput.RequestToken, 29u);
    EXPECT_EQ(firstOutput.Width, 320u);
    EXPECT_EQ(firstOutput.Height, 192u);
    EXPECT_EQ(firstOutput.TargetGeneration, 1u);
    EXPECT_EQ(firstOutput.RecordedPassCount, 0u);
    EXPECT_EQ(fixture.View.GetTarget(), firstOutput.Texture);
    EXPECT_TRUE(fixture.View.ShouldRecord());
    EXPECT_FALSE(fixture.View.TargetHasShaderReadContents());

    ASSERT_EQ(fixture.Device.CreatedTextureDescs.size(), 1u);
    const RHI::TextureDesc& targetDesc = fixture.Device.CreatedTextureDescs.back();
    EXPECT_EQ(targetDesc.Width, 320u);
    EXPECT_EQ(targetDesc.Height, 192u);
    EXPECT_EQ(targetDesc.Fmt, RHI::Format::RGBA8_UNORM);
    EXPECT_TRUE(HasTextureUsage(targetDesc.Usage, RHI::TextureUsage::Sampled));
    EXPECT_TRUE(HasTextureUsage(targetDesc.Usage, RHI::TextureUsage::ColorTarget));
    EXPECT_TRUE(HasTextureUsage(targetDesc.Usage, RHI::TextureUsage::TransferSrc));
    EXPECT_FALSE(HasTextureUsage(targetDesc.Usage, RHI::TextureUsage::Storage));
    EXPECT_NE(firstOutput.BindlessIndex, RHI::kInvalidBindlessIndex);

    ASSERT_EQ(fixture.Device.BufferWrites.size(),
              bufferWritesBeforePrepare + 1u);
    const auto& lineWrite = fixture.Device.BufferWrites.back();
    const RHI::BufferDesc* lineDesc =
        fixture.Buffers.GetDesc(lineWrite.Handle);
    ASSERT_NE(lineDesc, nullptr);
    EXPECT_TRUE(lineDesc->HostVisible);
    EXPECT_TRUE(RHI::HasUsage(lineDesc->Usage, RHI::BufferUsage::Index));
    EXPECT_FALSE(RHI::HasUsage(lineDesc->Usage,
                               RHI::BufferUsage::TransferDst));

    fixture.View.Record(fixture.Device.CommandContext);

    const auto& commands = fixture.Device.CommandContext;
    EXPECT_EQ(commands.BindPipelineCalls, 3);
    EXPECT_EQ(commands.DrawCalls, 1);
    EXPECT_EQ(commands.LastDraw.VertexCount, 3u);
    EXPECT_EQ(commands.DrawIndexedCalls, 2);
    EXPECT_EQ(commands.BindIndexBufferCalls, 2);
    EXPECT_EQ(commands.LastDrawIndexed.IndexCount, 6u);
    EXPECT_EQ(commands.LastScissor.Width, 320u);
    EXPECT_EQ(commands.LastScissor.Height, 192u);
    EXPECT_EQ(commands.PushConstantsCalls, 3);
    EXPECT_TRUE(commands.BufferBarrierCalls.empty());
    EXPECT_EQ(fixture.View.GetOutput().RecordedPassCount, 1u);
    EXPECT_FALSE(fixture.View.TargetHasShaderReadContents());
    fixture.View.CompleteFrame(false);
    EXPECT_FALSE(fixture.View.TargetHasShaderReadContents());
    EXPECT_FALSE(fixture.View.GetOutput().IsGpuReady());

    fixture.View.Record(fixture.Device.CommandContext);
    fixture.View.CompleteFrame(true);
    EXPECT_TRUE(fixture.View.TargetHasShaderReadContents());
    EXPECT_TRUE(fixture.View.GetOutput().HasCompletedContents);
    EXPECT_TRUE(fixture.View.GetOutput().IsGpuReady());

    const int createdTextures = fixture.Device.CreateTextureCount;
    fixture.View.Submit(std::move(request));
    fixture.View.Prepare(fixture.World);
    EXPECT_EQ(fixture.View.GetOutput().Texture, firstOutput.Texture);
    EXPECT_EQ(fixture.View.GetOutput().TargetGeneration,
              firstOutput.TargetGeneration);
    EXPECT_EQ(fixture.Device.CreateTextureCount, createdTextures);
    EXPECT_TRUE(fixture.View.TargetHasShaderReadContents());
}

TEST(GraphicsUvViewContract, TextureAndHeatmapFallbacksRemainExplicit)
{
    UvViewFixture fixture;
    ASSERT_TRUE(fixture.Initialize());
    const auto geometry = fixture.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(geometry.IsValid());

    auto request = ValidRequest(geometry);
    request.Background = Graphics::UvViewBackgroundMode::Texture;
    request.BackgroundTexture = RHI::kInvalidBindlessIndex;
    request.ShowDistortionHeatmap = true;
    request.TriangleConformalDistortion = {1.25f};
    const std::size_t bufferWritesBeforePrepare =
        fixture.Device.BufferWrites.size();
    fixture.View.Submit(request);
    fixture.View.Prepare(fixture.World);

    const auto& active = fixture.View.GetOutput();
    ASSERT_EQ(active.Status, Graphics::UvViewStatus::Ready);
    EXPECT_EQ(active.RequestedBackground,
              Graphics::UvViewBackgroundMode::Texture);
    EXPECT_EQ(active.ActiveBackground,
              Graphics::UvViewBackgroundMode::Checker);
    EXPECT_TRUE(active.HeatmapActive);
    EXPECT_NE(active.Diagnostic.find("fell back to checker"), std::string::npos);

    bool sawHostVisibleLineBuffer = false;
    bool sawHostVisibleDistortionBuffer = false;
    for (std::size_t index = bufferWritesBeforePrepare;
         index < fixture.Device.BufferWrites.size(); ++index)
    {
        const RHI::BufferDesc* desc = fixture.Buffers.GetDesc(
            fixture.Device.BufferWrites[index].Handle);
        ASSERT_NE(desc, nullptr);
        EXPECT_TRUE(desc->HostVisible);
        EXPECT_FALSE(RHI::HasUsage(desc->Usage,
                                   RHI::BufferUsage::TransferDst));
        sawHostVisibleLineBuffer = sawHostVisibleLineBuffer ||
            RHI::HasUsage(desc->Usage, RHI::BufferUsage::Index);
        sawHostVisibleDistortionBuffer = sawHostVisibleDistortionBuffer ||
            RHI::HasUsage(desc->Usage, RHI::BufferUsage::Storage);
    }
    EXPECT_TRUE(sawHostVisibleLineBuffer);
    EXPECT_TRUE(sawHostVisibleDistortionBuffer);

    fixture.View.Record(fixture.Device.CommandContext);
    EXPECT_TRUE(fixture.Device.CommandContext.BufferBarrierCalls.empty());
    ASSERT_FALSE(fixture.Device.CommandContext.PushConstantPayloads.empty());
    Graphics::UvViewPushConstants activeConstants{};
    const auto& activePayload =
        fixture.Device.CommandContext.PushConstantPayloads.back();
    ASSERT_EQ(activePayload.size(), sizeof(activeConstants));
    std::memcpy(&activeConstants, activePayload.data(), sizeof(activeConstants));
    EXPECT_EQ(activeConstants.BackgroundMode,
              static_cast<std::uint32_t>(Graphics::UvViewBackgroundMode::Checker));
    EXPECT_EQ(activeConstants.ShowHeatmap, 1u);
    EXPECT_NE(activeConstants.DistortionBDA, 0u);

    request.BackgroundTexture =
        std::numeric_limits<RHI::BindlessIndex>::max();
    request.RequestToken = 18u;
    fixture.View.Submit(request);
    fixture.View.Prepare(fixture.World);

    const auto& outOfRangeSentinelFallback = fixture.View.GetOutput();
    ASSERT_EQ(outOfRangeSentinelFallback.Status,
              Graphics::UvViewStatus::Ready);
    EXPECT_EQ(outOfRangeSentinelFallback.RequestedBackground,
              Graphics::UvViewBackgroundMode::Texture);
    EXPECT_EQ(outOfRangeSentinelFallback.ActiveBackground,
              Graphics::UvViewBackgroundMode::Checker);
    EXPECT_NE(outOfRangeSentinelFallback.Diagnostic.find(
                  "fell back to checker"),
              std::string::npos);

    const std::uint32_t bindlessCapacity =
        fixture.Device.GetBindlessHeap().GetCapacity();
    ASSERT_LT(bindlessCapacity,
              std::numeric_limits<RHI::BindlessIndex>::max());
    for (const RHI::BindlessIndex outOfRange : {
             bindlessCapacity,
             bindlessCapacity + 1u,
         })
    {
        request.BackgroundTexture = outOfRange;
        ++request.RequestToken;
        fixture.View.Submit(request);
        fixture.View.Prepare(fixture.World);
        const auto& capacityFallback = fixture.View.GetOutput();
        ASSERT_EQ(capacityFallback.Status, Graphics::UvViewStatus::Ready);
        EXPECT_EQ(capacityFallback.RequestedBackground,
                  Graphics::UvViewBackgroundMode::Texture);
        EXPECT_EQ(capacityFallback.ActiveBackground,
                  Graphics::UvViewBackgroundMode::Checker);
        EXPECT_NE(capacityFallback.Diagnostic.find("fell back to checker"),
                  std::string::npos);
    }

    request.Background = Graphics::UvViewBackgroundMode::Grid;
    ++request.RequestToken;
    request.ShowDistortionHeatmap = true;
    request.TriangleConformalDistortion.clear();
    fixture.View.Submit(std::move(request));
    fixture.View.Prepare(fixture.World);

    const auto& plainFill = fixture.View.GetOutput();
    ASSERT_EQ(plainFill.Status, Graphics::UvViewStatus::Ready);
    EXPECT_FALSE(plainFill.HeatmapActive);
    EXPECT_NE(plainFill.Diagnostic.find("plain fill"), std::string::npos);

    fixture.View.Record(fixture.Device.CommandContext);
    Graphics::UvViewPushConstants plainConstants{};
    const auto& plainPayload =
        fixture.Device.CommandContext.PushConstantPayloads.back();
    ASSERT_EQ(plainPayload.size(), sizeof(plainConstants));
    std::memcpy(&plainConstants, plainPayload.data(), sizeof(plainConstants));
    EXPECT_EQ(plainConstants.ShowHeatmap, 0u);
    EXPECT_EQ(plainConstants.DistortionBDA, 0u);
}

TEST(GraphicsUvViewContract, ResizeReplacesTargetAndIncrementsGeneration)
{
    UvViewFixture fixture;
    ASSERT_TRUE(fixture.Initialize());
    const auto geometry = fixture.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(geometry.IsValid());

    auto request = ValidRequest(geometry);
    request.Width = 128u;
    request.Height = 96u;
    fixture.View.Submit(request);
    fixture.View.Prepare(fixture.World);
    fixture.View.Record(fixture.Device.CommandContext);
    fixture.View.CompleteFrame(true);
    const Graphics::UvViewOutput first = fixture.View.GetOutput();
    ASSERT_TRUE(first.IsGpuReady());

    request.Width = 257u;
    request.Height = 129u;
    request.RequestToken = 18u;
    fixture.View.Submit(std::move(request));
    fixture.View.Prepare(fixture.World);
    fixture.View.Record(fixture.Device.CommandContext);
    fixture.View.CompleteFrame(true);
    const Graphics::UvViewOutput resized = fixture.View.GetOutput();

    ASSERT_TRUE(resized.IsGpuReady());
    EXPECT_NE(resized.Texture, first.Texture);
    EXPECT_EQ(resized.Width, 257u);
    EXPECT_EQ(resized.Height, 129u);
    EXPECT_EQ(resized.TargetGeneration, first.TargetGeneration + 1u);
    EXPECT_EQ(fixture.Device.CreateTextureCount, 2);
    EXPECT_EQ(fixture.Device.DestroyTextureCount, 0)
        << "the old sampled target remains leased until its in-flight frame retires";
    EXPECT_TRUE(fixture.View.TargetHasShaderReadContents());
}

TEST(GraphicsUvViewContract, SubmissionHeartbeatDisablesUnrefreshedRetainedWork)
{
    UvViewFixture fixture;
    ASSERT_TRUE(fixture.Initialize());
    const auto geometry = fixture.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(geometry.IsValid());

    auto request = ValidRequest(geometry);
    fixture.View.Submit(request);
    fixture.View.Prepare(fixture.World);
    ASSERT_EQ(fixture.View.GetOutput().Status, Graphics::UvViewStatus::Ready);

    const RHI::TextureHandle target = fixture.View.GetTarget();
    const std::uint64_t generation =
        fixture.View.GetOutput().TargetGeneration;
    const int createdTextures = fixture.Device.CreateTextureCount;
    const std::size_t bufferWrites = fixture.Device.BufferWrites.size();

    fixture.View.Submit(request);
    fixture.View.Prepare(fixture.World);
    EXPECT_EQ(fixture.View.GetOutput().Status, Graphics::UvViewStatus::Ready);
    EXPECT_EQ(fixture.View.GetTarget(), target);
    EXPECT_EQ(fixture.View.GetOutput().TargetGeneration, generation);
    EXPECT_EQ(fixture.Device.CreateTextureCount, createdTextures);
    EXPECT_EQ(fixture.Device.BufferWrites.size(), bufferWrites)
        << "a same-token heartbeat must not recopy dense request payloads";

    fixture.View.Prepare(fixture.World);
    EXPECT_EQ(fixture.View.GetOutput().Status,
              Graphics::UvViewStatus::Disabled);
    ExpectCpuFallback(fixture.View.GetOutput());
    EXPECT_FALSE(fixture.View.ShouldRecord());
    EXPECT_EQ(fixture.View.GetTarget(), target)
        << "the retained target may stay leased while its work is unpublished";
    EXPECT_EQ(fixture.Device.CreateTextureCount, createdTextures);
}

TEST(GraphicsUvViewContract, RebuildClearsPublishedReadinessAndRequiresRefresh)
{
    UvViewFixture fixture;
    ASSERT_TRUE(fixture.Initialize());
    const auto geometry = fixture.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(geometry.IsValid());

    auto request = ValidRequest(geometry);
    fixture.View.Submit(request);
    fixture.View.Prepare(fixture.World);
    fixture.View.Record(fixture.Device.CommandContext);
    fixture.View.CompleteFrame(true);
    ASSERT_TRUE(fixture.View.GetOutput().IsGpuReady());
    const RHI::TextureHandle oldTarget = fixture.View.GetTarget();
    const int createdPipelines = fixture.Device.CreatePipelineCount;

    ASSERT_TRUE(fixture.View.RebuildOperationalResources(fixture.Device));
    EXPECT_EQ(fixture.View.GetOutput().Status,
              Graphics::UvViewStatus::Disabled);
    ExpectCpuFallback(fixture.View.GetOutput());
    EXPECT_FALSE(fixture.View.ShouldRecord());
    EXPECT_FALSE(fixture.View.GetTarget().IsValid());
    EXPECT_FALSE(fixture.View.TargetHasShaderReadContents());
    EXPECT_EQ(fixture.Device.CreatePipelineCount, createdPipelines)
        << "device rebuild keeps optional UV resources lazy until refresh";

    fixture.View.Submit(std::move(request));
    fixture.View.Prepare(fixture.World);
    ASSERT_EQ(fixture.View.GetOutput().Status, Graphics::UvViewStatus::Ready);
    EXPECT_TRUE(fixture.View.GetTarget().IsValid());
    EXPECT_NE(fixture.View.GetTarget(), oldTarget);
}

TEST(GraphicsUvViewContract, TargetExtentAndUvFitLimitsFailClosed)
{
    UvViewFixture fixture;
    ASSERT_TRUE(fixture.Initialize());
    const auto geometry = fixture.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(geometry.IsValid());

    auto request = ValidRequest(geometry);
    request.Width = 4097u;
    fixture.View.Submit(std::move(request));
    fixture.View.Prepare(fixture.World);
    EXPECT_EQ(fixture.View.GetOutput().Status,
              Graphics::UvViewStatus::InvalidRequest);
    EXPECT_FALSE(fixture.View.GetTarget().IsValid());
    EXPECT_EQ(fixture.Device.CreateTextureCount, 0);

    request = ValidRequest(geometry);
    request.RequestToken = 18u;
    request.Bounds = {
        .MinU = -std::numeric_limits<float>::max(),
        .MinV = -std::numeric_limits<float>::max(),
        .MaxU = std::numeric_limits<float>::max(),
        .MaxV = std::numeric_limits<float>::max(),
    };
    fixture.View.Submit(std::move(request));
    fixture.View.Prepare(fixture.World);
    EXPECT_EQ(fixture.View.GetOutput().Status,
              Graphics::UvViewStatus::InvalidRequest);
    EXPECT_NE(fixture.View.GetOutput().Diagnostic.find("represented"),
              std::string::npos);
    EXPECT_FALSE(fixture.View.GetTarget().IsValid());
    EXPECT_EQ(fixture.Device.CreateTextureCount, 0);

    request = ValidRequest(geometry);
    request.RequestToken = 19u;
    request.Width = 4096u;
    request.Height = 4096u;
    fixture.View.Submit(std::move(request));
    fixture.View.Prepare(fixture.World);
    ASSERT_EQ(fixture.View.GetOutput().Status, Graphics::UvViewStatus::Ready);
    EXPECT_EQ(fixture.View.GetOutput().Width, 4096u);
    EXPECT_EQ(fixture.View.GetOutput().Height, 4096u);
}

TEST(GraphicsUvViewContract, EveryBackgroundModeReachesTheShaderContract)
{
    UvViewFixture fixture;
    ASSERT_TRUE(fixture.Initialize());
    const auto geometry = fixture.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(geometry.IsValid());

    constexpr std::array modes{
        Graphics::UvViewBackgroundMode::Grid,
        Graphics::UvViewBackgroundMode::Checker,
        Graphics::UvViewBackgroundMode::TexelDensity,
        Graphics::UvViewBackgroundMode::Texture,
    };
    for (std::size_t index = 0u; index < modes.size(); ++index)
    {
        auto request = ValidRequest(geometry);
        request.RequestToken = 100u + index;
        request.Background = modes[index];
        request.BackgroundTexture = modes[index] ==
                Graphics::UvViewBackgroundMode::Texture
            ? 41u
            : RHI::kInvalidBindlessIndex;
        fixture.View.Submit(std::move(request));
        fixture.View.Prepare(fixture.World);

        const auto& output = fixture.View.GetOutput();
        ASSERT_EQ(output.Status, Graphics::UvViewStatus::Ready);
        EXPECT_EQ(output.RequestedBackground, modes[index]);
        EXPECT_EQ(output.ActiveBackground, modes[index]);
        EXPECT_TRUE(output.Diagnostic.empty());

        fixture.View.Record(fixture.Device.CommandContext);
        ASSERT_FALSE(
            fixture.Device.CommandContext.PushConstantPayloads.empty());
        Graphics::UvViewPushConstants constants{};
        const auto& payload =
            fixture.Device.CommandContext.PushConstantPayloads.back();
        ASSERT_EQ(payload.size(), sizeof(constants));
        std::memcpy(&constants, payload.data(), sizeof(constants));
        EXPECT_EQ(constants.BackgroundMode,
                  static_cast<std::uint32_t>(modes[index]));
        EXPECT_EQ(constants.BackgroundTextureBindlessIndex,
                  modes[index] == Graphics::UvViewBackgroundMode::Texture
                      ? 41u
                      : RHI::kInvalidBindlessIndex);
        fixture.View.CompleteFrame(false);
    }
    EXPECT_TRUE(fixture.Device.CommandContext.BufferBarrierCalls.empty());
}

TEST(GraphicsUvViewContract, GeometryRecordLookupReturnsResidentDataAndRejectsFreedHandle)
{
    UvViewFixture fixture;
    ASSERT_TRUE(fixture.Initialize());
    const auto geometry = fixture.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(geometry.IsValid());

    RHI::GpuGeometryRecord record{};
    ASSERT_TRUE(fixture.World.TryGetGeometryRecord(geometry, record));

    const std::uint64_t vertexBda = fixture.Device.GetBufferDeviceAddress(
        fixture.World.GetManagedVertexBuffer());
    const std::uint64_t indexBda = fixture.Device.GetBufferDeviceAddress(
        fixture.World.GetManagedIndexBuffer());
    EXPECT_EQ(record.VertexBufferBDA, vertexBda);
    EXPECT_EQ(record.IndexBufferBDA, indexBda);
    constexpr std::uint64_t texcoordOffset =
        (sizeof(glm::vec3) * kPositions.size() + 7u) & ~std::uint64_t{7u};
    EXPECT_EQ(record.TexcoordBufferBDA,
              vertexBda + texcoordOffset);
    EXPECT_EQ(record.TexcoordBufferBDA % 8u, 0u);
    EXPECT_EQ(record.NormalBufferBDA, 0u);
    EXPECT_EQ(record.VertexOffset, 0u);
    EXPECT_EQ(record.VertexCount, 3u);
    EXPECT_EQ(record.SurfaceFirstIndex, 0u);
    EXPECT_EQ(record.SurfaceIndexCount, 3u);
    EXPECT_EQ(record.LineFirstIndex, 3u);
    EXPECT_EQ(record.LineIndexCount, 0u);
    EXPECT_EQ(record.PointFirstVertex, 0u);
    EXPECT_EQ(record.PointVertexCount, 3u);
    EXPECT_EQ(record.BufferID, 0u);
    EXPECT_EQ(record.Flags, 0u);
    EXPECT_EQ(record.ColorBufferBDA, 0u);

    fixture.World.FreeGeometry(geometry);
    record.VertexBufferBDA = 123u;
    EXPECT_FALSE(fixture.World.TryGetGeometryRecord(geometry, record));
    EXPECT_EQ(record.VertexBufferBDA, 0u);

    fixture.World.SyncFrame();
    const auto replacement = fixture.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(replacement.IsValid());
    EXPECT_EQ(replacement.Index, geometry.Index);
    EXPECT_NE(replacement.Generation, geometry.Generation);
    EXPECT_FALSE(fixture.World.TryGetGeometryRecord(geometry, record));
    EXPECT_TRUE(fixture.World.TryGetGeometryRecord(replacement, record));
}
