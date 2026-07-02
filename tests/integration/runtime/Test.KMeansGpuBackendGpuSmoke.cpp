#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.KMeansGpuBackend;
import Geometry.KMeans;

namespace
{
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;
    namespace GK = Geometry::KMeans;

    class NoopCommandContext final : public RHI::ICommandContext
    {
    public:
        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle) override {}
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, RHI::IndexType) override {}
        void PushConstants(const void*, std::uint32_t, std::uint32_t) override {}
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess, RHI::MemoryAccess) override {}
        void SubmitBarriers(const RHI::BarrierBatchDesc&) override {}
        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
    };

    struct VulkanSmokeDevice
    {
        std::unique_ptr<Extrinsic::Platform::IWindow> Window{};
        std::unique_ptr<RHI::IDevice> Device{};
        bool Skipped{false};
        std::string SkipReason{};
    };

    [[nodiscard]] VulkanSmokeDevice BootstrapVulkanSmokeDevice()
    {
        if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        {
            return VulkanSmokeDevice{
                .Skipped = true,
                .SkipReason = "GLFW could not initialize in this environment; gpu;vulkan KMeans smoke is opt-in.",
            };
        }

        Extrinsic::Core::Config::WindowConfig windowConfig{};
        windowConfig.Title = "Intrinsic KMeans gpu;vulkan parity smoke";
        windowConfig.Width = 64;
        windowConfig.Height = 64;
        windowConfig.Resizable = false;

        std::unique_ptr<Extrinsic::Platform::IWindow> window =
            Extrinsic::Platform::Backends::Glfw::CreateWindow(windowConfig);
        if (window == nullptr || window->GetNativeHandle() == nullptr)
        {
            return VulkanSmokeDevice{
                .Skipped = true,
                .SkipReason = "GLFW window creation failed; gpu;vulkan KMeans smoke requires a native surface.",
            };
        }

        Extrinsic::Core::Config::RenderConfig renderConfig{};
        renderConfig.EnablePromotedVulkanDevice = true;
        renderConfig.EnableValidation = false;

        std::unique_ptr<RHI::IDevice> device =
            Extrinsic::Backends::Vulkan::CreateVulkanDevice();
        if (device == nullptr)
        {
            return VulkanSmokeDevice{
                .Skipped = true,
                .SkipReason = "Promoted Vulkan device factory returned null.",
            };
        }

        device->Initialize(RHI::MakeDeviceCreateDesc(renderConfig,
                                                     window->GetFramebufferExtent(),
                                                     window->GetNativeHandle()));

        const auto bootstrap =
            Extrinsic::Backends::Vulkan::GetVulkanBootstrapDiagnosticsSnapshot();
        const auto services =
            Extrinsic::Backends::Vulkan::GetVulkanServiceDiagnosticsSnapshot();
        if (bootstrap.Status != Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::RegisteredSwapchainImages ||
            services.Status != Extrinsic::Backends::Vulkan::VulkanServiceBootstrapStatus::Ready ||
            !services.PublicTransferQueueExposed ||
            !bootstrap.BufferDeviceAddressEnabled)
        {
            device->Shutdown();
            return VulkanSmokeDevice{
                .Skipped = true,
                .SkipReason = "Promoted Vulkan did not reach service-ready BDA/transfer state on this host.",
            };
        }

        // This compute-only smoke bypasses the renderer, so publish the same
        // clean validation bit the renderer would set after its warmup frame.
        device->NoteRecipeGraphValidation(true);
        if (!device->IsOperational())
        {
            device->Shutdown();
            return VulkanSmokeDevice{
                .Skipped = true,
                .SkipReason = "Promoted Vulkan services were ready but the operational gate did not flip for the KMeans smoke.",
            };
        }

        return VulkanSmokeDevice{
            .Window = std::move(window),
            .Device = std::move(device),
        };
    }

    [[nodiscard]] bool SubmitKMeansFrame(RHI::IDevice& device,
                                         Runtime::KMeansGpuResourceCache& cache,
                                         Runtime::KMeansGpuPipelineSet pipelines,
                                         std::span<const glm::vec3> points,
                                         std::span<const glm::vec3> seeds,
                                         const Runtime::KMeansGpuPlanDesc& plan,
                                         Runtime::KMeansGpuExecutionResult& outResult)
    {
        RHI::FrameHandle frame{};
        if (!device.BeginFrame(frame))
            return false;

        RHI::ICommandContext& cmd = device.GetGraphicsContext(frame.FrameIndex);
        cmd.Begin();
        outResult = Runtime::RecordKMeansGpuExecution(Runtime::KMeansGpuExecutionDesc{
            .Device = &device,
            .CommandContext = &cmd,
            .ResourceCache = &cache,
            .Pipelines = pipelines,
            .Points = points,
            .SeedCentroids = seeds,
            .Plan = plan,
        });
        cmd.End();
        device.EndFrame(frame);
        device.Present(frame);
        return true;
    }

    [[nodiscard]] bool SubmitNoopFrame(RHI::IDevice& device)
    {
        RHI::FrameHandle frame{};
        if (!device.BeginFrame(frame))
            return false;

        RHI::ICommandContext& cmd = device.GetGraphicsContext(frame.FrameIndex);
        cmd.Begin();
        cmd.End();
        device.EndFrame(frame);
        device.Present(frame);
        return true;
    }

    [[nodiscard]] bool SubmitReadbacksAfterProducerRetired(
        RHI::IDevice& device,
        Runtime::KMeansGpuAsyncReadbacks& readbacks,
        const Runtime::KMeansGpuExecutionResources& resources)
    {
        const std::uint32_t framesInFlight = std::max(device.GetFramesInFlight(), 1u);
        for (std::uint32_t frame = 1u; frame < framesInFlight; ++frame)
        {
            if (!SubmitNoopFrame(device))
                return false;
        }

        RHI::FrameHandle frame{};
        if (!device.BeginFrame(frame))
            return false;

        RHI::ICommandContext& cmd = device.GetGraphicsContext(frame.FrameIndex);
        cmd.Begin();
        const bool queued = readbacks.Enqueue(cmd, resources);
        cmd.End();
        device.EndFrame(frame);
        device.Present(frame);
        return queued;
    }

    [[nodiscard]] bool DrainReadbacksUntilReady(RHI::ITransferQueue& queue,
                                                Graphics::GpuTransfer& transfer,
                                                Runtime::KMeansGpuAsyncReadbacks& readbacks)
    {
        NoopCommandContext noopContext;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (std::chrono::steady_clock::now() < deadline)
        {
            queue.CollectCompleted();
            transfer.DrainCompleted(noopContext);
            if (readbacks.Poll())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }

        queue.CollectCompleted();
        transfer.DrainCompleted(noopContext);
        return readbacks.Poll();
    }

    [[nodiscard]] float MaxCentroidDelta(std::span<const glm::vec3> a,
                                         std::span<const glm::vec3> b)
    {
        float maxDelta = 0.0f;
        const std::size_t count = std::min(a.size(), b.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            maxDelta = std::max(maxDelta, glm::length(a[index] - b[index]));
        }
        return maxDelta;
    }

    [[nodiscard]] std::uint32_t CountLabelMismatches(
        std::span<const std::uint32_t> a,
        std::span<const std::uint32_t> b)
    {
        std::uint32_t mismatches = 0u;
        const std::size_t count = std::min(a.size(), b.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            if (a[index] != b[index])
                ++mismatches;
        }
        return mismatches +
               static_cast<std::uint32_t>(std::max(a.size(), b.size()) - count);
    }
}

TEST(KMeansGpuBackendGpuSmoke, VulkanExecutionMatchesCpuReferenceOnSeparatedClusters)
{
    VulkanSmokeDevice bootstrap = BootstrapVulkanSmokeDevice();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    ASSERT_NE(bootstrap.Device, nullptr);
    RHI::IDevice& device = *bootstrap.Device;

    const std::array<glm::vec3, 6> points{{
        glm::vec3{-2.0f, 0.0f, 0.0f},
        glm::vec3{-1.0f, 1.0f, 0.0f},
        glm::vec3{-1.0f, -1.0f, 0.0f},
        glm::vec3{8.0f, 0.0f, 0.0f},
        glm::vec3{9.0f, 1.0f, 0.0f},
        glm::vec3{9.0f, -1.0f, 0.0f},
    }};
    const std::array<glm::vec3, 2> seeds{{
        points[0],
        points[3],
    }};

    GK::KMeansParams params{};
    params.ClusterCount = static_cast<std::uint32_t>(seeds.size());
    params.MaxIterations = 6u;
    params.ConvergenceTolerance = 1.0e-5f;
    params.Compute = GK::Backend::CPU;

    GK::CpuScratch scratch{};
    const std::optional<GK::KMeansResult> cpu =
        GK::Cluster(points, seeds, params, &scratch);
    ASSERT_TRUE(cpu.has_value());
    ASSERT_EQ(cpu->Labels.size(), points.size());
    ASSERT_EQ(cpu->Centroids.size(), seeds.size());

    const std::string resetShader =
        Extrinsic::Core::Filesystem::GetShaderPath("shaders/kmeans_reset.comp.spv");
    const std::string assignShader =
        Extrinsic::Core::Filesystem::GetShaderPath("shaders/kmeans_assign.comp.spv");
    const std::string updateShader =
        Extrinsic::Core::Filesystem::GetShaderPath("shaders/kmeans_update.comp.spv");
    Runtime::KMeansGpuPipelineSet pipelines{
        .Reset = device.CreatePipeline(
            Runtime::BuildKMeansResetPipelineDesc(resetShader.c_str())),
        .Assign = device.CreatePipeline(
            Runtime::BuildKMeansAssignPipelineDesc(assignShader.c_str())),
        .Update = device.CreatePipeline(
            Runtime::BuildKMeansUpdatePipelineDesc(updateShader.c_str())),
    };
    ASSERT_TRUE(pipelines.IsValid());

    RHI::BufferManager buffers{device};
    Runtime::KMeansGpuResourceCache cache{buffers};
    RHI::ITransferQueue& queue = device.GetTransferQueue();
    Graphics::GpuTransfer transfer{queue};
    Runtime::KMeansGpuAsyncReadbacks readbacks{transfer};

    Runtime::KMeansGpuExecutionResult execution{};
    const Runtime::KMeansGpuPlanDesc plan{
        .PointCount = static_cast<std::uint32_t>(points.size()),
        .ClusterCount = static_cast<std::uint32_t>(seeds.size()),
        .MaxIterations = params.MaxIterations,
        .GroupSize = Runtime::kKMeansGpuGroupSize,
        .ConvergenceTolerance = params.ConvergenceTolerance,
    };

    ASSERT_TRUE(SubmitKMeansFrame(device,
                                  cache,
                                  pipelines,
                                  points,
                                  seeds,
                                  plan,
                                  execution))
        << "failed to submit KMeans compute frame";
    ASSERT_TRUE(execution.Succeeded())
        << Runtime::DebugNameForKMeansGpuStatus(execution.Status);
    EXPECT_TRUE(execution.Recorded);
    EXPECT_TRUE(execution.UploadedInputs);
    EXPECT_TRUE(execution.ReadbackResourcesReady);
    EXPECT_FALSE(execution.CpuFallbackRecommended);
    EXPECT_EQ(cache.AllocationCount(), 1u);
    ASSERT_NE(execution.Resources, nullptr);

    ASSERT_TRUE(SubmitReadbacksAfterProducerRetired(device,
                                                    readbacks,
                                                    *execution.Resources))
        << "failed to enqueue async KMeans GPU readbacks after producer retirement";

    ASSERT_TRUE(DrainReadbacksUntilReady(queue, transfer, readbacks))
        << "timed out waiting for async KMeans GPU readback";

    const Runtime::KMeansGpuReadbackResult gpu = readbacks.Collect(execution.Plan);
    ASSERT_TRUE(gpu.Succeeded()) << gpu.Diagnostic;
    ASSERT_EQ(gpu.Labels.size(), cpu->Labels.size());
    ASSERT_EQ(gpu.Centroids.size(), cpu->Centroids.size());

    const std::uint32_t labelMismatches =
        CountLabelMismatches(gpu.Labels, cpu->Labels);
    const float centroidDelta = MaxCentroidDelta(gpu.Centroids, cpu->Centroids);
    const float inertiaDelta = std::abs(gpu.Inertia - cpu->Inertia);

    EXPECT_EQ(labelMismatches, 0u);
    EXPECT_LT(centroidDelta, 1.0e-4f);
    EXPECT_LT(inertiaDelta, 1.0e-4f);
    EXPECT_EQ(gpu.MaxDistanceIndex, cpu->MaxDistanceIndex);

    device.DestroyPipeline(pipelines.Update);
    device.DestroyPipeline(pipelines.Assign);
    device.DestroyPipeline(pipelines.Reset);
    device.Shutdown();
}
