#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Runtime.KMeansGpuBackend;

#include "MockRHI.hpp"

namespace
{
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;
    namespace Tests = Extrinsic::Tests;

    [[nodiscard]] Runtime::KMeansGpuPlanDesc MakePlanDesc(
        const std::uint32_t points,
        const std::uint32_t clusters,
        const std::uint32_t iterations,
        const std::uint32_t groupSize = Runtime::kKMeansGpuGroupSize)
    {
        return Runtime::KMeansGpuPlanDesc{
            .PointCount = points,
            .ClusterCount = clusters,
            .MaxIterations = iterations,
            .GroupSize = groupSize,
            .ConvergenceTolerance = 1.0e-4f,
        };
    }

    [[nodiscard]] RHI::PipelineHandle ValidPipeline(
        const std::uint32_t index) noexcept
    {
        return RHI::PipelineHandle{index, 1u};
    }

    [[nodiscard]] RHI::BufferHandle ValidBuffer(
        const std::uint32_t index) noexcept
    {
        return RHI::BufferHandle{index, 1u};
    }

    [[nodiscard]] std::string ReadShaderSource(const char* relativePath)
    {
        const auto path =
            Extrinsic::Core::Filesystem::GetRoot() / "assets" / "shaders" / relativePath;
        std::ifstream input{path};
        if (!input)
            return {};

        std::ostringstream stream;
        stream << input.rdbuf();
        return stream.str();
    }

    [[nodiscard]] Runtime::KMeansGpuPipelineSet ValidKMeansPipelines() noexcept
    {
        return Runtime::KMeansGpuPipelineSet{
            .Reset = ValidPipeline(40u),
            .Assign = ValidPipeline(41u),
            .Update = ValidPipeline(42u),
        };
    }

    [[nodiscard]] const Runtime::KMeansGpuBufferSpan* FindSpan(
        const Runtime::KMeansGpuBufferLayout& layout,
        const Runtime::KMeansGpuBufferRole role)
    {
        for (const Runtime::KMeansGpuBufferSpan& span : layout.Spans)
        {
            if (span.Role == role)
                return &span;
        }
        return nullptr;
    }

    template <typename T>
    [[nodiscard]] std::vector<std::byte> BytesOf(std::span<const T> values)
    {
        const std::span<const std::byte> bytes = std::as_bytes(values);
        return std::vector<std::byte>{bytes.begin(), bytes.end()};
    }

    template <typename T>
    void WriteSpanBytes(std::vector<std::byte>& destination,
                        const Runtime::KMeansGpuBufferLayout& layout,
                        const Runtime::KMeansGpuBufferRole role,
                        std::span<const T> values)
    {
        const Runtime::KMeansGpuBufferSpan* span = FindSpan(layout, role);
        ASSERT_NE(span, nullptr);
        ASSERT_LE(values.size_bytes(), span->SizeBytes);
        ASSERT_LE(span->OffsetBytes + values.size_bytes(), destination.size());
        std::memcpy(destination.data() + static_cast<std::size_t>(span->OffsetBytes),
                    values.data(),
                    values.size_bytes());
    }

    template <typename T>
    [[nodiscard]] T ReadPayload(const std::vector<std::byte>& payload)
    {
        T value{};
        EXPECT_EQ(payload.size(), sizeof(T));
        if (payload.size() == sizeof(T))
        {
            std::memcpy(&value, payload.data(), sizeof(T));
        }
        return value;
    }

    template <typename T>
    [[nodiscard]] std::vector<T> ReadVectorPayload(
        const std::vector<std::byte>& payload)
    {
        std::vector<T> values(payload.size() / sizeof(T));
        EXPECT_EQ(payload.size(), values.size() * sizeof(T));
        if (!values.empty())
        {
            std::memcpy(values.data(), payload.data(), payload.size());
        }
        return values;
    }

    [[nodiscard]] const Tests::MockDevice::BufferWriteRecord* FindWrite(
        const Tests::MockDevice& device,
        const RHI::BufferHandle handle,
        const std::uint64_t offset)
    {
        const auto it = std::find_if(
            device.BufferWrites.begin(),
            device.BufferWrites.end(),
            [handle, offset](const Tests::MockDevice::BufferWriteRecord& write)
            {
                return write.Handle == handle && write.Offset == offset;
            });
        return it == device.BufferWrites.end() ? nullptr : &*it;
    }

    class MockReadbackTransferQueue final : public RHI::ITransferQueue
    {
    public:
        std::unordered_map<std::uint32_t, std::vector<std::byte>> BufferContents{};
        std::uint32_t DownloadCalls{0u};

        [[nodiscard]] RHI::TransferToken UploadBuffer(
            RHI::BufferHandle, const void*, std::uint64_t, std::uint64_t) override
        {
            return {};
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(
            RHI::BufferHandle, std::span<const std::byte>, std::uint64_t) override
        {
            return {};
        }

        [[nodiscard]] RHI::TransferToken UploadTexture(
            RHI::TextureHandle, const void*, std::uint64_t, std::uint32_t, std::uint32_t) override
        {
            return {};
        }

        [[nodiscard]] RHI::TransferToken UploadTextureFullChain(
            RHI::TextureHandle, std::span<const std::byte>) override
        {
            return {};
        }

        [[nodiscard]] bool IsComplete(RHI::TransferToken token) const override
        {
            return !token.IsValid();
        }

        void CollectCompleted() override
        {
            while (!m_PendingReadbacks.empty())
            {
                PendingReadback pending = std::move(m_PendingReadbacks.front());
                m_PendingReadbacks.pop_front();
                pending.Sink.Deliver(std::span<const std::byte>{pending.Bytes});
                m_CompletedReadback = std::max(m_CompletedReadback, pending.Token.Value);
            }
        }

        [[nodiscard]] RHI::ReadbackToken DownloadBuffer(
            RHI::BufferHandle src,
            std::uint64_t size,
            std::uint64_t offset,
            RHI::ReadbackSink sink) override
        {
            if (!src.IsValid() || size == 0u || !sink.IsValidForSize(size))
                return {};

            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            if (const auto it = BufferContents.find(src.Index); it != BufferContents.end())
            {
                const std::vector<std::byte>& contents = it->second;
                if (offset + size > contents.size())
                    return {};
                std::copy_n(contents.begin() + static_cast<std::ptrdiff_t>(offset),
                            bytes.size(),
                            bytes.begin());
            }

            const RHI::ReadbackToken token{++m_NextReadback};
            m_PendingReadbacks.push_back(PendingReadback{
                .Token = token,
                .Sink = std::move(sink),
                .Bytes = std::move(bytes),
            });
            ++DownloadCalls;
            return token;
        }

        [[nodiscard]] bool IsComplete(RHI::ReadbackToken token) const override
        {
            return !token.IsValid() || token.Value <= m_CompletedReadback;
        }

    private:
        struct PendingReadback
        {
            RHI::ReadbackToken Token{};
            RHI::ReadbackSink Sink{};
            std::vector<std::byte> Bytes{};
        };

        std::deque<PendingReadback> m_PendingReadbacks{};
        std::uint64_t m_NextReadback{0u};
        std::uint64_t m_CompletedReadback{0u};
    };
}

TEST(KMeansGpuBackend, StructLayoutsMatchShaderContract)
{
    EXPECT_EQ(sizeof(Runtime::KMeansGpuStateBufferRecord), 96u);
    EXPECT_EQ(sizeof(Runtime::KMeansGpuPassPushConstants), 32u);
}

TEST(KMeansGpuBackend, AssignShaderAvoidsOptionalAtomicFeatureRequirements)
{
    const std::string source = ReadShaderSource("kmeans_assign.comp");
    ASSERT_FALSE(source.empty());

    EXPECT_EQ(source.find("GL_EXT_shader_atomic_float"), std::string::npos);
    EXPECT_EQ(source.find("GL_EXT_shader_atomic_int64"), std::string::npos);
    EXPECT_EQ(source.find("atomicAdd(sSum"), std::string::npos);
    EXPECT_EQ(source.find("atomicAdd(FloatArray"), std::string::npos);
    EXPECT_EQ(source.find("atomicMax(reduction.PackedMaxDistIndex"), std::string::npos);
}

TEST(KMeansGpuBackend, BufferLayoutIsPackedAlignedAndSized)
{
    const Runtime::KMeansGpuBufferLayout layout =
        Runtime::ComputeKMeansGpuBufferLayout(MakePlanDesc(4u, 2u, 8u));

    ASSERT_TRUE(layout.IsValid());
    EXPECT_EQ(layout.PointCount, 4u);
    EXPECT_EQ(layout.ClusterCount, 2u);
    EXPECT_EQ(layout.StateBytes, sizeof(Runtime::KMeansGpuStateBufferRecord));

    // Readback sizes: labels/dist per point, centroids packed vec3 per cluster.
    EXPECT_EQ(layout.LabelsReadbackBytes, 4u * sizeof(std::uint32_t));
    EXPECT_EQ(layout.SquaredDistancesReadbackBytes, 4u * sizeof(float));
    EXPECT_EQ(layout.CentroidsReadbackBytes, 2u * 3u * sizeof(float));

    ASSERT_EQ(layout.Spans.size(), 12u);

    const Runtime::KMeansGpuBufferSpan* positionX =
        FindSpan(layout, Runtime::KMeansGpuBufferRole::PositionX);
    ASSERT_NE(positionX, nullptr);
    EXPECT_EQ(positionX->OffsetBytes, 0u);
    EXPECT_EQ(positionX->SizeBytes, 4u * sizeof(float));

    // Spans are 16-byte aligned, monotonic, and inside the work buffer.
    std::uint64_t previousEnd = 0u;
    for (const Runtime::KMeansGpuBufferSpan& span : layout.Spans)
    {
        EXPECT_EQ(span.OffsetBytes % 16u, 0u);
        EXPECT_GE(span.OffsetBytes, previousEnd);
        EXPECT_LE(span.OffsetBytes + span.SizeBytes, layout.WorkBufferBytes);
        previousEnd = span.OffsetBytes + span.SizeBytes;
    }
    EXPECT_GT(layout.WorkBufferBytes, 0u);

    const Runtime::KMeansGpuBufferSpan* reduction =
        FindSpan(layout, Runtime::KMeansGpuBufferRole::Reduction);
    ASSERT_NE(reduction, nullptr);
    EXPECT_EQ(reduction->SizeBytes, sizeof(Runtime::KMeansGpuReductionRecord));
}

TEST(KMeansGpuBackend, LayoutClampsClustersToPointCountAndRejectsEmpty)
{
    // k requested above n clamps to n.
    const Runtime::KMeansGpuBufferLayout clamped =
        Runtime::ComputeKMeansGpuBufferLayout(MakePlanDesc(3u, 8u, 4u));
    ASSERT_TRUE(clamped.IsValid());
    EXPECT_EQ(clamped.ClusterCount, 3u);

    // Empty input fails closed.
    const Runtime::KMeansGpuBufferLayout empty =
        Runtime::ComputeKMeansGpuBufferLayout(MakePlanDesc(0u, 4u, 4u));
    EXPECT_FALSE(empty.IsValid());
    EXPECT_EQ(empty.Status, Runtime::KMeansGpuStatus::InvalidInput);

    const Runtime::KMeansGpuBufferLayout zeroClusters =
        Runtime::ComputeKMeansGpuBufferLayout(MakePlanDesc(4u, 0u, 4u));
    EXPECT_FALSE(zeroClusters.IsValid());
    EXPECT_EQ(zeroClusters.Status, Runtime::KMeansGpuStatus::InvalidInput);
}

TEST(KMeansGpuBackend, DispatchPlanEmitsResetAssignUpdatePerIteration)
{
    const Runtime::KMeansGpuDispatchPlan plan =
        Runtime::ComputeKMeansGpuDispatchPlan(MakePlanDesc(4u, 2u, 8u));

    ASSERT_TRUE(plan.IsValid());
    EXPECT_EQ(plan.MaxIterations, 8u);
    ASSERT_EQ(plan.Dispatches.size(), 8u * 3u);

    for (std::uint32_t iteration = 0u; iteration < 8u; ++iteration)
    {
        const Runtime::KMeansGpuDispatchDesc& reset = plan.Dispatches[iteration * 3u + 0u];
        const Runtime::KMeansGpuDispatchDesc& assign = plan.Dispatches[iteration * 3u + 1u];
        const Runtime::KMeansGpuDispatchDesc& update = plan.Dispatches[iteration * 3u + 2u];

        EXPECT_EQ(reset.Kind, Runtime::KMeansGpuPassKind::Reset);
        EXPECT_EQ(assign.Kind, Runtime::KMeansGpuPassKind::Assign);
        EXPECT_EQ(update.Kind, Runtime::KMeansGpuPassKind::Update);
        EXPECT_EQ(reset.Iteration, iteration);
        EXPECT_EQ(assign.Iteration, iteration);
        EXPECT_EQ(update.Iteration, iteration);

        // ceil(k/G) and ceil(n/G) with the default 256 group size collapse to 1.
        EXPECT_EQ(reset.GroupCountX, 1u);
        EXPECT_EQ(assign.GroupCountX, 1u);
        EXPECT_EQ(update.GroupCountX, 1u);
        EXPECT_EQ(assign.ElementCount, 4u);
        EXPECT_EQ(update.ElementCount, 2u);
    }
}

TEST(KMeansGpuBackend, DispatchPlanGroupCountsUseCeilDivision)
{
    // Small group size exercises the ceil rounding: 5 points / 2 = 3 groups.
    const Runtime::KMeansGpuDispatchPlan plan =
        Runtime::ComputeKMeansGpuDispatchPlan(MakePlanDesc(5u, 3u, 1u, /*groupSize=*/2u));

    ASSERT_TRUE(plan.IsValid());
    ASSERT_EQ(plan.Dispatches.size(), 3u);
    EXPECT_EQ(plan.Dispatches[0].Kind, Runtime::KMeansGpuPassKind::Reset);
    EXPECT_EQ(plan.Dispatches[0].GroupCountX, 2u); // ceil(3/2)
    EXPECT_EQ(plan.Dispatches[1].Kind, Runtime::KMeansGpuPassKind::Assign);
    EXPECT_EQ(plan.Dispatches[1].GroupCountX, 3u); // ceil(5/2)
    EXPECT_EQ(plan.Dispatches[2].Kind, Runtime::KMeansGpuPassKind::Update);
    EXPECT_EQ(plan.Dispatches[2].GroupCountX, 2u); // ceil(3/2)
}

TEST(KMeansGpuBackend, ResolveFailsClosedOnNonOperationalDevice)
{
    Tests::MockDevice device;
    device.Operational = false;

    const Runtime::KMeansGpuResolveResult resolved =
        Runtime::ResolveKMeansGpuRequest(Runtime::KMeansGpuResolveDesc{
            .Device = &device, .Plan = MakePlanDesc(4u, 2u, 8u)});

    EXPECT_EQ(resolved.Status, Runtime::KMeansGpuStatus::DeviceUnavailable);
    EXPECT_FALSE(resolved.GpuExecutionAvailable);
    EXPECT_TRUE(resolved.CpuFallbackRecommended);
    // The plan is still computed so later slices can record against it.
    EXPECT_TRUE(resolved.Plan.IsValid());
}

TEST(KMeansGpuBackend, ResolveOnOperationalDeviceReportsExecutionAvailable)
{
    Tests::MockDevice device;
    device.Operational = true;

    const Runtime::KMeansGpuResolveResult resolved =
        Runtime::ResolveKMeansGpuRequest(Runtime::KMeansGpuResolveDesc{
            .Device = &device, .Plan = MakePlanDesc(4u, 2u, 8u)});

    EXPECT_EQ(resolved.Status, Runtime::KMeansGpuStatus::Success);
    EXPECT_TRUE(resolved.GpuExecutionAvailable);
    EXPECT_FALSE(resolved.CpuFallbackRecommended);
    EXPECT_TRUE(resolved.Plan.IsValid());
    EXPECT_EQ(resolved.Plan.Dispatches.size(), 8u * 3u);
}

TEST(KMeansGpuBackend, ResolveReportsMissingDeviceAndInvalidPlan)
{
    const Runtime::KMeansGpuResolveResult missing =
        Runtime::ResolveKMeansGpuRequest(Runtime::KMeansGpuResolveDesc{
            .Device = nullptr, .Plan = MakePlanDesc(4u, 2u, 8u)});
    EXPECT_EQ(missing.Status, Runtime::KMeansGpuStatus::MissingDevice);
    EXPECT_FALSE(missing.GpuExecutionAvailable);
    EXPECT_TRUE(missing.CpuFallbackRecommended);

    Tests::MockDevice device;
    device.Operational = true;
    const Runtime::KMeansGpuResolveResult invalid =
        Runtime::ResolveKMeansGpuRequest(Runtime::KMeansGpuResolveDesc{
            .Device = &device, .Plan = MakePlanDesc(0u, 2u, 8u)});
    EXPECT_EQ(invalid.Status, Runtime::KMeansGpuStatus::InvalidInput);
    EXPECT_FALSE(invalid.GpuExecutionAvailable);
    EXPECT_TRUE(invalid.CpuFallbackRecommended);
}

namespace
{
    struct RecordFixture
    {
        Tests::MockDevice Device{};
        Runtime::KMeansGpuPipelineSet Pipelines{};
        Runtime::KMeansGpuResourceSet Resources{};

        void CreateResources(const Runtime::KMeansGpuPlanDesc& plan)
        {
            const Runtime::KMeansGpuBufferLayout layout =
                Runtime::ComputeKMeansGpuBufferLayout(plan);
            Resources.State = Device.CreateBuffer(Runtime::BuildKMeansGpuStateBufferDesc());
            Resources.Work = Device.CreateBuffer(Runtime::BuildKMeansGpuWorkBufferDesc(layout));
            Resources.LabelsReadback = Device.CreateBuffer(
                Runtime::BuildKMeansGpuReadbackBufferDesc(layout.LabelsReadbackBytes));
            Resources.SquaredDistancesReadback = Device.CreateBuffer(
                Runtime::BuildKMeansGpuReadbackBufferDesc(layout.SquaredDistancesReadbackBytes));
            Resources.CentroidsReadback = Device.CreateBuffer(
                Runtime::BuildKMeansGpuReadbackBufferDesc(layout.CentroidsReadbackBytes));
            Pipelines.Reset = Device.CreatePipeline(Runtime::BuildKMeansResetPipelineDesc());
            Pipelines.Assign = Device.CreatePipeline(Runtime::BuildKMeansAssignPipelineDesc());
            Pipelines.Update = Device.CreatePipeline(Runtime::BuildKMeansUpdatePipelineDesc());
        }
    };
}

TEST(KMeansGpuBackend, RecordFailsClosedOnMissingDeviceAndCommandContext)
{
    Tests::MockDevice device;
    device.Operational = true;

    const Runtime::KMeansGpuRecordResult missingDevice =
        Runtime::RecordKMeansGpuPasses(Runtime::KMeansGpuRecordDesc{
            .Device = nullptr, .CommandContext = &device.CommandContext,
            .Plan = MakePlanDesc(4u, 2u, 4u)});
    EXPECT_EQ(missingDevice.Status, Runtime::KMeansGpuStatus::MissingDevice);
    EXPECT_FALSE(missingDevice.Recorded);
    EXPECT_TRUE(missingDevice.CpuFallbackRecommended);

    const Runtime::KMeansGpuRecordResult missingCmd =
        Runtime::RecordKMeansGpuPasses(Runtime::KMeansGpuRecordDesc{
            .Device = &device, .CommandContext = nullptr,
            .Plan = MakePlanDesc(4u, 2u, 4u)});
    EXPECT_EQ(missingCmd.Status, Runtime::KMeansGpuStatus::InvalidInput);
    EXPECT_FALSE(missingCmd.Recorded);
}

TEST(KMeansGpuBackend, RecordFailsClosedOnNonOperationalDevice)
{
    Tests::MockDevice device;
    device.Operational = false;

    const Runtime::KMeansGpuRecordResult result =
        Runtime::RecordKMeansGpuPasses(Runtime::KMeansGpuRecordDesc{
            .Device = &device, .CommandContext = &device.CommandContext,
            .Plan = MakePlanDesc(4u, 2u, 4u)});

    EXPECT_EQ(result.Status, Runtime::KMeansGpuStatus::DeviceUnavailable);
    EXPECT_FALSE(result.Recorded);
    EXPECT_TRUE(result.CpuFallbackRecommended);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 0);
}

TEST(KMeansGpuBackend, RecordFailsClosedOnInvalidResources)
{
    Tests::MockDevice device;
    device.Operational = true;

    // Valid plan + operational device but default (invalid) resource/pipeline
    // handles must fail closed without recording.
    const Runtime::KMeansGpuRecordResult result =
        Runtime::RecordKMeansGpuPasses(Runtime::KMeansGpuRecordDesc{
            .Device = &device, .CommandContext = &device.CommandContext,
            .Plan = MakePlanDesc(4u, 2u, 4u)});

    EXPECT_EQ(result.Status, Runtime::KMeansGpuStatus::InvalidGpuResource);
    EXPECT_FALSE(result.Recorded);
    EXPECT_TRUE(result.CpuFallbackRecommended);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 0);
}

TEST(KMeansGpuBackend, RecordEmitsResetAssignUpdatePerIterationWithBarriers)
{
    RecordFixture fixture;
    fixture.Device.Operational = true;
    const Runtime::KMeansGpuPlanDesc plan = MakePlanDesc(4u, 2u, 3u);
    fixture.CreateResources(plan);

    const Runtime::KMeansGpuRecordResult result =
        Runtime::RecordKMeansGpuPasses(Runtime::KMeansGpuRecordDesc{
            .Device = &fixture.Device,
            .CommandContext = &fixture.Device.CommandContext,
            .Pipelines = fixture.Pipelines,
            .Resources = fixture.Resources,
            .Plan = plan});

    ASSERT_TRUE(result.Succeeded());
    EXPECT_TRUE(result.Recorded);
    EXPECT_TRUE(result.StateRecordUploaded);
    EXPECT_FALSE(result.CpuFallbackRecommended);
    EXPECT_EQ(result.MethodDispatchCount, 3u * 3u);

    const Tests::MockCommandContext& cmd = fixture.Device.CommandContext;
    EXPECT_EQ(cmd.BindPipelineCalls, 9);
    EXPECT_EQ(cmd.PushConstantsCalls, 9);
    EXPECT_EQ(cmd.DispatchCalls, 9);
    for (const auto& record : cmd.DispatchRecords)
    {
        EXPECT_EQ(record.X, 1u); // ceil(4/256) and ceil(2/256) both collapse to 1
        EXPECT_EQ(record.Y, 1u);
        EXPECT_EQ(record.Z, 1u);
    }

    // One State visibility barrier + one Work barrier per dispatch.
    ASSERT_EQ(cmd.BufferBarrierCalls.size(), 1u + 9u);
    EXPECT_EQ(cmd.BufferBarrierCalls[0].Buffer, fixture.Resources.State);
    EXPECT_EQ(cmd.BufferBarrierCalls[0].Before, RHI::MemoryAccess::TransferWrite);
    EXPECT_EQ(cmd.BufferBarrierCalls[0].After, RHI::MemoryAccess::ShaderRead);
    for (std::size_t i = 1; i < cmd.BufferBarrierCalls.size(); ++i)
    {
        EXPECT_EQ(cmd.BufferBarrierCalls[i].Buffer, fixture.Resources.Work);
        EXPECT_EQ(cmd.BufferBarrierCalls[i].Before, RHI::MemoryAccess::ShaderWrite);
    }

    // The State BDA table was uploaded once at its full size.
    int stateWrites = 0;
    for (const auto& write : fixture.Device.BufferWrites)
    {
        if (write.Handle == fixture.Resources.State)
        {
            ++stateWrites;
            EXPECT_EQ(write.Data.size(), sizeof(Runtime::KMeansGpuStateBufferRecord));
        }
    }
    EXPECT_EQ(stateWrites, 1);
}

TEST(KMeansGpuBackend, ExecutionAllocatesUploadsRecordsAndPublishesReadbackResources)
{
    Tests::MockDevice device;
    device.Operational = true;
    RHI::BufferManager buffers{device};
    Runtime::KMeansGpuResourceCache cache{buffers};
    MockReadbackTransferQueue queue;
    Extrinsic::Graphics::GpuTransfer transfer{queue};
    Runtime::KMeansGpuAsyncReadbacks readbacks{transfer};

    const Runtime::KMeansGpuPlanDesc plan = MakePlanDesc(3u, 2u, 2u);
    const std::array<glm::vec3, 3> points{{
        glm::vec3{1.0f, 2.0f, 3.0f},
        glm::vec3{4.0f, 5.0f, 6.0f},
        glm::vec3{7.0f, 8.0f, 9.0f},
    }};
    const std::array<glm::vec3, 2> seeds{{
        glm::vec3{1.0f, 2.0f, 3.0f},
        glm::vec3{7.0f, 8.0f, 9.0f},
    }};

    const Runtime::KMeansGpuExecutionResult result =
        Runtime::RecordKMeansGpuExecution(Runtime::KMeansGpuExecutionDesc{
            .Device = &device,
            .CommandContext = &device.CommandContext,
            .ResourceCache = &cache,
            .Pipelines = ValidKMeansPipelines(),
            .Points = points,
            .SeedCentroids = seeds,
            .Plan = plan,
        });

    ASSERT_TRUE(result.Succeeded());
    ASSERT_NE(result.Resources, nullptr);
    EXPECT_TRUE(result.Recorded);
    EXPECT_TRUE(result.UploadedInputs);
    EXPECT_TRUE(result.ReadbackResourcesReady);
    EXPECT_FALSE(result.CpuFallbackRecommended);
    EXPECT_EQ(result.UploadWriteCount, 6u);
    EXPECT_EQ(cache.AllocationCount(), 1u);
    EXPECT_EQ(device.CreateBufferCount, 2);
    EXPECT_EQ(queue.DownloadCalls, 0u);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 6);

    const Runtime::KMeansGpuBufferLayout& layout = result.Resources->Layout;
    const RHI::BufferHandle work = result.Resources->Resources.Work;

    const Runtime::KMeansGpuBufferSpan* xSpan =
        FindSpan(layout, Runtime::KMeansGpuBufferRole::PositionX);
    ASSERT_NE(xSpan, nullptr);
    const Tests::MockDevice::BufferWriteRecord* xWrite =
        FindWrite(device, work, xSpan->OffsetBytes);
    ASSERT_NE(xWrite, nullptr);
    EXPECT_EQ((std::vector<float>{1.0f, 4.0f, 7.0f}),
              ReadVectorPayload<float>(xWrite->Data));

    const Runtime::KMeansGpuBufferSpan* centroidSpan =
        FindSpan(layout, Runtime::KMeansGpuBufferRole::Centroids);
    ASSERT_NE(centroidSpan, nullptr);
    const Tests::MockDevice::BufferWriteRecord* centroidWrite =
        FindWrite(device, work, centroidSpan->OffsetBytes);
    ASSERT_NE(centroidWrite, nullptr);
    EXPECT_EQ((std::vector<float>{1.0f, 2.0f, 3.0f, 7.0f, 8.0f, 9.0f}),
              ReadVectorPayload<float>(centroidWrite->Data));

    const Tests::MockDevice::BufferWriteRecord* stateWrite =
        FindWrite(device, result.Resources->Resources.State, 0u);
    ASSERT_NE(stateWrite, nullptr);
    const Runtime::KMeansGpuStateBufferRecord state =
        ReadPayload<Runtime::KMeansGpuStateBufferRecord>(stateWrite->Data);
    EXPECT_EQ(state.PositionXBDA,
              device.GetBufferDeviceAddress(work) + xSpan->OffsetBytes);
    EXPECT_EQ(state.CentroidsBDA,
              device.GetBufferDeviceAddress(work) + centroidSpan->OffsetBytes);

    ASSERT_TRUE(readbacks.Enqueue(device.CommandContext, *result.Resources));
    EXPECT_EQ(queue.DownloadCalls, 3u);

    std::uint32_t transferReadBarriers = 0u;
    for (const auto& barrier : device.CommandContext.BufferBarrierCalls)
    {
        if (barrier.Buffer == work && barrier.After == RHI::MemoryAccess::TransferRead)
            ++transferReadBarriers;
    }
    EXPECT_EQ(transferReadBarriers, 3u);

    queue.CollectCompleted();
    transfer.DrainCompleted(device.CommandContext);
    ASSERT_TRUE(readbacks.Poll());
    readbacks.Reset();

    const Runtime::KMeansGpuExecutionResult reused =
        Runtime::RecordKMeansGpuExecution(Runtime::KMeansGpuExecutionDesc{
            .Device = &device,
            .CommandContext = &device.CommandContext,
            .ResourceCache = &cache,
            .Pipelines = ValidKMeansPipelines(),
            .Points = points,
            .SeedCentroids = seeds,
            .Plan = plan,
        });
    ASSERT_TRUE(reused.Succeeded());
    EXPECT_EQ(cache.AllocationCount(), 1u);
    EXPECT_EQ(device.CreateBufferCount, 2);
}

TEST(KMeansGpuBackend, AsyncReadbacksCollectLabelsDistancesAndCentroids)
{
    const Runtime::KMeansGpuDispatchPlan plan =
        Runtime::ComputeKMeansGpuDispatchPlan(MakePlanDesc(3u, 2u, 1u));
    ASSERT_TRUE(plan.IsValid());

    Runtime::KMeansGpuExecutionResources resources{};
    resources.Layout = plan.Layout;
    resources.Key = Runtime::KMeansGpuResourceCacheKey{
        .PointCount = plan.PointCount,
        .ClusterCount = plan.ClusterCount,
        .WorkBufferBytes = plan.Layout.WorkBufferBytes,
        .StateBytes = plan.Layout.StateBytes,
    };
    resources.Resources.State = ValidBuffer(100u);
    resources.Resources.Work = ValidBuffer(101u);

    std::vector<std::byte> work(static_cast<std::size_t>(plan.Layout.WorkBufferBytes));
    const std::array<std::uint32_t, 3> labels{{0u, 1u, 1u}};
    const std::array<float, 3> distances{{0.25f, 1.5f, 0.75f}};
    const std::array<float, 6> centroids{{1.0f, 2.0f, 3.0f, 7.0f, 8.0f, 9.0f}};
    WriteSpanBytes(work, plan.Layout, Runtime::KMeansGpuBufferRole::Labels,
                   std::span<const std::uint32_t>{labels});
    WriteSpanBytes(work, plan.Layout, Runtime::KMeansGpuBufferRole::SquaredDistances,
                   std::span<const float>{distances});
    WriteSpanBytes(work, plan.Layout, Runtime::KMeansGpuBufferRole::Centroids,
                   std::span<const float>{centroids});

    MockReadbackTransferQueue queue;
    queue.BufferContents[resources.Resources.Work.Index] = std::move(work);
    Extrinsic::Graphics::GpuTransfer transfer{queue};
    Runtime::KMeansGpuAsyncReadbacks readbacks{transfer};
    Tests::MockDevice device;

    ASSERT_TRUE(readbacks.Enqueue(device.CommandContext, resources));
    EXPECT_FALSE(readbacks.Poll());

    queue.CollectCompleted();
    transfer.DrainCompleted(device.CommandContext);
    ASSERT_TRUE(readbacks.Poll());

    const Runtime::KMeansGpuReadbackResult result = readbacks.Collect(plan);
    ASSERT_TRUE(result.Succeeded()) << result.Diagnostic;
    EXPECT_TRUE(result.Read);
    EXPECT_TRUE(result.StructurallyValid);
    EXPECT_EQ((std::vector<std::uint32_t>{0u, 1u, 1u}), result.Labels);
    EXPECT_EQ((std::vector<float>{0.25f, 1.5f, 0.75f}), result.SquaredDistances);
    ASSERT_EQ(result.Centroids.size(), 2u);
    EXPECT_EQ(result.Centroids[0], (glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(result.Centroids[1], (glm::vec3{7.0f, 8.0f, 9.0f}));
    EXPECT_FLOAT_EQ(result.Inertia, 2.5f);
    EXPECT_EQ(result.MaxDistanceIndex, 1u);
}
