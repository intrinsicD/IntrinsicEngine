#include <gtest/gtest.h>

#include "ProgressivePoissonReference.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <glm/glm.hpp>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;
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
import Extrinsic.Runtime.ProgressivePoissonGpuBackend;

namespace
{
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;
    namespace Ppr =
        Intrinsic::Methods::Geometry::ProgressivePoissonReference;

    class NoopCommandContext final : public RHI::ICommandContext
    {
    public:
        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t,
                        std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle) override {}
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t,
                             RHI::IndexType) override {}
        void PushConstants(const void*, std::uint32_t, std::uint32_t) override {}
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t,
                  std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t,
                         std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t,
                          std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t,
                                 std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t,
                                      RHI::BufferHandle, std::uint64_t,
                                      std::uint32_t) override {}
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t,
                               RHI::BufferHandle, std::uint64_t,
                               std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout,
                            RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess,
                           RHI::MemoryAccess) override {}
        void SubmitBarriers(const RHI::BarrierBatchDesc&) override {}
        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t,
                        std::uint32_t) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t,
                        std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t,
                                 RHI::TextureHandle, std::uint32_t,
                                 std::uint32_t) override {}
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
                .SkipReason =
                    "GLFW could not initialize; the gpu;vulkan Poisson result-transport smoke is opt-in.",
            };
        }

        Extrinsic::Core::Config::WindowConfig windowConfig{};
        windowConfig.Title = "Intrinsic Progressive Poisson result transport";
        windowConfig.Width = 64;
        windowConfig.Height = 64;
        windowConfig.Resizable = false;

        std::unique_ptr<Extrinsic::Platform::IWindow> window =
            Extrinsic::Platform::Backends::Glfw::CreateWindow(windowConfig);
        if (window == nullptr || window->GetNativeHandle() == nullptr)
        {
            return VulkanSmokeDevice{
                .Skipped = true,
                .SkipReason =
                    "GLFW window creation failed; the Poisson result-transport smoke requires a native surface.",
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

        device->Initialize(RHI::MakeDeviceCreateDesc(
            renderConfig,
            window->GetFramebufferExtent(),
            window->GetNativeHandle()));

        const auto bootstrap = Extrinsic::Backends::Vulkan::
            GetVulkanBootstrapDiagnosticsSnapshot();
        const auto services = Extrinsic::Backends::Vulkan::
            GetVulkanServiceDiagnosticsSnapshot();
        if (bootstrap.Status != Extrinsic::Backends::Vulkan::
                                    VulkanBootstrapStatus::RegisteredSwapchainImages ||
            services.Status != Extrinsic::Backends::Vulkan::
                                   VulkanServiceBootstrapStatus::Ready ||
            !services.PublicTransferQueueExposed ||
            !bootstrap.BufferDeviceAddressEnabled)
        {
            device->Shutdown();
            return VulkanSmokeDevice{
                .Skipped = true,
                .SkipReason =
                    "Promoted Vulkan did not reach service-ready BDA/transfer state on this host.",
            };
        }

        device->NoteRecipeGraphValidation(true);
        if (!device->IsOperational())
        {
            device->Shutdown();
            return VulkanSmokeDevice{
                .Skipped = true,
                .SkipReason =
                    "Promoted Vulkan services were ready but the operational gate did not flip for the Poisson transport smoke.",
            };
        }

        return VulkanSmokeDevice{
            .Window = std::move(window),
            .Device = std::move(device),
        };
    }

    [[nodiscard]] RHI::BufferDesc ResultBufferDesc(
        const std::uint64_t sizeBytes,
        const char* debugName) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = sizeBytes,
            .Usage = RHI::BufferUsage::Storage |
                     RHI::BufferUsage::TransferSrc |
                     RHI::BufferUsage::TransferDst,
            .HostVisible = false,
            .DebugName = debugName,
        };
    }

    [[nodiscard]] bool SubmitReadbackFrame(
        RHI::IDevice& device,
        Runtime::ProgressivePoissonGpuResultReadback& readback,
        const Runtime::ProgressivePoissonGpuExecutionResources& resources,
        const Runtime::ProgressivePoissonGpuDispatchPlan& plan)
    {
        RHI::FrameHandle frame{};
        if (!device.BeginFrame(frame))
            return false;

        RHI::ICommandContext& cmd =
            device.GetGraphicsContext(frame.FrameIndex);
        cmd.Begin();
        const bool queued = readback.Enqueue(cmd, resources, plan);
        cmd.End();
        device.EndFrame(frame);
        device.Present(frame);
        return queued;
    }

    [[nodiscard]] bool DrainReadbackUntilReady(
        RHI::ITransferQueue& queue,
        Graphics::GpuTransfer& transfer,
        Runtime::ProgressivePoissonGpuResultReadback& readback)
    {
        NoopCommandContext noopContext{};
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (std::chrono::steady_clock::now() < deadline)
        {
            queue.CollectCompleted();
            transfer.DrainCompleted(noopContext);
            if (readback.Poll())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }

        queue.CollectCompleted();
        transfer.DrainCompleted(noopContext);
        return readback.Poll();
    }
}

TEST(ProgressivePoissonGpuResultReadbackGpuSmoke,
     VulkanBatchTransportsReferenceShapedResultWithoutBlockingRead)
{
    VulkanSmokeDevice bootstrap = BootstrapVulkanSmokeDevice();
    if (bootstrap.Skipped)
        GTEST_SKIP() << bootstrap.SkipReason;
    ASSERT_NE(bootstrap.Device, nullptr);
    RHI::IDevice& device = *bootstrap.Device;

    const std::array<glm::vec3, 8u> points{{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
        glm::vec3{1.0f, 1.0f, 0.0f},
        glm::vec3{0.25f, 0.25f, 0.0f},
        glm::vec3{0.75f, 0.25f, 0.0f},
        glm::vec3{0.25f, 0.75f, 0.0f},
        glm::vec3{0.75f, 0.75f, 0.0f},
    }};

    Ppr::Config referenceConfig{};
    referenceConfig.Dimension = 2u;
    referenceConfig.GridWidth = 4u;
    referenceConfig.MaxLevels = 1u;
    referenceConfig.HashLoadFactor = 0.25f;
    referenceConfig.RadiusAlpha = 0.5f;
    referenceConfig.RandomizeGridOrigin = false;
    referenceConfig.ShuffleWithinLevels = false;
    const Ppr::Result reference = Ppr::Compute(points, referenceConfig);
    ASSERT_EQ(reference.Diag.Code, Ppr::ValidationCode::Valid);
    ASSERT_EQ(reference.LevelOffsets.size(), 2u);
    ASSERT_FALSE(reference.Order.empty());
    ASSERT_EQ(reference.Order.size(), reference.SplatRadii.size());

    Runtime::ProgressivePoissonGpuPlanDesc planDesc{};
    planDesc.InputCount = static_cast<std::uint32_t>(points.size());
    planDesc.Config.Dimension = referenceConfig.Dimension;
    planDesc.Config.GridWidth = referenceConfig.GridWidth;
    planDesc.Config.MaxLevels = referenceConfig.MaxLevels;
    planDesc.Config.HashLoadFactor = referenceConfig.HashLoadFactor;
    planDesc.Config.RadiusAlpha = referenceConfig.RadiusAlpha;
    planDesc.Config.RandomizeGridOrigin =
        referenceConfig.RandomizeGridOrigin;
    planDesc.Config.ShuffleWithinLevels =
        referenceConfig.ShuffleWithinLevels;
    const Runtime::ProgressivePoissonGpuDispatchPlan plan =
        Runtime::ComputeProgressivePoissonGpuDispatchPlan(planDesc);
    ASSERT_TRUE(plan.IsValid());

    std::vector<std::uint32_t> paddedOrder(points.size(), 0u);
    std::copy(reference.Order.begin(),
              reference.Order.end(),
              paddedOrder.begin());
    std::vector<float> paddedSplatRadii(points.size(), 0.0f);
    std::copy(reference.SplatRadii.begin(),
              reference.SplatRadii.end(),
              paddedSplatRadii.begin());

    {
        RHI::BufferManager buffers{device};
        Runtime::ProgressivePoissonGpuExecutionResources resources{};
        auto orderLease = buffers.Create(ResultBufferDesc(
            static_cast<std::uint64_t>(paddedOrder.size() *
                                       sizeof(std::uint32_t)),
            "ProgressivePoisson.TransportSmoke.Order"));
        auto offsetsLease = buffers.Create(ResultBufferDesc(
            static_cast<std::uint64_t>(reference.LevelOffsets.size() *
                                       sizeof(std::uint32_t)),
            "ProgressivePoisson.TransportSmoke.LevelOffsets"));
        auto splatsLease = buffers.Create(ResultBufferDesc(
            static_cast<std::uint64_t>(paddedSplatRadii.size() * sizeof(float)),
            "ProgressivePoisson.TransportSmoke.SplatRadii"));
        ASSERT_TRUE(orderLease.has_value());
        ASSERT_TRUE(offsetsLease.has_value());
        ASSERT_TRUE(splatsLease.has_value());

        resources.Resources.AcceptedKeys = orderLease->GetHandle();
        resources.Resources.LevelOffsets = offsetsLease->GetHandle();
        resources.Resources.SplatRadii = splatsLease->GetHandle();
        resources.Leases.push_back(std::move(*orderLease));
        resources.Leases.push_back(std::move(*offsetsLease));
        resources.Leases.push_back(std::move(*splatsLease));

        // This fixture seeds a CPU-reference-shaped producer payload. It proves
        // the RUNTIME-195 Vulkan transport/parser seam only; METHOD-014 still
        // owns Progressive Poisson compute execution and public GPU parity.
        device.WriteBuffer(resources.Resources.AcceptedKeys,
                           paddedOrder.data(),
                           static_cast<std::uint64_t>(paddedOrder.size() *
                                                      sizeof(std::uint32_t)),
                           0u);
        device.WriteBuffer(resources.Resources.LevelOffsets,
                           reference.LevelOffsets.data(),
                           static_cast<std::uint64_t>(
                               reference.LevelOffsets.size() *
                               sizeof(std::uint32_t)),
                           0u);
        device.WriteBuffer(resources.Resources.SplatRadii,
                           paddedSplatRadii.data(),
                           static_cast<std::uint64_t>(paddedSplatRadii.size() *
                                                      sizeof(float)),
                           0u);

        RHI::ITransferQueue& queue = device.GetTransferQueue();
        Graphics::GpuTransfer transfer{queue};
        Runtime::ProgressivePoissonGpuResultReadback readback{transfer};
        ASSERT_TRUE(SubmitReadbackFrame(device, readback, resources, plan))
            << "failed to enqueue the Progressive Poisson result batch";
        ASSERT_TRUE(DrainReadbackUntilReady(queue, transfer, readback))
            << "timed out waiting for the Progressive Poisson result batch";

        const Runtime::ProgressivePoissonGpuReadbackResult result =
            readback.Collect(plan);
        ASSERT_TRUE(result.Succeeded()) << result.Diagnostic;
        EXPECT_EQ(result.Order, reference.Order);
        EXPECT_EQ(result.LevelOffsets, reference.LevelOffsets);
        EXPECT_EQ(result.SplatRadii, reference.SplatRadii);

        const Runtime::ProgressivePoissonGpuParityDiagnostics parity =
            Runtime::CompareProgressivePoissonGpuOutputToReference(
                Runtime::ProgressivePoissonGpuParityDesc{
                    .Positions = points,
                    .GpuOrder = result.Order,
                    .GpuLevelOffsets = result.LevelOffsets,
                    .GpuSplatRadii = result.SplatRadii,
                    .Reference = Runtime::ProgressivePoissonGpuReferenceView{
                        .Order = reference.Order,
                        .LevelOffsets = reference.LevelOffsets,
                        .SplatRadii = reference.SplatRadii,
                        .LevelRadii = reference.Diag.LevelRadii,
                    },
                    .Dimension = referenceConfig.Dimension,
                });
        ASSERT_TRUE(parity.Succeeded()) << parity.Diagnostic;
        EXPECT_TRUE(parity.MatchesReference);

        const Graphics::GpuTransferDiagnostics diagnostics =
            transfer.GetDiagnostics();
        EXPECT_EQ(diagnostics.ReadbacksIssued, 3u);
        EXPECT_EQ(diagnostics.ReadbackBarriersEmitted, 3u);
        EXPECT_EQ(diagnostics.ReadbackBatchesIssued, 1u);
        EXPECT_EQ(diagnostics.ReadbackBatchesDelivered, 1u);
        EXPECT_EQ(diagnostics.ReadbackBatchesConsumed, 1u);
    }

    device.Shutdown();
}
