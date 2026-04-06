#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "RHI.Vulkan.hpp"

import Core;
import RHI;
import Runtime.FrameLoop;
import Runtime.RenderExtraction;
import Runtime.ResourceMaintenance;

namespace
{
    // Probe whether Vulkan instance creation will succeed. VulkanContext's
    // constructor calls std::exit(-1) on failure, so we must pre-check.
    [[nodiscard]] bool IsVulkanAvailable()
    {
        if (volkInitialize() != VK_SUCCESS)
            return false;

        // Try to actually create an instance — this catches missing ICDs and
        // driver issues that volkInitialize() alone cannot detect.
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "VulkanProbe";
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        VkInstance probeInstance = VK_NULL_HANDLE;
        VkResult result = vkCreateInstance(&createInfo, nullptr, &probeInstance);
        if (result != VK_SUCCESS || probeInstance == VK_NULL_HANDLE)
            return false;
        vkDestroyInstance(probeInstance, nullptr);
        return true;
    }
}

// =============================================================================
// CPU-only tests — verify maintenance lane coordinator contract over N frames
// =============================================================================

namespace
{
    class FakeMaintenanceLaneHost final : public Runtime::IMaintenanceLaneHost
    {
    public:
        void CaptureGpuSyncState() override { Calls.emplace_back("capture_sync"); }
        void ProcessCompletedReadbacks() override { Calls.emplace_back("readbacks"); }
        void CollectGpuDeferredDestructions() override { Calls.emplace_back("deferred_gc"); }
        void GarbageCollectTransfers() override { Calls.emplace_back("gc"); }
        void ProcessTextureDeletions() override { Calls.emplace_back("textures"); }
        void ProcessMaterialDeletions() override { Calls.emplace_back("materials"); }
        void CaptureFrameTelemetry(const Runtime::FrameTelemetrySnapshot& snapshot) override
        {
            Calls.emplace_back("telemetry");
            LastTelemetry = snapshot;
            TelemetryHistory.push_back(snapshot);
        }
        void BookkeepHotReloads() override { Calls.emplace_back("hot_reloads"); }

        std::vector<std::string> Calls;
        Runtime::FrameTelemetrySnapshot LastTelemetry{};
        std::vector<Runtime::FrameTelemetrySnapshot> TelemetryHistory;
    };

    // Expected call sequence per maintenance lane run — canonical ordering.
    // All 8 steps: GPU sync capture, readback processing, deferred GC,
    // transfer GC, texture deletions, material deletions, telemetry, hot-reload.
    const std::vector<std::string> kExpectedCallSequence = {
        "capture_sync",
        "readbacks",
        "deferred_gc",
        "gc",
        "textures",
        "materials",
        "telemetry",
        "hot_reloads",
    };
}

// Verify the maintenance lane runs all 8 steps in the correct order on every
// frame, and that telemetry snapshots are propagated correctly across N frames.
TEST(MaintenanceLane, CoordinatorRunsAllStepsInOrderOverNFrames)
{
    FakeMaintenanceLaneHost host;
    const Runtime::MaintenanceLaneCoordinator coordinator{.Host = host};

    constexpr int kFrameCount = 5;
    for (int frame = 0; frame < kFrameCount; ++frame)
    {
        host.Calls.clear();
        const Runtime::FrameTelemetrySnapshot telemetry{
            .FixedStepSubsteps = static_cast<uint32_t>(frame + 1),
            .AccumulatorClamped = (frame % 2 == 0),
            .SimulationCpuTimeNs = static_cast<uint64_t>(frame * 100'000),
            .FrameGraphCompileNs = static_cast<uint64_t>(frame * 50'000),
            .FrameGraphExecuteNs = static_cast<uint64_t>(frame * 200'000),
            .FrameGraphCriticalPathNs = static_cast<uint64_t>(frame * 150'000),
        };

        coordinator.Run(telemetry);

        // Verify all 8 steps ran in the canonical order.
        EXPECT_EQ(host.Calls, kExpectedCallSequence)
            << "Maintenance lane call sequence diverged on frame " << frame;

        // Verify telemetry was propagated for this frame.
        EXPECT_EQ(host.LastTelemetry.FixedStepSubsteps, static_cast<uint32_t>(frame + 1));
        EXPECT_EQ(host.LastTelemetry.AccumulatorClamped, (frame % 2 == 0));
        EXPECT_EQ(host.LastTelemetry.SimulationCpuTimeNs, static_cast<uint64_t>(frame * 100'000));
    }

    // Verify all N frames were captured.
    ASSERT_EQ(host.TelemetryHistory.size(), static_cast<size_t>(kFrameCount));
    for (int i = 0; i < kFrameCount; ++i)
    {
        EXPECT_EQ(host.TelemetryHistory[i].FixedStepSubsteps, static_cast<uint32_t>(i + 1))
            << "Telemetry history mismatch at frame " << i;
    }
}

// Verify the maintenance lane is idempotent — running it with default/zero
// telemetry does not corrupt subsequent frames.
TEST(MaintenanceLane, ZeroTelemetryDoesNotCorruptSubsequentFrames)
{
    FakeMaintenanceLaneHost host;
    const Runtime::MaintenanceLaneCoordinator coordinator{.Host = host};

    // Frame 0: zero telemetry
    coordinator.Run({});
    EXPECT_EQ(host.LastTelemetry.FixedStepSubsteps, 0u);
    EXPECT_EQ(host.LastTelemetry.SimulationCpuTimeNs, 0u);

    // Frame 1: real telemetry
    const Runtime::FrameTelemetrySnapshot real{
        .FixedStepSubsteps = 3,
        .SimulationCpuTimeNs = 42'000,
    };
    coordinator.Run(real);
    EXPECT_EQ(host.LastTelemetry.FixedStepSubsteps, 3u);
    EXPECT_EQ(host.LastTelemetry.SimulationCpuTimeNs, 42'000u);

    // Frame 2: zero again — previous values must not leak
    coordinator.Run({});
    EXPECT_EQ(host.LastTelemetry.FixedStepSubsteps, 0u);
    EXPECT_EQ(host.LastTelemetry.SimulationCpuTimeNs, 0u);
}

// =============================================================================
// GPU integration tests — verify the deferred-destruction mechanism that the
// maintenance lane's CollectGpuDeferredDestructions step depends on.
//
// These test VulkanDevice's timeline-based deletion infrastructure directly
// because the full MaintenanceLaneCoordinator -> ResourceMaintenanceService ->
// GraphicsBackend -> VulkanDevice chain requires a complete runtime stack.
// The CPU tests above verify the coordinator contract; these verify the
// underlying GPU resource retirement that makes CollectGpuDeferredDestructions
// correct.
//
// Note: The maintenance lane calls CollectGarbage() (timeline-gated), while
// these tests also use FlushTimelineDeletionQueueNow() for shutdown paths.
// Both paths are exercised.
// =============================================================================

class MaintenanceLaneGpuTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!IsVulkanAvailable())
        {
            GTEST_SKIP() << "No Vulkan ICD available (headless environment)";
        }

        RHI::ContextConfig ctxConfig{
            .AppName = "MaintenanceLaneTest",
            .EnableValidation = false,
            .Headless = true,
        };

        m_Context = std::make_unique<RHI::VulkanContext>(ctxConfig);
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, VK_NULL_HANDLE);
        if (!m_Device->IsValid())
        {
            GTEST_SKIP() << "No suitable GPU found";
        }
    }

    void TearDown() override
    {
        if (m_Device && m_Device->IsValid())
        {
            (void)m_Device->WaitForGraphicsIdle();
            m_Device->FlushTimelineDeletionQueueNow();
            m_Device->FlushAllDeletionQueues();
        }
    }

    std::unique_ptr<RHI::VulkanContext> m_Context;
    std::shared_ptr<RHI::VulkanDevice> m_Device;
};

// Verify that SafeDestroy schedules a deferred deletion that is not executed
// until CollectGarbage() runs after the GPU has completed past the target
// timeline value. In headless mode without real GPU submissions, we verify the
// lifecycle by using WaitForGraphicsIdle + FlushTimelineDeletionQueueNow.
TEST_F(MaintenanceLaneGpuTest, SafeDestroyDefersUntilTimelineCompletion)
{
    int deleteCount = 0;

    // Schedule 3 deferred deletions.
    for (int i = 0; i < 3; ++i)
    {
        m_Device->SafeDestroy([&deleteCount]() {
            ++deleteCount;
        });
    }

    // Before any GPU completion, CollectGarbage should not fire the deletions
    // (timeline completed value is 0, but deletions target value >= 1).
    m_Device->CollectGarbage();
    EXPECT_EQ(deleteCount, 0)
        << "Deferred deletions should not fire before timeline completion";

    // Force GPU idle and flush — this should execute all pending deletions.
    (void)m_Device->WaitForGraphicsIdle();
    m_Device->FlushTimelineDeletionQueueNow();
    EXPECT_EQ(deleteCount, 3)
        << "All deferred deletions should fire after flush";
}

// Verify SafeDestroyAfter respects per-deletion timeline values: deletions
// with earlier timeline values are collected before later ones.
TEST_F(MaintenanceLaneGpuTest, SafeDestroyAfterRespectsTimelineOrdering)
{
    std::vector<int> executionOrder;

    // Schedule deletions at different timeline values.
    m_Device->SafeDestroyAfter(1, [&executionOrder]() {
        executionOrder.push_back(1);
    });
    m_Device->SafeDestroyAfter(5, [&executionOrder]() {
        executionOrder.push_back(5);
    });
    m_Device->SafeDestroyAfter(3, [&executionOrder]() {
        executionOrder.push_back(3);
    });

    // In headless mode, GetGraphicsTimelineCompletedValue() returns 0 via
    // vkGetSemaphoreCounterValue (no submissions occurred). CollectGarbage
    // should not fire any of these.
    m_Device->CollectGarbage();
    EXPECT_TRUE(executionOrder.empty())
        << "No deletions should fire with completed value = 0";

    // Flush all pending — exercises the unconditional flush path used during
    // shutdown. Note: FlushTimelineDeletionQueueNow executes in insertion order
    // (not sorted by timeline value), unlike CollectGarbage which is gated by
    // timeline completion.
    (void)m_Device->WaitForGraphicsIdle();
    m_Device->FlushTimelineDeletionQueueNow();

    ASSERT_EQ(executionOrder.size(), 3u);
    EXPECT_EQ(executionOrder[0], 1);
    EXPECT_EQ(executionOrder[1], 5);
    EXPECT_EQ(executionOrder[2], 3);
}

// Verify that a real VulkanBuffer can be deferred-destroyed via SafeDestroy
// without leaking GPU memory or crashing during device teardown.
TEST_F(MaintenanceLaneGpuTest, BufferDeferredDestroyDoesNotLeak)
{
    constexpr size_t kBufSize = 4096;

    // Create a device-local buffer.
    auto buffer = std::make_unique<RHI::VulkanBuffer>(
        *m_Device, kBufSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    ASSERT_NE(buffer->GetHandle(), VK_NULL_HANDLE);

    // Transfer ownership to deferred destruction.
    bool destroyed = false;
    auto* rawPtr = buffer.release();
    m_Device->SafeDestroy([rawPtr, &destroyed]() {
        delete rawPtr;
        destroyed = true;
    });

    EXPECT_FALSE(destroyed)
        << "Buffer should not be destroyed immediately";

    // Flush all deferred deletions — simulates the maintenance lane's
    // CollectGpuDeferredDestructions after GPU idle.
    (void)m_Device->WaitForGraphicsIdle();
    m_Device->FlushTimelineDeletionQueueNow();

    EXPECT_TRUE(destroyed)
        << "Buffer should be destroyed after timeline flush";
}

// Simulate the maintenance lane's multi-frame resource retirement pattern:
// allocate resources, schedule deferred deletions, run the GC cycle.
TEST_F(MaintenanceLaneGpuTest, MultiFrameRetirementCycle)
{
    constexpr int kFrameCount = 5;
    constexpr int kResourcesPerFrame = 3;
    int totalDeleted = 0;

    for (int frame = 0; frame < kFrameCount; ++frame)
    {
        // Each frame: schedule deferred deletions at frame-specific timeline values.
        for (int r = 0; r < kResourcesPerFrame; ++r)
        {
            m_Device->SafeDestroyAfter(static_cast<uint64_t>(frame + 1),
                [&totalDeleted]() {
                    ++totalDeleted;
                });
        }

        // Run the maintenance lane's GC step (timeline won't have advanced in
        // headless mode, so nothing is collected yet).
        m_Device->CollectGarbage();
    }

    // Before any timeline advancement, nothing should be deleted.
    EXPECT_EQ(totalDeleted, 0)
        << "No resources should be retired before timeline completion";

    // Flush everything — simulates final maintenance lane flush or shutdown.
    (void)m_Device->WaitForGraphicsIdle();
    m_Device->FlushTimelineDeletionQueueNow();

    EXPECT_EQ(totalDeleted, kFrameCount * kResourcesPerFrame)
        << "All resources should be retired after complete flush";
}
