#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <thread>
#include <vector>

#include <glm/glm.hpp>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.GpuReadbackJob;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.StreamingExecutor;
import Geometry.Properties;

namespace
{
    namespace Core = Extrinsic::Core;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;

    template <class T>
    [[nodiscard]] std::span<const std::byte> BytesOf(std::span<const T> values) noexcept
    {
        return std::as_bytes(values);
    }

    [[nodiscard]] bool DrainTransferTokenUntilComplete(
        RHI::ITransferQueue& queue,
        const RHI::TransferToken token)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (std::chrono::steady_clock::now() < deadline)
        {
            queue.CollectCompleted();
            if (queue.IsComplete(token))
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        queue.CollectCompleted();
        return queue.IsComplete(token);
    }

    [[nodiscard]] bool DrainReadbackTokenUntilComplete(
        RHI::ITransferQueue& queue,
        const RHI::ReadbackToken token)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (std::chrono::steady_clock::now() < deadline)
        {
            queue.CollectCompleted();
            if (queue.IsComplete(token))
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        queue.CollectCompleted();
        return queue.IsComplete(token);
    }

    class RecordingCommandContext final : public RHI::ICommandContext
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

        void BufferBarrier(RHI::BufferHandle buffer,
                           RHI::MemoryAccess before,
                           RHI::MemoryAccess after) override
        {
            Barriers.push_back(BufferBarrierEvent{
                .Buffer = buffer,
                .Before = before,
                .After = after,
            });
        }

        void SubmitBarriers(const RHI::BarrierBatchDesc& batch) override
        {
            for (const RHI::BufferBarrierDesc& barrier : batch.BufferBarriers)
            {
                BufferBarrier(barrier.Buffer, barrier.BeforeAccess, barrier.AfterAccess);
            }
        }

        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}

        struct BufferBarrierEvent
        {
            RHI::BufferHandle Buffer{};
            RHI::MemoryAccess Before{RHI::MemoryAccess::None};
            RHI::MemoryAccess After{RHI::MemoryAccess::None};
        };

        std::vector<BufferBarrierEvent> Barriers{};
    };

    [[nodiscard]] Runtime::DerivedJobKey MakeKey(
        const std::uint32_t entityId,
        const Runtime::ProgressiveSlotSemantic semantic)
    {
        return Runtime::DerivedJobKey{
            .EntityId = entityId,
            .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
            .OutputSemantic = semantic,
            .EntityGeneration = 1u,
            .GeometryGeneration = 1u,
            .SourcePropertyGeneration = 1u,
            .BindingGeneration = 1u,
            .OutputName = std::string{Runtime::ToString(semantic)},
        };
    }

    [[nodiscard]] bool DrainRuntimeReadbackUntilReady(
        RHI::ITransferQueue& queue,
        Graphics::GpuTransfer& transfer,
        RecordingCommandContext& commandContext,
        Runtime::DerivedJobRegistry& jobs,
        const Runtime::DerivedJobHandle readback)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (std::chrono::steady_clock::now() < deadline)
        {
            queue.CollectCompleted();
            transfer.DrainCompleted(commandContext);
            jobs.DrainReadbacks();

            const std::optional<Runtime::DerivedJobSnapshot> snapshot = jobs.Snapshot(readback);
            if (snapshot.has_value() &&
                snapshot->ExecutionState == Runtime::StreamingTaskState::WaitingForMainThreadApply)
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        queue.CollectCompleted();
        transfer.DrainCompleted(commandContext);
        jobs.DrainReadbacks();
        const std::optional<Runtime::DerivedJobSnapshot> snapshot = jobs.Snapshot(readback);
        return snapshot.has_value() &&
               snapshot->ExecutionState == Runtime::StreamingTaskState::WaitingForMainThreadApply;
    }
}

TEST(GpuReadbackJobGpuSmoke, VulkanTransferReadbackWritesPropertyAndFollowUpUploadsDerivedColor)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        GTEST_SKIP() << "GLFW could not initialize in this environment; gpu;vulkan readback-job smoke is opt-in.";
    }

    Extrinsic::Core::Config::WindowConfig windowConfig{};
    windowConfig.Title = "Intrinsic runtime GPU readback job smoke";
    windowConfig.Width = 64;
    windowConfig.Height = 64;
    windowConfig.Resizable = false;

    std::unique_ptr<Extrinsic::Platform::IWindow> window =
        Extrinsic::Platform::Backends::Glfw::CreateWindow(windowConfig);
    ASSERT_NE(window, nullptr);
    if (window->GetNativeHandle() == nullptr)
    {
        GTEST_SKIP() << "GLFW window creation failed; gpu;vulkan readback-job smoke requires a native surface.";
    }

    Extrinsic::Core::Config::RenderConfig renderConfig{};
    renderConfig.EnablePromotedVulkanDevice = true;
    renderConfig.EnableValidation = false;

    std::unique_ptr<RHI::IDevice> device =
        Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);

    device->Initialize(RHI::MakeDeviceCreateDesc(
        renderConfig,
        window->GetFramebufferExtent(),
        window->GetNativeHandle()));

    const auto bootstrap = Extrinsic::Backends::Vulkan::GetVulkanBootstrapDiagnosticsSnapshot();
    const auto services = Extrinsic::Backends::Vulkan::GetVulkanServiceDiagnosticsSnapshot();
    if (bootstrap.Status != Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::RegisteredSwapchainImages ||
        services.Status != Extrinsic::Backends::Vulkan::VulkanServiceBootstrapStatus::Ready ||
        !services.PublicTransferQueueExposed)
    {
        device->Shutdown();
        GTEST_SKIP() << "Promoted Vulkan transfer queue was not service-ready on this host.";
    }

    constexpr std::array<float, 3> readbackValues{1.0f, 2.0f, 3.0f};
    const RHI::BufferDesc sourceDesc{
        .SizeBytes = readbackValues.size() * sizeof(float),
        .Usage = RHI::BufferUsage::Storage |
                 RHI::BufferUsage::TransferDst |
                 RHI::BufferUsage::TransferSrc,
        .HostVisible = false,
        .DebugName = "GpuReadbackJobGpuSmoke.Source",
    };
    const RHI::BufferHandle source = device->CreateBuffer(sourceDesc);
    ASSERT_TRUE(source.IsValid());

    const RHI::BufferDesc colorDesc{
        .SizeBytes = readbackValues.size() * sizeof(glm::vec4),
        .Usage = RHI::BufferUsage::Storage |
                 RHI::BufferUsage::TransferDst |
                 RHI::BufferUsage::TransferSrc,
        .HostVisible = false,
        .DebugName = "GpuReadbackJobGpuSmoke.DerivedColor",
    };
    const RHI::BufferHandle colorBuffer = device->CreateBuffer(colorDesc);
    ASSERT_TRUE(colorBuffer.IsValid());

    RHI::ITransferQueue& queue = device->GetTransferQueue();
    Graphics::GpuTransfer transfer{queue};
    RecordingCommandContext commandContext{};

    const RHI::TransferToken sourceUpload =
        queue.UploadBuffer(source, BytesOf<float>(std::span<const float>{readbackValues}), 0u);
    ASSERT_TRUE(sourceUpload.IsValid());
    ASSERT_TRUE(DrainTransferTokenUntilComplete(queue, sourceUpload))
        << "timed out uploading source data for runtime readback smoke";

    Geometry::PropertySet properties;
    properties.Resize(static_cast<std::uint32_t>(readbackValues.size()));
    ASSERT_TRUE(properties.Add<float>("v:height", 0.0f));
    ASSERT_TRUE(properties.Add<glm::vec4>("v:readback_color", glm::vec4{0.0f}));

    Runtime::StreamingExecutor executor{};
    Runtime::DerivedJobRegistry jobs{executor};

    const Runtime::DerivedJobHandle readback =
        Runtime::SubmitGpuReadbackJob(
            jobs,
            Runtime::GpuReadbackJobDesc{
                .Key = MakeKey(42u, Runtime::ProgressiveSlotSemantic::ScalarField),
                .Name = "vulkan readback to scalar property",
                .Transfer = &transfer,
                .CommandContext = &commandContext,
                .Source = source,
                .SourceDesc = sourceDesc,
                .SourceAccess = RHI::MemoryAccess::ShaderRead,
                .Binding = Runtime::GpuReadbackPropertyBinding{
                    .TargetProperties = &properties.Registry(),
                    .TargetProperty = "v:height",
                    .TargetType = Runtime::GpuReadbackPropertyType::Float32,
                    .SourceRange = RHI::BufferRange{
                        .OffsetBytes = 0u,
                        .SizeBytes = readbackValues.size() * sizeof(float),
                    },
                },
            });
    ASSERT_TRUE(readback.IsValid());

    Graphics::GpuTransferUploadTicket colorUpload{};
    Runtime::DerivedJobDesc followUp{
        .Key = MakeKey(42u, Runtime::ProgressiveSlotSemantic::Albedo),
        .Name = "derive color from readback property",
        .Execute = []() -> Runtime::DerivedJobWorkerResult
        {
            return Runtime::DerivedJobOutput{.PayloadToken = 400u};
        },
        .ApplyOnMainThread =
            [&properties, &transfer, colorBuffer, colorDesc, &colorUpload](
                Runtime::DerivedJobApplyContext&) -> Core::Result
        {
            auto heights = properties.Registry().Get<float>("v:height");
            auto colors = properties.Registry().Get<glm::vec4>("v:readback_color");
            if (!heights.has_value() || !colors.has_value())
            {
                return Core::Err(Core::ErrorCode::ResourceNotFound);
            }
            if (heights->Span().size() != colors->Span().size())
            {
                return Core::Err(Core::ErrorCode::TypeMismatch);
            }

            for (std::size_t index = 0; index < heights->Span().size(); ++index)
            {
                const float value = heights->Span()[index];
                colors->Span()[index] = glm::vec4{
                    value / 4.0f,
                    0.25f,
                    1.0f - (value / 4.0f),
                    1.0f,
                };
            }

            colorUpload = transfer.ScheduleUpload(Graphics::GpuTransferUploadDesc{
                .Destination = colorBuffer,
                .DestinationDesc = colorDesc,
                .Source = BytesOf<glm::vec4>(colors->Span()),
                .DestinationOffsetBytes = 0u,
            });
            return colorUpload.IsValid()
                ? Core::Ok()
                : Core::Err(Core::ErrorCode::InvalidState);
        },
    };

    const Runtime::DerivedJobHandle colorJob =
        jobs.SubmitFollowUp(readback, std::move(followUp), "readback scalar property ready");
    ASSERT_TRUE(colorJob.IsValid());

    executor.PumpBackground(1);
    executor.DrainCompletions();
    {
        const std::optional<Runtime::DerivedJobSnapshot> snapshot = jobs.Snapshot(readback);
        ASSERT_TRUE(snapshot.has_value());
        EXPECT_EQ(snapshot->ExecutionState, Runtime::StreamingTaskState::WaitingForReadback);
    }

    ASSERT_TRUE(DrainRuntimeReadbackUntilReady(queue, transfer, commandContext, jobs, readback))
        << "timed out waiting for readback job delivery";
    jobs.ApplyMainThreadResults();
    EXPECT_EQ(jobs.GetStatus(readback), Runtime::DerivedJobStatus::Complete);

    auto heights = properties.Registry().Get<float>("v:height");
    ASSERT_TRUE(heights.has_value());
    ASSERT_EQ(heights->Span().size(), readbackValues.size());
    for (std::size_t index = 0; index < readbackValues.size(); ++index)
    {
        EXPECT_FLOAT_EQ(heights->Span()[index], readbackValues[index]);
    }

    executor.PumpBackground(1);
    executor.DrainCompletions();
    jobs.ApplyMainThreadResults();
    EXPECT_EQ(jobs.GetStatus(colorJob), Runtime::DerivedJobStatus::Complete);
    ASSERT_TRUE(colorUpload.IsValid());
    ASSERT_TRUE(DrainTransferTokenUntilComplete(queue, colorUpload.Token))
        << "timed out uploading derived color property";
    transfer.DrainCompleted(commandContext);
    EXPECT_TRUE(transfer.IsReady(colorUpload));

    const auto colors = properties.Registry().Get<glm::vec4>("v:readback_color");
    ASSERT_TRUE(colors.has_value());
    std::vector<std::byte> expectedColorBytes(
        readbackValues.size() * sizeof(glm::vec4));
    std::memcpy(expectedColorBytes.data(),
                colors->Span().data(),
                expectedColorBytes.size());

    std::vector<std::byte> actualColorBytes(expectedColorBytes.size());
    const RHI::ReadbackToken colorReadback =
        queue.DownloadBuffer(colorBuffer,
                             actualColorBytes.size(),
                             0u,
                             RHI::ReadbackSink::CopyTo(
                                 std::span<std::byte>{actualColorBytes}));
    ASSERT_TRUE(colorReadback.IsValid());
    ASSERT_TRUE(DrainReadbackTokenUntilComplete(queue, colorReadback))
        << "timed out reading back derived color upload";
    EXPECT_EQ(actualColorBytes, expectedColorBytes);

    const Runtime::DerivedJobQueueSnapshot jobSnapshot = jobs.SnapshotAll();
    EXPECT_EQ(jobSnapshot.Readbacks.Issued, 1u);
    EXPECT_EQ(jobSnapshot.Readbacks.Completed, 1u);
    EXPECT_EQ(jobSnapshot.Readbacks.Waiting, 0u);
    EXPECT_EQ(jobSnapshot.Readbacks.Failed, 0u);

    device->DestroyBuffer(colorBuffer);
    device->DestroyBuffer(source);
    device->Shutdown();
}
