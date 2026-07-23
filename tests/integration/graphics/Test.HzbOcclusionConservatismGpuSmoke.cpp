#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "OperationalCounterStability.hpp"

#include "RuntimeTestModule.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;

namespace
{
namespace Counters = Extrinsic::Tests::Support::OperationalCounterStability;

using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
using Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs;
using Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::ToString;
using Extrinsic::Runtime::Engine;

constexpr std::uint32_t kOptionExemptSelectionBuckets = 1u << 1u;

struct alignas(4) HZBSmokeCandidate
{
    float PreviousNearestDepth = 0.0f;
    float PreviousConservativeMaxDepth = 1.0f;
    float CurrentNearestDepth = 0.0f;
    float CurrentConservativeMaxDepth = 1.0f;
    std::uint32_t PreviousValid = 0u;
    std::uint32_t CurrentValid = 0u;
    std::uint32_t FrustumVisible = 0u;
    std::uint32_t Bucket = 0u;
};
static_assert(sizeof(HZBSmokeCandidate) == 32u);

struct alignas(4) HZBSmokeResult
{
    std::uint32_t Decision = 0u;
    std::uint32_t Bucket = 0u;
    std::uint32_t Phase1VisibleCount = 0u;
    std::uint32_t Phase1RejectedCount = 0u;
    std::uint32_t Phase2RescuedCount = 0u;
    std::uint32_t FrustumRejectedCount = 0u;
    std::uint32_t SelectionOcclusionExemptCount = 0u;
    std::uint32_t _pad0 = 0u;
};
static_assert(sizeof(HZBSmokeResult) == 32u);

struct alignas(8) HZBSmokePushConstants
{
    std::uint64_t CandidateBDA = 0u;
    std::uint64_t ResultBDA = 0u;
    std::uint32_t CandidateCount = 0u;
    std::uint32_t Options = 0u;
    std::uint32_t _pad0 = 0u;
    std::uint32_t _pad1 = 0u;
};
static_assert(sizeof(HZBSmokePushConstants) == 32u);

struct BucketCounters
{
    std::uint32_t Phase1VisibleCount = 0u;
    std::uint32_t Phase1RejectedCount = 0u;
    std::uint32_t Phase2RescuedCount = 0u;
};

class ExitAfterFramesApp final : public Intrinsic::Tests::RuntimeTestModule
{
public:
    explicit ExitAfterFramesApp(const std::uint32_t targetFrames) noexcept
        : m_TargetFrames(targetFrames)
    {
    }

    void Resolve() override {}
    void Simulate(double) override {}

    void Frame(double, double) override
    {
        auto& engine = Kernel();
        ++m_Frames;
        if (m_Frames >= m_TargetFrames)
        {
            engine.RequestExit();
        }
    }

    void Shutdown() override {}

private:
    std::uint32_t m_TargetFrames = 1u;
    std::uint32_t m_Frames = 0u;
};

Counters::Snapshot ToCounterSnapshot(
    const Extrinsic::Backends::Vulkan::VulkanOperationalDiagnosticsSnapshot& vk) noexcept
{
    return Counters::Snapshot{
        vk.VulkanFallbackToNullCount,
        vk.VulkanInitFailureCount,
        vk.VulkanValidationErrorCount,
        vk.VulkanOperationalGateFailureCount,
    };
}

struct SmokeBootstrap
{
    std::unique_ptr<Engine> EnginePtr;
    bool Skipped = false;
    std::string SkipReason;
};

[[nodiscard]] SmokeBootstrap BootstrapEngineForHzbSmoke()
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        return SmokeBootstrap{
            .EnginePtr  = nullptr,
            .Skipped    = true,
            .SkipReason = "GLFW could not initialize in this environment; "
                          "gpu;vulkan HZB conservatism smoke is opt-in.",
        };
    }

    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
    config.Window.Title = "Intrinsic HZB conservatism gpu;vulkan smoke";
    config.Window.Width = 128u;
    config.Window.Height = 128u;
    config.Window.Resizable = false;
    config.Render.EnableValidation = false;
    config.Render.EnableVSync = false;

    auto engine = std::make_unique<Engine>(config);
    Intrinsic::Tests::AddRuntimeTestModule(*engine, std::make_unique<ExitAfterFramesApp>(4u));
    engine->Initialize();

    const auto initInputs = GetVulkanDeviceOperationalInputs(&engine->GetDevice());
    if (!initInputs.LogicalDeviceReady || !initInputs.SwapchainReady || !initInputs.CommandSyncReady)
    {
        engine->Shutdown();
        return SmokeBootstrap{
            .EnginePtr  = nullptr,
            .Skipped    = true,
            .SkipReason = "Promoted Vulkan did not reach "
                          "logical-device/swapchain/command-sync readiness on this host.",
        };
    }

    return SmokeBootstrap{.EnginePtr = std::move(engine), .Skipped = false, .SkipReason = {}};
}

[[nodiscard]] Extrinsic::Graphics::CullingTwoPhaseCandidate ToCpuCandidate(
    const HZBSmokeCandidate& candidate) noexcept
{
    return Extrinsic::Graphics::CullingTwoPhaseCandidate{
        .Bucket = static_cast<Extrinsic::RHI::GpuDrawBucketKind>(candidate.Bucket),
        .FrustumVisible = candidate.FrustumVisible != 0u,
        .PreviousFrameHZB = Extrinsic::Graphics::CullingHZBDepthSample{
            .NearestDepth = candidate.PreviousNearestDepth,
            .ConservativeMaxDepth = candidate.PreviousConservativeMaxDepth,
            .Valid = candidate.PreviousValid != 0u,
        },
        .CurrentFrameHZB = Extrinsic::Graphics::CullingHZBDepthSample{
            .NearestDepth = candidate.CurrentNearestDepth,
            .ConservativeMaxDepth = candidate.CurrentConservativeMaxDepth,
            .Valid = candidate.CurrentValid != 0u,
        },
    };
}

[[nodiscard]] std::string_view DecisionName(
    const Extrinsic::Graphics::CullingTwoPhaseDecision decision) noexcept
{
    switch (decision)
    {
    case Extrinsic::Graphics::CullingTwoPhaseDecision::FrustumRejected:
        return "FrustumRejected";
    case Extrinsic::Graphics::CullingTwoPhaseDecision::Phase1Visible:
        return "Phase1Visible";
    case Extrinsic::Graphics::CullingTwoPhaseDecision::Phase1Rejected:
        return "Phase1Rejected";
    case Extrinsic::Graphics::CullingTwoPhaseDecision::Phase2Rescued:
        return "Phase2Rescued";
    case Extrinsic::Graphics::CullingTwoPhaseDecision::Phase2Rejected:
        return "Phase2Rejected";
    }
    return "Unknown";
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

void DestroyPipelineIfValid(Extrinsic::RHI::IDevice& device,
                            Extrinsic::RHI::PipelineHandle& handle) noexcept
{
    if (handle.IsValid())
    {
        device.DestroyPipeline(handle);
        handle = {};
    }
}
} // namespace

TEST(HzbOcclusionConservatismGpuSmoke, TwoPhasePredicateMatchesCpuContractOnOperationalVulkan)
{
    auto bootstrap = BootstrapEngineForHzbSmoke();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const Counters::Snapshot beforeWarmup =
        ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    engine.Run();

    Extrinsic::RHI::IDevice& device = engine.GetDevice();
    const auto status = EvaluateVulkanDeviceOperationalStatus(&device);
    if (!device.IsOperational())
    {
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during HZB "
                         "conservatism warmup: status="
                      << ToString(status.Code) << " reason=" << ToString(status.Reason)
                      << ". Host capability checks passed, so this is a "
                         "GRAPHICS-038E regression, not a skip condition.";
        return;
    }

    const Counters::Snapshot afterWarmup =
        ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    if (!Counters::IsStable(beforeWarmup, afterWarmup))
    {
        engine.Shutdown();
        ASSERT_TRUE(Counters::IsStable(beforeWarmup, afterWarmup))
            << "Vulkan fallback counters incremented during HZB conservatism "
               "warmup.";
    }

    constexpr std::array<HZBSmokeCandidate, 6u> kCandidates{{
        // Known visible probe: equal nearest/max depth must not over-reject.
        HZBSmokeCandidate{
            .PreviousNearestDepth = 0.50f,
            .PreviousConservativeMaxDepth = 0.50f,
            .CurrentNearestDepth = 0.80f,
            .CurrentConservativeMaxDepth = 0.20f,
            .PreviousValid = 1u,
            .CurrentValid = 1u,
            .FrustumVisible = 1u,
            .Bucket = static_cast<std::uint32_t>(Extrinsic::RHI::GpuDrawBucketKind::SurfaceOpaque),
        },
        // Disocclusion rescue: previous HZB rejects, current HZB accepts.
        HZBSmokeCandidate{
            .PreviousNearestDepth = 0.80f,
            .PreviousConservativeMaxDepth = 0.20f,
            .CurrentNearestDepth = 0.25f,
            .CurrentConservativeMaxDepth = 0.70f,
            .PreviousValid = 1u,
            .CurrentValid = 1u,
            .FrustumVisible = 1u,
            .Bucket = static_cast<std::uint32_t>(Extrinsic::RHI::GpuDrawBucketKind::SurfaceOpaque),
        },
        // Persistent occlusion: both HZB samples reject.
        HZBSmokeCandidate{
            .PreviousNearestDepth = 0.90f,
            .PreviousConservativeMaxDepth = 0.30f,
            .CurrentNearestDepth = 0.95f,
            .CurrentConservativeMaxDepth = 0.40f,
            .PreviousValid = 1u,
            .CurrentValid = 1u,
            .FrustumVisible = 1u,
            .Bucket = static_cast<std::uint32_t>(Extrinsic::RHI::GpuDrawBucketKind::Lines),
        },
        // Selection buckets stay frustum-only even if both HZB samples reject.
        HZBSmokeCandidate{
            .PreviousNearestDepth = 0.90f,
            .PreviousConservativeMaxDepth = 0.10f,
            .CurrentNearestDepth = 0.90f,
            .CurrentConservativeMaxDepth = 0.10f,
            .PreviousValid = 1u,
            .CurrentValid = 1u,
            .FrustumVisible = 1u,
            .Bucket = static_cast<std::uint32_t>(Extrinsic::RHI::GpuDrawBucketKind::SelectionSurface),
        },
        // Missing previous sample is conservative and visible.
        HZBSmokeCandidate{
            .PreviousNearestDepth = 0.95f,
            .PreviousConservativeMaxDepth = 0.10f,
            .CurrentNearestDepth = 0.95f,
            .CurrentConservativeMaxDepth = 0.10f,
            .PreviousValid = 0u,
            .CurrentValid = 1u,
            .FrustumVisible = 1u,
            .Bucket = static_cast<std::uint32_t>(Extrinsic::RHI::GpuDrawBucketKind::Points),
        },
        // Frustum rejection remains the first rejection gate.
        HZBSmokeCandidate{
            .PreviousNearestDepth = 0.10f,
            .PreviousConservativeMaxDepth = 1.00f,
            .CurrentNearestDepth = 0.10f,
            .CurrentConservativeMaxDepth = 1.00f,
            .PreviousValid = 1u,
            .CurrentValid = 1u,
            .FrustumVisible = 0u,
            .Bucket = static_cast<std::uint32_t>(Extrinsic::RHI::GpuDrawBucketKind::ShadowOpaque),
        },
    }};

    std::array<Extrinsic::Graphics::CullingTwoPhaseCandidate, kCandidates.size()> cpuCandidates{};
    for (std::size_t i = 0; i < kCandidates.size(); ++i)
    {
        cpuCandidates[i] = ToCpuCandidate(kCandidates[i]);
    }
    const Extrinsic::Graphics::CullingTwoPhasePartition expected =
        Extrinsic::Graphics::ComputeTwoPhaseCullPartition(cpuCandidates);
    if (expected.Decisions.size() != kCandidates.size())
    {
        engine.Shutdown();
        ASSERT_EQ(expected.Decisions.size(), kCandidates.size());
    }

    Extrinsic::RHI::PipelineDesc pipelineDesc{};
    pipelineDesc.ComputeShaderPath =
        Extrinsic::Core::Filesystem::GetShaderPath("shaders/tests/hzb_conservatism_smoke.comp.spv");
    pipelineDesc.PushConstantSize = static_cast<std::uint32_t>(sizeof(HZBSmokePushConstants));
    pipelineDesc.DebugName = "HzbOcclusionConservatismGpuSmoke.Predicate";
    Extrinsic::RHI::PipelineHandle pipeline = device.CreatePipeline(pipelineDesc);
    if (!pipeline.IsValid())
    {
        engine.Shutdown();
        ASSERT_TRUE(pipeline.IsValid())
            << "Operational Vulkan device failed to create the HZB conservatism "
               "smoke compute pipeline.";
    }

    Extrinsic::RHI::BufferHandle candidateBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = sizeof(kCandidates),
        .Usage = Extrinsic::RHI::BufferUsage::Storage,
        .HostVisible = true,
        .DebugName = "HZBConservatismSmoke.Candidates",
    });
    Extrinsic::RHI::BufferHandle resultBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = sizeof(HZBSmokeResult) * kCandidates.size(),
        .Usage = Extrinsic::RHI::BufferUsage::Storage | Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "HZBConservatismSmoke.Results",
    });
    if (!candidateBuffer.IsValid() || !resultBuffer.IsValid())
    {
        DestroyBufferIfValid(device, resultBuffer);
        DestroyBufferIfValid(device, candidateBuffer);
        DestroyPipelineIfValid(device, pipeline);
        engine.Shutdown();
        ASSERT_TRUE(candidateBuffer.IsValid() && resultBuffer.IsValid())
            << "Operational Vulkan device failed to create HZB conservatism smoke "
               "buffers.";
    }

    constexpr std::array<HZBSmokeResult, kCandidates.size()> kZeroResults{};
    device.WriteBuffer(candidateBuffer, kCandidates.data(), sizeof(kCandidates), 0u);
    device.WriteBuffer(resultBuffer, kZeroResults.data(), sizeof(kZeroResults), 0u);

    const HZBSmokePushConstants push{
        .CandidateBDA = device.GetBufferDeviceAddress(candidateBuffer),
        .ResultBDA = device.GetBufferDeviceAddress(resultBuffer),
        .CandidateCount = static_cast<std::uint32_t>(kCandidates.size()),
        .Options = kOptionExemptSelectionBuckets,
    };
    if (push.CandidateBDA == 0u || push.ResultBDA == 0u)
    {
        DestroyBufferIfValid(device, resultBuffer);
        DestroyBufferIfValid(device, candidateBuffer);
        DestroyPipelineIfValid(device, pipeline);
        engine.Shutdown();
        ASSERT_NE(push.CandidateBDA, 0u);
        ASSERT_NE(push.ResultBDA, 0u);
    }

    Extrinsic::RHI::FrameHandle frame{};
    if (!device.BeginFrame(frame))
    {
        DestroyBufferIfValid(device, resultBuffer);
        DestroyBufferIfValid(device, candidateBuffer);
        DestroyPipelineIfValid(device, pipeline);
        engine.Shutdown();
        ADD_FAILURE() << "Operational Vulkan device failed to begin the HZB "
                         "conservatism smoke frame.";
        return;
    }

    Extrinsic::RHI::ICommandContext& cmd = device.GetGraphicsContext(frame.FrameIndex);
    cmd.Begin();
    cmd.BufferBarrier(candidateBuffer,
                      Extrinsic::RHI::MemoryAccess::HostWrite,
                      Extrinsic::RHI::MemoryAccess::ShaderRead);
    cmd.BufferBarrier(resultBuffer,
                      Extrinsic::RHI::MemoryAccess::HostWrite,
                      Extrinsic::RHI::MemoryAccess::ShaderWrite);
    cmd.BindPipeline(pipeline);
    cmd.PushConstants(&push, static_cast<std::uint32_t>(sizeof(push)), 0u);
    cmd.Dispatch(static_cast<std::uint32_t>((kCandidates.size() + 63u) / 64u), 1u, 1u);
    cmd.BufferBarrier(resultBuffer,
                      Extrinsic::RHI::MemoryAccess::ShaderWrite,
                      Extrinsic::RHI::MemoryAccess::HostRead);
    cmd.End();
    device.EndFrame(frame);
    device.Present(frame);

    std::array<HZBSmokeResult, kCandidates.size()> actual{};
    device.ReadBuffer(resultBuffer, actual.data(), sizeof(actual), 0u);

    std::array<BucketCounters, static_cast<std::size_t>(Extrinsic::RHI::GpuDrawBucketKind::Count)> actualBuckets{};
    std::uint32_t actualFrustumRejected = 0u;
    std::uint32_t actualSelectionExempt = 0u;
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        const auto actualDecision =
            static_cast<Extrinsic::Graphics::CullingTwoPhaseDecision>(actual[i].Decision);
        EXPECT_EQ(actualDecision, expected.Decisions[i])
            << "candidate " << i << " expected "
            << DecisionName(expected.Decisions[i]) << " got "
            << DecisionName(actualDecision);

        if (actual[i].Bucket >=
            static_cast<std::uint32_t>(Extrinsic::RHI::GpuDrawBucketKind::Count))
        {
            ADD_FAILURE() << "candidate " << i << " wrote invalid bucket " << actual[i].Bucket;
            continue;
        }
        BucketCounters& bucket = actualBuckets[actual[i].Bucket];
        bucket.Phase1VisibleCount += actual[i].Phase1VisibleCount;
        bucket.Phase1RejectedCount += actual[i].Phase1RejectedCount;
        bucket.Phase2RescuedCount += actual[i].Phase2RescuedCount;
        actualFrustumRejected += actual[i].FrustumRejectedCount;
        actualSelectionExempt += actual[i].SelectionOcclusionExemptCount;
    }

    for (std::size_t i = 0; i < actualBuckets.size(); ++i)
    {
        const auto& gpu = actualBuckets[i];
        const auto& cpu = expected.Buckets[i];
        EXPECT_EQ(gpu.Phase1VisibleCount, cpu.Phase1VisibleCount)
            << "bucket " << i << " phase-1 visible mismatch";
        EXPECT_EQ(gpu.Phase1RejectedCount, cpu.Phase1RejectedCount)
            << "bucket " << i << " phase-1 rejected mismatch";
        EXPECT_EQ(gpu.Phase2RescuedCount, cpu.Phase2RescuedCount)
            << "bucket " << i << " phase-2 rescued mismatch";
    }
    EXPECT_EQ(actualFrustumRejected, expected.FrustumRejectedCount);
    EXPECT_EQ(actualSelectionExempt, expected.SelectionOcclusionExemptCount);

    EXPECT_EQ(actual[0].Decision, static_cast<std::uint32_t>(
                                      Extrinsic::Graphics::CullingTwoPhaseDecision::Phase1Visible))
        << "known-visible probe was over-rejected by the Vulkan HZB predicate "
           "smoke";
    EXPECT_EQ(actual[1].Decision, static_cast<std::uint32_t>(
                                      Extrinsic::Graphics::CullingTwoPhaseDecision::Phase2Rescued))
        << "known-disoccluded probe was not rescued by the Vulkan HZB predicate "
           "smoke";

    const Counters::Snapshot afterSmoke =
        ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    EXPECT_TRUE(Counters::IsStable(afterWarmup, afterSmoke))
        << "Vulkan fallback counters incremented across the HZB conservatism "
           "smoke frame.";

    DestroyBufferIfValid(device, resultBuffer);
    DestroyBufferIfValid(device, candidateBuffer);
    DestroyPipelineIfValid(device, pipeline);
    engine.Shutdown();
}
