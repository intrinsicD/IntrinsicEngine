#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;

namespace
{
    using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
    using Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs;
    using Extrinsic::Backends::Vulkan::ToString;
    using Extrinsic::Runtime::Engine;
    using Extrinsic::Runtime::IApplication;

    inline constexpr std::uint32_t kBakeWidth = 16u;
    inline constexpr std::uint32_t kBakeHeight = 16u;
    inline constexpr std::uint32_t kPixelBytes = 4u;

    class ExitAfterFramesApp final : public IApplication
    {
    public:
        explicit ExitAfterFramesApp(const std::uint32_t targetFrames) noexcept
            : m_TargetFrames(targetFrames)
        {
        }

        void OnInitialize(Engine&) override {}
        void OnSimTick(Engine&, double) override {}

        void OnVariableTick(Engine& engine, double, double) override
        {
            ++m_Frames;
            if (m_Frames >= m_TargetFrames)
            {
                engine.RequestExit();
            }
        }

        void OnShutdown(Engine&) override {}

    private:
        std::uint32_t m_TargetFrames = 1u;
        std::uint32_t m_Frames = 0u;
    };

    struct SmokeBootstrap
    {
        std::unique_ptr<Engine> EnginePtr{};
        bool Skipped = false;
        std::string SkipReason{};
    };

    [[nodiscard]] SmokeBootstrap BootstrapEngineForBakeSmoke()
    {
        if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        {
            return SmokeBootstrap{
                .Skipped = true,
                .SkipReason = "GLFW could not initialize in this environment; gpu;vulkan object-space normal bake smoke is opt-in.",
            };
        }

        auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
        config.Window.Title = "Intrinsic object-space normal bake gpu;vulkan smoke";
        config.Window.Width = 128u;
        config.Window.Height = 128u;
        config.Window.Resizable = false;
        config.Render.EnableValidation = false;
        config.Render.EnableVSync = false;

        auto engine = std::make_unique<Engine>(
            config,
            std::make_unique<ExitAfterFramesApp>(4u));
        engine->Initialize();

        const auto initInputs = GetVulkanDeviceOperationalInputs(&engine->GetDevice());
        if (!initInputs.LogicalDeviceReady ||
            !initInputs.SwapchainReady ||
            !initInputs.CommandSyncReady)
        {
            engine->Shutdown();
            return SmokeBootstrap{
                .Skipped = true,
                .SkipReason = "Promoted Vulkan did not reach logical-device/swapchain/command-sync readiness on this host.",
            };
        }

        return SmokeBootstrap{.EnginePtr = std::move(engine)};
    }

    void DestroyBufferIfValid(Extrinsic::RHI::IDevice& device,
                              Extrinsic::RHI::BufferHandle& handle) noexcept
    {
        if (handle.IsValid())
        {
            device.DestroyBuffer(handle);
            handle = {};
        }
    }

    void DestroyTextureIfValid(Extrinsic::RHI::IDevice& device,
                               Extrinsic::RHI::TextureHandle& handle) noexcept
    {
        if (handle.IsValid())
        {
            device.DestroyTexture(handle);
            handle = {};
        }
    }

    void DestroyPipelineIfValid(Extrinsic::RHI::IDevice& device,
                                Extrinsic::RHI::PipelineHandle& handle) noexcept
    {
        if (handle.IsValid())
        {
            device.DestroyPipeline(handle);
            handle = {};
        }
    }

    [[nodiscard]] std::uint8_t QuantizeUnorm8(const float value) noexcept
    {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
    }

    struct ExpectedPixel
    {
        std::uint8_t R = 0u;
        std::uint8_t G = 0u;
        std::uint8_t B = 0u;
        std::uint8_t A = 255u;
    };

    [[nodiscard]] ExpectedPixel ToExpectedPixel(const glm::vec4& rgba) noexcept
    {
        return ExpectedPixel{
            .R = QuantizeUnorm8(rgba.r),
            .G = QuantizeUnorm8(rgba.g),
            .B = QuantizeUnorm8(rgba.b),
            .A = QuantizeUnorm8(rgba.a),
        };
    }

    void ExpectPixelNear(const std::vector<std::byte>& pixels,
                         const std::uint32_t x,
                         const std::uint32_t y,
                         const ExpectedPixel expected)
    {
        const std::size_t offset =
            (static_cast<std::size_t>(y) * kBakeWidth + x) * kPixelBytes;
        ASSERT_LE(offset + kPixelBytes, pixels.size());
        const auto channel = [&](const std::uint32_t component) -> std::uint8_t
        {
            return std::to_integer<std::uint8_t>(pixels[offset + component]);
        };

        constexpr int kTolerance = 3;
        EXPECT_NEAR(static_cast<int>(channel(0)), static_cast<int>(expected.R), kTolerance)
            << "x=" << x << " y=" << y;
        EXPECT_NEAR(static_cast<int>(channel(1)), static_cast<int>(expected.G), kTolerance)
            << "x=" << x << " y=" << y;
        EXPECT_NEAR(static_cast<int>(channel(2)), static_cast<int>(expected.B), kTolerance)
            << "x=" << x << " y=" << y;
        EXPECT_NEAR(static_cast<int>(channel(3)), static_cast<int>(expected.A), kTolerance)
            << "x=" << x << " y=" << y;
    }
}

TEST(ObjectSpaceNormalTextureBakeGpuSmoke, VulkanBakeMatchesCpuContractAtSelectedTexels)
{
    auto bootstrap = BootstrapEngineForBakeSmoke();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;
    engine.Run();

    Extrinsic::RHI::IDevice& device = engine.GetDevice();
    const auto status = EvaluateVulkanDeviceOperationalStatus(&device);
    if (!device.IsOperational())
    {
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during object-space normal bake warmup: status="
                      << ToString(status.Code) << " reason=" << ToString(status.Reason);
        return;
    }

    constexpr std::uint32_t kTargetFirstIndex = 3u;
    constexpr std::uint32_t kSelectedIndexCount = 3u;

    const std::array<glm::vec2, 6u> texcoords{{
        glm::vec2{0.0f, 0.0f},
        glm::vec2{1.0f, 0.0f},
        glm::vec2{0.0f, 1.0f},
        glm::vec2{0.0f, 0.0f},
        glm::vec2{1.0f, 0.0f},
        glm::vec2{0.0f, 1.0f},
    }};
    const std::array<glm::vec3, 6u> normals{{
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 1.0f},
        glm::vec3{-1.0f, 0.0f, 0.0f},
        glm::vec3{-1.0f, 0.0f, 0.0f},
        glm::vec3{-1.0f, 0.0f, 0.0f},
    }};
    const std::array<std::uint32_t, 6u> combinedIndices{{
        0u,
        1u,
        2u,
        3u,
        4u,
        5u,
    }};

    Extrinsic::RHI::BufferHandle texcoordBuffer = device.CreateBuffer({
        .SizeBytes = sizeof(texcoords),
        .Usage = Extrinsic::RHI::BufferUsage::Storage,
        .HostVisible = true,
        .DebugName = "ObjectSpaceNormalBakeGpuSmoke.Texcoords",
    });
    Extrinsic::RHI::BufferHandle normalBuffer = device.CreateBuffer({
        .SizeBytes = sizeof(normals),
        .Usage = Extrinsic::RHI::BufferUsage::Storage,
        .HostVisible = true,
        .DebugName = "ObjectSpaceNormalBakeGpuSmoke.Normals",
    });
    Extrinsic::RHI::BufferHandle indexBuffer = device.CreateBuffer({
        .SizeBytes = sizeof(combinedIndices),
        .Usage = Extrinsic::RHI::BufferUsage::Index,
        .HostVisible = true,
        .DebugName = "ObjectSpaceNormalBakeGpuSmoke.Indices",
    });
    Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer({
        .SizeBytes = static_cast<std::uint64_t>(kBakeWidth) * kBakeHeight * kPixelBytes,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "ObjectSpaceNormalBakeGpuSmoke.Readback",
    });
    Extrinsic::RHI::TextureHandle outputTexture = device.CreateTexture({
        .Width = kBakeWidth,
        .Height = kBakeHeight,
        .Fmt = Extrinsic::RHI::Format::RGBA8_UNORM,
        .Usage = Extrinsic::RHI::TextureUsage::ColorTarget |
                 Extrinsic::RHI::TextureUsage::Sampled |
                 Extrinsic::RHI::TextureUsage::TransferSrc,
        .InitialLayout = Extrinsic::RHI::TextureLayout::Undefined,
        .DebugName = "ObjectSpaceNormalBakeGpuSmoke.Output",
    });

    if (!texcoordBuffer.IsValid() ||
        !normalBuffer.IsValid() ||
        !indexBuffer.IsValid() ||
        !readbackBuffer.IsValid() ||
        !outputTexture.IsValid())
    {
        DestroyTextureIfValid(device, outputTexture);
        DestroyBufferIfValid(device, readbackBuffer);
        DestroyBufferIfValid(device, indexBuffer);
        DestroyBufferIfValid(device, normalBuffer);
        DestroyBufferIfValid(device, texcoordBuffer);
        engine.Shutdown();
        ASSERT_TRUE(false) << "Operational Vulkan device failed to allocate object-space normal bake resources.";
    }

    device.WriteBuffer(texcoordBuffer, texcoords.data(), sizeof(texcoords), 0u);
    device.WriteBuffer(normalBuffer, normals.data(), sizeof(normals), 0u);
    device.WriteBuffer(
        indexBuffer,
        combinedIndices.data(),
        sizeof(combinedIndices),
        0u);

    const std::uint64_t texcoordBDA = device.GetBufferDeviceAddress(texcoordBuffer);
    const std::uint64_t normalBDA = device.GetBufferDeviceAddress(normalBuffer);
    if (texcoordBDA == 0u || normalBDA == 0u)
    {
        DestroyTextureIfValid(device, outputTexture);
        DestroyBufferIfValid(device, readbackBuffer);
        DestroyBufferIfValid(device, indexBuffer);
        DestroyBufferIfValid(device, normalBuffer);
        DestroyBufferIfValid(device, texcoordBuffer);
        engine.Shutdown();
        ASSERT_NE(texcoordBDA, 0u);
        ASSERT_NE(normalBDA, 0u);
    }

    Extrinsic::RHI::PipelineDesc pipelineDesc =
        Extrinsic::Graphics::MakeObjectSpaceNormalTextureBakePipelineDesc(
            Extrinsic::Core::Filesystem::GetShaderPath(
                "shaders/object_space_normal_bake.vert.spv"),
            Extrinsic::Core::Filesystem::GetShaderPath(
                "shaders/object_space_normal_bake.frag.spv"));
    Extrinsic::RHI::PipelineHandle pipeline = device.CreatePipeline(pipelineDesc);
    if (!pipeline.IsValid())
    {
        DestroyTextureIfValid(device, outputTexture);
        DestroyBufferIfValid(device, readbackBuffer);
        DestroyBufferIfValid(device, indexBuffer);
        DestroyBufferIfValid(device, normalBuffer);
        DestroyBufferIfValid(device, texcoordBuffer);
        engine.Shutdown();
        ASSERT_TRUE(pipeline.IsValid())
            << "Operational Vulkan device failed to create the object-space normal bake pipeline.";
    }

    Extrinsic::RHI::FrameHandle frame{};
    if (!device.BeginFrame(frame))
    {
        DestroyPipelineIfValid(device, pipeline);
        DestroyTextureIfValid(device, outputTexture);
        DestroyBufferIfValid(device, readbackBuffer);
        DestroyBufferIfValid(device, indexBuffer);
        DestroyBufferIfValid(device, normalBuffer);
        DestroyBufferIfValid(device, texcoordBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "Operational Vulkan device failed to begin the object-space normal bake frame.";
        return;
    }

    Extrinsic::RHI::ICommandContext& cmd = device.GetGraphicsContext(frame.FrameIndex);
    cmd.Begin();
    cmd.BufferBarrier(texcoordBuffer,
                      Extrinsic::RHI::MemoryAccess::HostWrite,
                      Extrinsic::RHI::MemoryAccess::ShaderRead);
    cmd.BufferBarrier(normalBuffer,
                      Extrinsic::RHI::MemoryAccess::HostWrite,
                      Extrinsic::RHI::MemoryAccess::ShaderRead);
    cmd.BufferBarrier(indexBuffer,
                      Extrinsic::RHI::MemoryAccess::HostWrite,
                      Extrinsic::RHI::MemoryAccess::IndexRead);

    const auto recordResult = Extrinsic::Graphics::RecordObjectSpaceNormalTextureBake(
        cmd,
        Extrinsic::Graphics::ObjectSpaceNormalTextureBakeGpuRecordDesc{
            .Pipeline = pipeline,
            .OutputTexture = outputTexture,
            .IndexBuffer = indexBuffer,
            .TexcoordBDA = texcoordBDA,
            .NormalBDA = normalBDA,
            .FirstIndex = kTargetFirstIndex,
            .IndexCount = kSelectedIndexCount,
            .Width = kBakeWidth,
            .Height = kBakeHeight,
            .InitialLayout = Extrinsic::RHI::TextureLayout::Undefined,
            .FinalLayout = Extrinsic::RHI::TextureLayout::TransferSrc,
        });
    ASSERT_TRUE(recordResult.has_value());

    cmd.CopyTextureToBuffer(outputTexture,
                            Extrinsic::RHI::TextureLayout::TransferSrc,
                            0u,
                            0u,
                            readbackBuffer,
                            0u,
                            0u,
                            0u,
                            kBakeWidth,
                            kBakeHeight);
    cmd.End();
    device.EndFrame(frame);
    device.Present(frame);

    std::vector<std::byte> pixels(
        static_cast<std::size_t>(kBakeWidth) * kBakeHeight * kPixelBytes);
    device.ReadBuffer(readbackBuffer, pixels.data(), pixels.size(), 0u);

    using BakeVertex = Extrinsic::Graphics::ObjectSpaceNormalTextureBakeVertex;
    using BakeTriangle = Extrinsic::Graphics::ObjectSpaceNormalTextureBakeTriangle;
    const std::array<BakeVertex, 6u> vertices{{
        BakeVertex{.Uv = texcoords[0], .Normal = normals[0]},
        BakeVertex{.Uv = texcoords[1], .Normal = normals[1]},
        BakeVertex{.Uv = texcoords[2], .Normal = normals[2]},
        BakeVertex{.Uv = texcoords[3], .Normal = normals[3]},
        BakeVertex{.Uv = texcoords[4], .Normal = normals[4]},
        BakeVertex{.Uv = texcoords[5], .Normal = normals[5]},
    }};
    const std::array<BakeTriangle, 1u> decoyTriangles{{
        BakeTriangle{.A = 0u, .B = 1u, .C = 2u},
    }};
    const std::array<BakeTriangle, 1u> targetTriangles{{
        BakeTriangle{.A = 3u, .B = 4u, .C = 5u},
    }};
    const Extrinsic::Graphics::ObjectSpaceNormalTextureBakeOptions options{
        .Width = kBakeWidth,
        .Height = kBakeHeight,
    };
    const auto resolved =
        Extrinsic::Graphics::ResolveObjectSpaceNormalTextureBakeOptions(options);

    const auto sampleAt =
        [&](const std::uint32_t x,
            const std::uint32_t y,
            const std::span<const BakeTriangle> triangles)
    {
        const glm::vec2 uv =
            Extrinsic::Graphics::UvForObjectSpaceNormalBakeTexelCenter(
                x,
                y,
                resolved);
        const auto sample =
            Extrinsic::Graphics::SampleObjectSpaceNormalTextureBakeAtUv(
                std::span<const BakeVertex>{vertices},
                triangles,
                uv,
                options);
        EXPECT_TRUE(sample.Succeeded())
            << Extrinsic::Graphics::DebugNameForObjectSpaceNormalTextureBakeStatus(
                   sample.Status);
        return sample;
    };
    const auto expectSample =
        [&](const std::uint32_t x,
            const std::uint32_t y,
            const std::span<const BakeTriangle> triangles)
    {
        const auto sample = sampleAt(x, y, triangles);
        ExpectPixelNear(pixels, x, y, ToExpectedPixel(sample.EncodedRgba));
    };

    const auto decoySample = sampleAt(0u, 0u, decoyTriangles);
    const auto targetSample = sampleAt(0u, 0u, targetTriangles);
    const ExpectedPixel decoyPixel = ToExpectedPixel(decoySample.EncodedRgba);
    const ExpectedPixel targetPixel = ToExpectedPixel(targetSample.EncodedRgba);
    EXPECT_NE(decoyPixel.R, targetPixel.R)
        << "The decoy and target slices must encode distinguishable normals.";
    ExpectPixelNear(pixels, 0u, 0u, targetPixel);
    expectSample(1u, 1u, targetTriangles);
    ExpectPixelNear(pixels,
                    15u,
                    15u,
                    ToExpectedPixel(glm::vec4{0.5f, 0.5f, 1.0f, 0.0f}));

    Extrinsic::RHI::TextureHandle dilationScratch = device.CreateTexture(
        Extrinsic::Graphics::
            MakeObjectSpaceNormalTextureBakeDilationScratchTextureDesc(
                Extrinsic::Graphics::ObjectSpaceNormalTextureBakeOptions{
                    .Width = kBakeWidth,
                    .Height = kBakeHeight,
                    .PaddingTexels = 1u,
                },
                "ObjectSpaceNormalBakeGpuSmoke.DilationScratch"));
    Extrinsic::RHI::PipelineHandle dilationPipeline = device.CreatePipeline(
        Extrinsic::Graphics::MakeObjectSpaceNormalTextureBakeDilationPipelineDesc(
            Extrinsic::Core::Filesystem::GetShaderPath(
                "shaders/post_fullscreen.vert.spv"),
            Extrinsic::Core::Filesystem::GetShaderPath(
                "shaders/object_space_normal_dilate.frag.spv")));
    if (!dilationScratch.IsValid() || !dilationPipeline.IsValid())
    {
        DestroyPipelineIfValid(device, dilationPipeline);
        DestroyTextureIfValid(device, dilationScratch);
        DestroyPipelineIfValid(device, pipeline);
        DestroyTextureIfValid(device, outputTexture);
        DestroyBufferIfValid(device, readbackBuffer);
        DestroyBufferIfValid(device, indexBuffer);
        DestroyBufferIfValid(device, normalBuffer);
        DestroyBufferIfValid(device, texcoordBuffer);
        engine.Shutdown();
        ASSERT_TRUE(dilationScratch.IsValid());
        ASSERT_TRUE(dilationPipeline.IsValid());
    }

    Extrinsic::RHI::FrameHandle paddedFrame{};
    if (!device.BeginFrame(paddedFrame))
    {
        DestroyPipelineIfValid(device, dilationPipeline);
        DestroyTextureIfValid(device, dilationScratch);
        DestroyPipelineIfValid(device, pipeline);
        DestroyTextureIfValid(device, outputTexture);
        DestroyBufferIfValid(device, readbackBuffer);
        DestroyBufferIfValid(device, indexBuffer);
        DestroyBufferIfValid(device, normalBuffer);
        DestroyBufferIfValid(device, texcoordBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "Operational Vulkan device failed to begin the padded object-space normal bake frame.";
        return;
    }

    Extrinsic::RHI::ICommandContext& paddedCmd =
        device.GetGraphicsContext(paddedFrame.FrameIndex);
    paddedCmd.Begin();
    const auto paddedRecordResult =
        Extrinsic::Graphics::RecordObjectSpaceNormalTextureBake(
            paddedCmd,
            Extrinsic::Graphics::ObjectSpaceNormalTextureBakeGpuRecordDesc{
                .Pipeline = pipeline,
                .OutputTexture = outputTexture,
                .Dilation =
                    Extrinsic::Graphics::ObjectSpaceNormalTextureBakeDilationResources{
                        .Pipeline = dilationPipeline,
                        .ScratchTexture = dilationScratch,
                        .ScratchInitialLayout =
                            Extrinsic::RHI::TextureLayout::Undefined,
                        .OutputDescriptorSlot =
                            Extrinsic::Graphics::
                                kObjectSpaceNormalBakeDilationOutputDescriptorSlot,
                        .ScratchDescriptorSlot =
                            Extrinsic::Graphics::
                                kObjectSpaceNormalBakeDilationScratchDescriptorSlot,
                    },
                .IndexBuffer = indexBuffer,
                .TexcoordBDA = texcoordBDA,
                .NormalBDA = normalBDA,
                .FirstIndex = 0u,
                .IndexCount = kSelectedIndexCount,
                .Width = kBakeWidth,
                .Height = kBakeHeight,
                .PaddingTexels = 1u,
                .InitialLayout = Extrinsic::RHI::TextureLayout::TransferSrc,
                .FinalLayout = Extrinsic::RHI::TextureLayout::TransferSrc,
            });
    ASSERT_TRUE(paddedRecordResult.has_value());

    paddedCmd.CopyTextureToBuffer(outputTexture,
                                  Extrinsic::RHI::TextureLayout::TransferSrc,
                                  0u,
                                  0u,
                                  readbackBuffer,
                                  0u,
                                  0u,
                                  0u,
                                  kBakeWidth,
                                  kBakeHeight);
    paddedCmd.End();
    device.EndFrame(paddedFrame);
    device.Present(paddedFrame);

    device.ReadBuffer(readbackBuffer, pixels.data(), pixels.size(), 0u);

    expectSample(0u, 0u, decoyTriangles);
    expectSample(1u, 1u, decoyTriangles);

    const auto outsideBeforeDilation =
        Extrinsic::Graphics::SampleObjectSpaceNormalTextureBakeAtUv(
            std::span<const BakeVertex>{vertices},
            std::span<const BakeTriangle>{decoyTriangles},
            Extrinsic::Graphics::UvForObjectSpaceNormalBakeTexelCenter(
                8u,
                8u,
                resolved),
            options);
    EXPECT_EQ(outsideBeforeDilation.Status,
              Extrinsic::Graphics::ObjectSpaceNormalTextureBakeStatus::
                  NoContainingTriangle);
    const auto gutterSource =
        Extrinsic::Graphics::SampleObjectSpaceNormalTextureBakeAtUv(
            std::span<const BakeVertex>{vertices},
            std::span<const BakeTriangle>{decoyTriangles},
            Extrinsic::Graphics::UvForObjectSpaceNormalBakeTexelCenter(
                7u,
                7u,
                resolved),
            options);
    ASSERT_TRUE(gutterSource.Succeeded())
        << Extrinsic::Graphics::DebugNameForObjectSpaceNormalTextureBakeStatus(
               gutterSource.Status);
    ExpectPixelNear(pixels, 8u, 8u, ToExpectedPixel(gutterSource.EncodedRgba));
    ExpectPixelNear(pixels,
                    15u,
                    15u,
                    ToExpectedPixel(glm::vec4{0.5f, 0.5f, 1.0f, 0.0f}));

    device.WaitIdle();
    DestroyPipelineIfValid(device, dilationPipeline);
    DestroyTextureIfValid(device, dilationScratch);
    DestroyPipelineIfValid(device, pipeline);
    DestroyTextureIfValid(device, outputTexture);
    DestroyBufferIfValid(device, readbackBuffer);
    DestroyBufferIfValid(device, indexBuffer);
    DestroyBufferIfValid(device, normalBuffer);
    DestroyBufferIfValid(device, texcoordBuffer);
    engine.Shutdown();
}
