#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.Runtime.Engine;

namespace
{
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;

    class ExitAfterFramesApp final : public Extrinsic::Runtime::IApplication
    {
    public:
        explicit ExitAfterFramesApp(const std::uint32_t targetFrames) noexcept
            : m_TargetFrames(targetFrames)
        {
        }

        void OnInitialize(Extrinsic::Runtime::Engine&) override {}
        void OnSimTick(Extrinsic::Runtime::Engine&, double) override {}

        void OnVariableTick(Extrinsic::Runtime::Engine& engine, double, double) override
        {
            ++m_Frames;
            if (m_Frames >= m_TargetFrames)
            {
                engine.RequestExit();
            }
        }

        void OnShutdown(Extrinsic::Runtime::Engine&) override {}

    private:
        std::uint32_t m_TargetFrames = 1u;
        std::uint32_t m_Frames = 0u;
    };

    struct SmokeBootstrap
    {
        std::unique_ptr<Extrinsic::Runtime::Engine> EnginePtr{};
        bool Skipped = false;
        std::string SkipReason{};
    };

    class EngineShutdownGuard
    {
    public:
        explicit EngineShutdownGuard(Extrinsic::Runtime::Engine& engine) noexcept
            : m_Engine(&engine)
        {
        }

        ~EngineShutdownGuard()
        {
            if (m_Engine != nullptr)
            {
                m_Engine->Shutdown();
            }
        }

        EngineShutdownGuard(const EngineShutdownGuard&) = delete;
        EngineShutdownGuard& operator=(const EngineShutdownGuard&) = delete;

    private:
        Extrinsic::Runtime::Engine* m_Engine = nullptr;
    };

    [[nodiscard]] SmokeBootstrap BootstrapEngine()
    {
        if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        {
            return SmokeBootstrap{
                .Skipped = true,
                .SkipReason = "GLFW could not initialize in this environment; gpu;vulkan compute primitive smoke is opt-in.",
            };
        }

        auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
        config.Window.Title = "Intrinsic compute primitives gpu;vulkan smoke";
        config.Window.Width = 96u;
        config.Window.Height = 96u;
        config.Window.Resizable = false;
        config.Render.EnableValidation = false;
        config.Render.EnableVSync = false;

        auto engine = std::make_unique<Extrinsic::Runtime::Engine>(
            config,
            std::make_unique<ExitAfterFramesApp>(4u));
        engine->Initialize();

        const auto initInputs =
            Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs(
                &engine->GetDevice());
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

    void DestroyBufferIfValid(RHI::IDevice& device, RHI::BufferHandle& handle) noexcept
    {
        if (handle.IsValid())
        {
            device.DestroyBuffer(handle);
            handle = {};
        }
    }

    void DestroyPipelineIfValid(RHI::IDevice& device, RHI::PipelineHandle& handle) noexcept
    {
        if (handle.IsValid())
        {
            device.DestroyPipeline(handle);
            handle = {};
        }
    }

    [[nodiscard]] RHI::BufferHandle CreateU32StorageBuffer(RHI::IDevice& device,
                                                           const std::uint32_t count,
                                                           const char* debugName)
    {
        const std::uint64_t sizeBytes =
            static_cast<std::uint64_t>(std::max(count, 1u)) * sizeof(std::uint32_t);
        return device.CreateBuffer(RHI::BufferDesc{
            .SizeBytes = sizeBytes,
            .Usage = RHI::BufferUsage::Storage |
                     RHI::BufferUsage::TransferSrc |
                     RHI::BufferUsage::TransferDst,
            .HostVisible = true,
            .DebugName = debugName,
        });
    }

    [[nodiscard]] bool BeginComputeFrame(RHI::IDevice& device,
                                         RHI::FrameHandle& frame,
                                         RHI::ICommandContext*& cmd,
                                         const std::string_view caseName)
    {
        if (!device.BeginFrame(frame))
        {
            ADD_FAILURE() << caseName << ": BeginFrame failed";
            return false;
        }

        cmd = &device.GetGraphicsContext(frame.FrameIndex);
        cmd->Begin();
        return true;
    }

    void EndComputeFrame(RHI::IDevice& device,
                         RHI::FrameHandle frame,
                         RHI::ICommandContext* cmd) noexcept
    {
        if (cmd != nullptr)
        {
            cmd->End();
        }
        device.EndFrame(frame);
        device.Present(frame);
    }

    [[nodiscard]] bool RunPrefixCase(RHI::IDevice& device,
                                     RHI::BufferManager& buffers,
                                     const Graphics::ParallelPrimitivePipelineSet pipelines,
                                     const std::vector<std::uint32_t>& input,
                                     const Graphics::PrefixScanMode mode,
                                     const std::string_view caseName)
    {
        std::vector<std::uint32_t> expected(input.size(), 0u);
        const Graphics::ParallelPrimitiveCpuResult cpu =
            Graphics::ComputePrefixScanCpu(input, expected, mode);
        if (!cpu.Succeeded())
        {
            ADD_FAILURE() << caseName << ": CPU reference failed";
            return false;
        }

        RHI::BufferHandle inputBuffer =
            CreateU32StorageBuffer(device, static_cast<std::uint32_t>(input.size()),
                                   "ComputePrimitiveSmoke.PrefixInput");
        RHI::BufferHandle outputBuffer =
            CreateU32StorageBuffer(device, static_cast<std::uint32_t>(input.size()),
                                   "ComputePrimitiveSmoke.PrefixOutput");
        if (!inputBuffer.IsValid() || !outputBuffer.IsValid())
        {
            ADD_FAILURE() << caseName << ": buffer allocation failed";
            DestroyBufferIfValid(device, outputBuffer);
            DestroyBufferIfValid(device, inputBuffer);
            return false;
        }

        std::vector<std::uint32_t> output(std::max<std::size_t>(input.size(), 1u),
                                          0xdeadbeefu);
        if (!input.empty())
        {
            device.WriteBuffer(inputBuffer,
                               input.data(),
                               input.size() * sizeof(std::uint32_t),
                               0u);
        }
        device.WriteBuffer(outputBuffer,
                           output.data(),
                           output.size() * sizeof(std::uint32_t),
                           0u);

        RHI::FrameHandle frame{};
        RHI::ICommandContext* cmd = nullptr;
        if (!BeginComputeFrame(device, frame, cmd, caseName))
        {
            DestroyBufferIfValid(device, outputBuffer);
            DestroyBufferIfValid(device, inputBuffer);
            return false;
        }

        if (!input.empty())
        {
            cmd->BufferBarrier(inputBuffer,
                               RHI::MemoryAccess::HostWrite,
                               RHI::MemoryAccess::ShaderRead);
            cmd->BufferBarrier(outputBuffer,
                               RHI::MemoryAccess::HostWrite,
                               RHI::MemoryAccess::ShaderWrite);
        }

        const Graphics::GpuParallelPrimitiveRecordResult gpu =
            Graphics::RecordGpuPrefixScan(Graphics::GpuPrefixScanRecordDesc{
                .Device = &device,
                .CommandContext = cmd,
                .Buffers = &buffers,
                .Pipelines = pipelines,
                .Input = inputBuffer,
                .Output = outputBuffer,
                .ElementCount = static_cast<std::uint32_t>(input.size()),
                .Mode = mode,
            });

        if (gpu.Succeeded() && !input.empty())
        {
            cmd->BufferBarrier(outputBuffer,
                               RHI::MemoryAccess::ShaderRead,
                               RHI::MemoryAccess::HostRead);
        }

        EndComputeFrame(device, frame, cmd);

        if (!gpu.Succeeded())
        {
            ADD_FAILURE() << caseName << ": GPU scan record failed with status "
                          << Graphics::DebugNameForParallelPrimitiveStatus(gpu.Status);
            DestroyBufferIfValid(device, outputBuffer);
            DestroyBufferIfValid(device, inputBuffer);
            return false;
        }

        if (!input.empty())
        {
            std::vector<std::uint32_t> actual(input.size(), 0u);
            device.ReadBuffer(outputBuffer,
                              actual.data(),
                              actual.size() * sizeof(std::uint32_t),
                              0u);
            EXPECT_EQ(actual, expected) << caseName;
        }

        DestroyBufferIfValid(device, outputBuffer);
        DestroyBufferIfValid(device, inputBuffer);
        return true;
    }

    struct CompactionSmokeResult
    {
        std::vector<std::uint32_t> KeptKeys{};
        std::uint32_t OutputCount = 0u;
        std::uint32_t ReadbackCount = 0u;
        Graphics::ParallelDispatchIndirectArgs DispatchArgs{};
    };

    [[nodiscard]] std::uint32_t CeilDiv(const std::uint32_t value,
                                        const std::uint32_t divisor) noexcept
    {
        return value == 0u ? 0u : 1u + ((value - 1u) / divisor);
    }

    [[nodiscard]] bool RunCompactionCase(RHI::IDevice& device,
                                         RHI::BufferManager& buffers,
                                         const Graphics::ParallelPrimitivePipelineSet pipelines,
                                         const RHI::PipelineHandle countToDispatchPipeline,
                                         const std::vector<std::uint32_t>& keys,
                                         const std::vector<std::uint32_t>& flags,
                                         const std::string_view caseName,
                                         CompactionSmokeResult* outResult = nullptr)
    {
        std::vector<std::uint32_t> expected(keys.size(), 0u);
        const Graphics::ParallelPrimitiveCpuResult cpu =
            Graphics::CompactByFlagsCpu(keys, flags, expected);
        if (!cpu.Succeeded())
        {
            ADD_FAILURE() << caseName << ": CPU reference failed";
            return false;
        }

        RHI::BufferHandle keysBuffer =
            CreateU32StorageBuffer(device, static_cast<std::uint32_t>(keys.size()),
                                   "ComputePrimitiveSmoke.Keys");
        RHI::BufferHandle flagsBuffer =
            CreateU32StorageBuffer(device, static_cast<std::uint32_t>(flags.size()),
                                   "ComputePrimitiveSmoke.Flags");
        RHI::BufferHandle outputKeysBuffer =
            CreateU32StorageBuffer(device, static_cast<std::uint32_t>(keys.size()),
                                   "ComputePrimitiveSmoke.OutputKeys");
        RHI::BufferHandle outputCountBuffer =
            CreateU32StorageBuffer(device, 1u, "ComputePrimitiveSmoke.OutputCount");
        RHI::BufferHandle readbackCountBuffer =
            device.CreateBuffer(
                Graphics::BuildParallelCompactionCountReadbackBufferDesc(
                    "ComputePrimitiveSmoke.OutputCount.Readback"));
        RHI::BufferHandle dispatchArgsBuffer =
            device.CreateBuffer(
                Graphics::BuildParallelDispatchIndirectArgsBufferDesc(
                    "ComputePrimitiveSmoke.DispatchArgs"));
        if (!keysBuffer.IsValid() ||
            !flagsBuffer.IsValid() ||
            !outputKeysBuffer.IsValid() ||
            !outputCountBuffer.IsValid() ||
            !readbackCountBuffer.IsValid() ||
            !dispatchArgsBuffer.IsValid())
        {
            ADD_FAILURE() << caseName << ": buffer allocation failed";
            DestroyBufferIfValid(device, dispatchArgsBuffer);
            DestroyBufferIfValid(device, readbackCountBuffer);
            DestroyBufferIfValid(device, outputCountBuffer);
            DestroyBufferIfValid(device, outputKeysBuffer);
            DestroyBufferIfValid(device, flagsBuffer);
            DestroyBufferIfValid(device, keysBuffer);
            return false;
        }

        std::vector<std::uint32_t> outputKeys(std::max<std::size_t>(keys.size(), 1u),
                                              0xdeadbeefu);
        std::uint32_t outputCount = 0xdeadbeefu;
        std::uint32_t readbackCount = 0xdeadbeefu;
        Graphics::ParallelDispatchIndirectArgs dispatchArgs{
            .GroupCountX = 0xdeadbeefu,
            .GroupCountY = 0xdeadbeefu,
            .GroupCountZ = 0xdeadbeefu,
        };
        if (!keys.empty())
        {
            device.WriteBuffer(keysBuffer,
                               keys.data(),
                               keys.size() * sizeof(std::uint32_t),
                               0u);
            device.WriteBuffer(flagsBuffer,
                               flags.data(),
                               flags.size() * sizeof(std::uint32_t),
                               0u);
        }
        device.WriteBuffer(outputKeysBuffer,
                           outputKeys.data(),
                           outputKeys.size() * sizeof(std::uint32_t),
                           0u);
        device.WriteBuffer(outputCountBuffer,
                           &outputCount,
                           sizeof(outputCount),
                           0u);
        device.WriteBuffer(readbackCountBuffer,
                           &readbackCount,
                           sizeof(readbackCount),
                           0u);
        device.WriteBuffer(dispatchArgsBuffer,
                           &dispatchArgs,
                           sizeof(dispatchArgs),
                           0u);

        auto destroyAll = [&device,
                           &dispatchArgsBuffer,
                           &readbackCountBuffer,
                           &outputCountBuffer,
                           &outputKeysBuffer,
                           &flagsBuffer,
                           &keysBuffer]() noexcept
        {
            DestroyBufferIfValid(device, dispatchArgsBuffer);
            DestroyBufferIfValid(device, readbackCountBuffer);
            DestroyBufferIfValid(device, outputCountBuffer);
            DestroyBufferIfValid(device, outputKeysBuffer);
            DestroyBufferIfValid(device, flagsBuffer);
            DestroyBufferIfValid(device, keysBuffer);
        };

        RHI::FrameHandle frame{};
        RHI::ICommandContext* cmd = nullptr;
        if (!BeginComputeFrame(device, frame, cmd, caseName))
        {
            destroyAll();
            return false;
        }

        if (!keys.empty())
        {
            cmd->BufferBarrier(keysBuffer,
                               RHI::MemoryAccess::HostWrite,
                               RHI::MemoryAccess::ShaderRead);
            cmd->BufferBarrier(flagsBuffer,
                               RHI::MemoryAccess::HostWrite,
                               RHI::MemoryAccess::ShaderRead);
            cmd->BufferBarrier(outputKeysBuffer,
                               RHI::MemoryAccess::HostWrite,
                               RHI::MemoryAccess::ShaderWrite);
            cmd->BufferBarrier(outputCountBuffer,
                               RHI::MemoryAccess::HostWrite,
                               RHI::MemoryAccess::ShaderWrite);
        }
        else
        {
            cmd->BufferBarrier(outputCountBuffer,
                               RHI::MemoryAccess::HostWrite,
                               RHI::MemoryAccess::TransferWrite);
        }

        const Graphics::GpuParallelPrimitiveRecordResult gpu =
            Graphics::RecordGpuStreamCompaction(Graphics::GpuStreamCompactionRecordDesc{
                .Device = &device,
                .CommandContext = cmd,
                .Buffers = &buffers,
                .Pipelines = pipelines,
                .Keys = keysBuffer,
                .Flags = flagsBuffer,
                .OutputKeys = outputKeysBuffer,
                .OutputCount = outputCountBuffer,
                .ElementCount = static_cast<std::uint32_t>(keys.size()),
            });

        Graphics::GpuCompactionCountPublicationResult publication{};
        if (gpu.Succeeded())
        {
            publication =
                Graphics::RecordCompactionCountPublication(
                    Graphics::GpuCompactionCountPublicationDesc{
                        .Device = &device,
                        .CommandContext = cmd,
                        .CountToDispatchArgsPipeline = countToDispatchPipeline,
                        .OutputCount = outputCountBuffer,
                        .ReadbackCount = readbackCountBuffer,
                        .DispatchArgs = dispatchArgsBuffer,
                        .DispatchGroupSize = 64u,
                    });
        }

        if (gpu.Succeeded())
        {
            if (!keys.empty())
            {
                cmd->BufferBarrier(outputKeysBuffer,
                                   RHI::MemoryAccess::ShaderRead,
                                   RHI::MemoryAccess::HostRead);
            }
        }
        if (gpu.Succeeded() && publication.Succeeded())
        {
            cmd->BufferBarrier(dispatchArgsBuffer,
                               RHI::MemoryAccess::IndirectRead,
                               RHI::MemoryAccess::HostRead);
        }

        EndComputeFrame(device, frame, cmd);

        if (!gpu.Succeeded())
        {
            ADD_FAILURE() << caseName << ": GPU compaction record failed with status "
                          << Graphics::DebugNameForParallelPrimitiveStatus(gpu.Status);
            destroyAll();
            return false;
        }
        if (!publication.Succeeded())
        {
            ADD_FAILURE() << caseName
                          << ": count publication failed with status "
                          << Graphics::DebugNameForParallelPrimitiveStatus(
                                 publication.Status);
            destroyAll();
            return false;
        }
        EXPECT_TRUE(publication.RecordedReadbackCopy) << caseName;
        EXPECT_TRUE(publication.RecordedDispatchArgs) << caseName;

        std::vector<std::uint32_t> actualKeys(outputKeys.size(), 0u);
        std::uint32_t actualReadbackCount = 0u;
        Graphics::ParallelDispatchIndirectArgs actualDispatchArgs{};
        device.ReadBuffer(outputKeysBuffer,
                          actualKeys.data(),
                          actualKeys.size() * sizeof(std::uint32_t),
                          0u);
        device.ReadBuffer(readbackCountBuffer,
                          &actualReadbackCount,
                          sizeof(actualReadbackCount),
                          0u);
        device.ReadBuffer(dispatchArgsBuffer,
                          &actualDispatchArgs,
                          sizeof(actualDispatchArgs),
                          0u);
        const std::uint32_t actualCount = actualReadbackCount;

        EXPECT_EQ(actualCount, cpu.Diagnostics.OutputCount) << caseName;
        EXPECT_EQ(actualReadbackCount, cpu.Diagnostics.OutputCount) << caseName;
        EXPECT_EQ(actualDispatchArgs.GroupCountX,
                  CeilDiv(cpu.Diagnostics.OutputCount, 64u)) << caseName;
        EXPECT_EQ(actualDispatchArgs.GroupCountY, 1u) << caseName;
        EXPECT_EQ(actualDispatchArgs.GroupCountZ, 1u) << caseName;
        for (std::uint32_t i = 0u; i < cpu.Diagnostics.OutputCount; ++i)
        {
            EXPECT_EQ(actualKeys[i], expected[i]) << caseName << " kept index " << i;
        }

        if (outResult != nullptr)
        {
            actualKeys.resize(cpu.Diagnostics.OutputCount);
            outResult->KeptKeys = std::move(actualKeys);
            outResult->OutputCount = actualCount;
            outResult->ReadbackCount = actualReadbackCount;
            outResult->DispatchArgs = actualDispatchArgs;
        }

        destroyAll();
        return true;
    }
} // namespace

TEST(ComputeParallelPrimitivesGpuSmoke, VulkanScanAndCompactionMatchCpuReference)
{
    auto bootstrap = BootstrapEngine();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }

    Extrinsic::Runtime::Engine& engine = *bootstrap.EnginePtr;
    EngineShutdownGuard shutdownGuard{engine};
    engine.Run();

    RHI::IDevice& device = engine.GetDevice();
    if (!device.IsOperational())
    {
        const auto status =
            Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus(&device);
        FAIL() << "Promoted Vulkan operational gate did not flip during compute primitive warmup: status="
               << Extrinsic::Backends::Vulkan::ToString(status.Code)
               << " reason=" << Extrinsic::Backends::Vulkan::ToString(status.Reason);
    }

    const std::string prefixShader =
        Extrinsic::Core::Filesystem::GetShaderPath(
            "shaders/parallel_prefix_scan.comp.spv");
    const std::string addShader =
        Extrinsic::Core::Filesystem::GetShaderPath(
            "shaders/parallel_scan_add_offsets.comp.spv");
    const std::string compactShader =
        Extrinsic::Core::Filesystem::GetShaderPath(
            "shaders/parallel_compact_by_flags.comp.spv");
    const std::string countToDispatchShader =
        Extrinsic::Core::Filesystem::GetShaderPath(
            "shaders/parallel_count_to_dispatch_args.comp.spv");

    RHI::PipelineHandle prefixPipeline =
        device.CreatePipeline(Graphics::BuildParallelPrefixScanPipelineDesc(
            prefixShader.c_str()));
    RHI::PipelineHandle addPipeline =
        device.CreatePipeline(Graphics::BuildParallelScanAddOffsetsPipelineDesc(
            addShader.c_str()));
    RHI::PipelineHandle compactPipeline =
        device.CreatePipeline(Graphics::BuildParallelCompactByFlagsPipelineDesc(
            compactShader.c_str()));
    RHI::PipelineHandle countToDispatchPipeline =
        device.CreatePipeline(Graphics::BuildParallelCountToDispatchArgsPipelineDesc(
            countToDispatchShader.c_str()));

    if (!prefixPipeline.IsValid() ||
        !addPipeline.IsValid() ||
        !compactPipeline.IsValid() ||
        !countToDispatchPipeline.IsValid())
    {
        DestroyPipelineIfValid(device, countToDispatchPipeline);
        DestroyPipelineIfValid(device, compactPipeline);
        DestroyPipelineIfValid(device, addPipeline);
        DestroyPipelineIfValid(device, prefixPipeline);
        FAIL() << "Operational Vulkan device failed to create compute primitive pipelines.";
    }

    const Graphics::ParallelPrimitivePipelineSet pipelines{
        .PrefixScan = prefixPipeline,
        .AddBlockOffsets = addPipeline,
        .CompactByFlags = compactPipeline,
    };

    {
        RHI::BufferManager buffers{device};
        ASSERT_TRUE(RunPrefixCase(device,
                                  buffers,
                                  pipelines,
                                  {},
                                  Graphics::PrefixScanMode::Exclusive,
                                  "prefix empty exclusive"));
        ASSERT_TRUE(RunPrefixCase(device,
                                  buffers,
                                  pipelines,
                                  {7u},
                                  Graphics::PrefixScanMode::Inclusive,
                                  "prefix single inclusive"));
        ASSERT_TRUE(RunPrefixCase(device,
                                  buffers,
                                  pipelines,
                                  {3u, 0u, 2u, 7u, 1u, 4u, 0u, 5u, 9u},
                                  Graphics::PrefixScanMode::Exclusive,
                                  "prefix small exclusive"));

        std::vector<std::uint32_t> large(777u, 0u);
        for (std::uint32_t i = 0u; i < large.size(); ++i)
        {
            large[i] = (i * 17u + 5u) % 11u;
        }
        ASSERT_TRUE(RunPrefixCase(device,
                                  buffers,
                                  pipelines,
                                  large,
                                  Graphics::PrefixScanMode::Exclusive,
                                  "prefix multiblock exclusive"));

        ASSERT_TRUE(RunCompactionCase(device,
                                      buffers,
                                      pipelines,
                                      countToDispatchPipeline,
                                      {},
                                      {},
                                      "compact empty"));
        ASSERT_TRUE(RunCompactionCase(device,
                                      buffers,
                                      pipelines,
                                      countToDispatchPipeline,
                                      {10u, 11u, 12u, 13u, 14u, 15u},
                                      {0u, 1u, 2u, 0u, 1u, 0u},
                                      "compact mixed"));
        ASSERT_TRUE(RunCompactionCase(device,
                                      buffers,
                                      pipelines,
                                      countToDispatchPipeline,
                                      {4u, 5u, 6u},
                                      {1u, 1u, 1u},
                                      "compact all-kept"));
        ASSERT_TRUE(RunCompactionCase(device,
                                      buffers,
                                      pipelines,
                                      countToDispatchPipeline,
                                      {4u, 5u, 6u},
                                      {0u, 0u, 0u},
                                      "compact all-dropped"));

        std::vector<std::uint32_t> compactKeys(777u, 0u);
        std::vector<std::uint32_t> compactFlags(777u, 0u);
        for (std::uint32_t i = 0u; i < compactKeys.size(); ++i)
        {
            compactKeys[i] = 1000u + i;
            compactFlags[i] = ((i * 37u + 13u) % 5u == 0u)
                ? ((i % 3u) + 1u)
                : 0u;
        }
        ASSERT_TRUE(RunCompactionCase(device,
                                      buffers,
                                      pipelines,
                                      countToDispatchPipeline,
                                      compactKeys,
                                      compactFlags,
                                      "compact multiblock pseudorandom"));

        CompactionSmokeResult firstDeterministic{};
        CompactionSmokeResult secondDeterministic{};
        ASSERT_TRUE(RunCompactionCase(device,
                                      buffers,
                                      pipelines,
                                      countToDispatchPipeline,
                                      compactKeys,
                                      compactFlags,
                                      "compact deterministic first",
                                      &firstDeterministic));
        ASSERT_TRUE(RunCompactionCase(device,
                                      buffers,
                                      pipelines,
                                      countToDispatchPipeline,
                                      compactKeys,
                                      compactFlags,
                                      "compact deterministic second",
                                      &secondDeterministic));
        EXPECT_EQ(firstDeterministic.OutputCount, secondDeterministic.OutputCount);
        EXPECT_EQ(firstDeterministic.ReadbackCount, secondDeterministic.ReadbackCount);
        EXPECT_EQ(firstDeterministic.KeptKeys, secondDeterministic.KeptKeys);
        EXPECT_EQ(firstDeterministic.DispatchArgs.GroupCountX,
                  secondDeterministic.DispatchArgs.GroupCountX);
        EXPECT_EQ(firstDeterministic.DispatchArgs.GroupCountY,
                  secondDeterministic.DispatchArgs.GroupCountY);
        EXPECT_EQ(firstDeterministic.DispatchArgs.GroupCountZ,
                  secondDeterministic.DispatchArgs.GroupCountZ);
    }

    DestroyPipelineIfValid(device, countToDispatchPipeline);
    DestroyPipelineIfValid(device, compactPipeline);
    DestroyPipelineIfValid(device, addPipeline);
    DestroyPipelineIfValid(device, prefixPipeline);
}
