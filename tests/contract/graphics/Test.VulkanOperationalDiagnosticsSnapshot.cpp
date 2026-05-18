#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Backend.Null;
import Extrinsic.RHI.Device;

namespace
{

using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
using Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::kVulkanOperationalReasonCount;
using Extrinsic::Backends::Vulkan::NoteVulkanOperationalDeviceLostDrop;
using Extrinsic::Backends::Vulkan::RecordVulkanOperationalFallback;
using Extrinsic::Backends::Vulkan::VulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::VulkanOperationalReason;
using Extrinsic::Backends::Vulkan::VulkanOperationalStatus;
using Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode;

// Truth-table row: (status, expected reason histogram bucket, which counters
// each row must advance besides the fallback total).
struct TruthTableRow
{
    const char*                 Name;
    VulkanOperationalStatusCode Code;
    VulkanOperationalReason     Reason;
    bool                        BumpsInit;
    bool                        BumpsValidation;
    bool                        BumpsIncompleteGate;
};

constexpr std::array<TruthTableRow, 5> kTruthTableFallbackRows{{
    {"NotCompiled",                  VulkanOperationalStatusCode::NotCompiled,
                                     VulkanOperationalReason::None,
                                     false, false, false},
    {"RequestedButUnsupported",      VulkanOperationalStatusCode::RequestedButUnsupported,
                                     VulkanOperationalReason::MissingInstance,
                                     false, false, false},
    {"RequestedButFailedInit",       VulkanOperationalStatusCode::RequestedButFailedInit,
                                     VulkanOperationalReason::LogicalDeviceFailed,
                                     true,  false, false},
    {"RequestedButValidationFailed", VulkanOperationalStatusCode::RequestedButValidationFailed,
                                     VulkanOperationalReason::ValidationLayerError,
                                     false, true,  false},
    {"RequestedButIncompleteGate",   VulkanOperationalStatusCode::RequestedButIncompleteGate,
                                     VulkanOperationalReason::MinimalRecipeRecordingMissing,
                                     false, false, true },
}};

} // namespace

// -----------------------------------------------------------------------------
// Every truth-table row that lands the runtime on the Null device bumps the
// aggregate fallback counter, the matching reason histogram bucket, and the
// path-specific counter for its status code. Rows that resolve to operational
// or not-requested do not touch the snapshot.
// -----------------------------------------------------------------------------
TEST(VulkanOperationalDiagnosticsSnapshot, FallbackRowsBumpExpectedCounters)
{
    for (const TruthTableRow& row : kTruthTableFallbackRows)
    {
        const VulkanOperationalDiagnosticsSnapshot before =
            GetVulkanOperationalDiagnosticsSnapshot();

        RecordVulkanOperationalFallback(VulkanOperationalStatus{row.Code, row.Reason});

        const VulkanOperationalDiagnosticsSnapshot after =
            GetVulkanOperationalDiagnosticsSnapshot();

        EXPECT_EQ(after.VulkanFallbackToNullCount - before.VulkanFallbackToNullCount,
                  1u) << "row=" << row.Name;
        EXPECT_EQ(after.VulkanInitFailureCount - before.VulkanInitFailureCount,
                  row.BumpsInit ? 1u : 0u) << "row=" << row.Name;
        EXPECT_EQ(after.VulkanValidationErrorCount - before.VulkanValidationErrorCount,
                  row.BumpsValidation ? 1u : 0u) << "row=" << row.Name;
        EXPECT_EQ(after.VulkanOperationalGateFailureCount - before.VulkanOperationalGateFailureCount,
                  row.BumpsIncompleteGate ? 1u : 0u) << "row=" << row.Name;

        const auto reasonIndex = static_cast<std::size_t>(row.Reason);
        ASSERT_LT(reasonIndex, kVulkanOperationalReasonCount);
        EXPECT_EQ(after.ReasonHistogram[reasonIndex] - before.ReasonHistogram[reasonIndex],
                  1u) << "row=" << row.Name;
    }
}

TEST(VulkanOperationalDiagnosticsSnapshot, OperationalAndNotRequestedAreNoOps)
{
    const VulkanOperationalDiagnosticsSnapshot before =
        GetVulkanOperationalDiagnosticsSnapshot();

    RecordVulkanOperationalFallback({VulkanOperationalStatusCode::Operational,
                                     VulkanOperationalReason::None});
    RecordVulkanOperationalFallback({VulkanOperationalStatusCode::NotRequested,
                                     VulkanOperationalReason::None});

    const VulkanOperationalDiagnosticsSnapshot after =
        GetVulkanOperationalDiagnosticsSnapshot();

    EXPECT_EQ(after.VulkanFallbackToNullCount, before.VulkanFallbackToNullCount);
    EXPECT_EQ(after.VulkanInitFailureCount, before.VulkanInitFailureCount);
    EXPECT_EQ(after.VulkanValidationErrorCount, before.VulkanValidationErrorCount);
    EXPECT_EQ(after.VulkanOperationalGateFailureCount,
              before.VulkanOperationalGateFailureCount);
    EXPECT_EQ(after.VulkanDeviceLostOperationalDropCount,
              before.VulkanDeviceLostOperationalDropCount);
    for (std::size_t i = 0; i < kVulkanOperationalReasonCount; ++i)
    {
        EXPECT_EQ(after.ReasonHistogram[i], before.ReasonHistogram[i]) << "i=" << i;
    }
}

TEST(VulkanOperationalDiagnosticsSnapshot, DeviceLostOperationalDropBumpsCounter)
{
    const VulkanOperationalDiagnosticsSnapshot before =
        GetVulkanOperationalDiagnosticsSnapshot();

    NoteVulkanOperationalDeviceLostDrop();
    NoteVulkanOperationalDeviceLostDrop();

    const VulkanOperationalDiagnosticsSnapshot after =
        GetVulkanOperationalDiagnosticsSnapshot();

    EXPECT_EQ(after.VulkanDeviceLostOperationalDropCount -
                  before.VulkanDeviceLostOperationalDropCount,
              2u);
    EXPECT_EQ(after.VulkanFallbackToNullCount, before.VulkanFallbackToNullCount);
}

// -----------------------------------------------------------------------------
// Counters never reset across Initialize/Shutdown cycles, matching the
// process-monotonic semantics already locked in for the FallbackDiagnostics
// counters by VulkanFailClosedContract.
// -----------------------------------------------------------------------------
TEST(VulkanOperationalDiagnosticsSnapshot, CountersSurviveDeviceInitializeShutdownCycles)
{
    const VulkanOperationalDiagnosticsSnapshot before =
        GetVulkanOperationalDiagnosticsSnapshot();

    Extrinsic::Core::Config::WindowConfig windowConfig{};
    Extrinsic::Core::Config::RenderConfig renderConfig{};
    renderConfig.EnablePromotedVulkanDevice = true;
    renderConfig.EnableValidation = false;

    constexpr std::uint64_t kCycles = 3u;
    for (std::uint64_t i = 0; i < kCycles; ++i)
    {
        Extrinsic::Platform::Backends::Null::NullWindow window{windowConfig};
        std::unique_ptr<Extrinsic::RHI::IDevice> device =
            Extrinsic::Backends::Vulkan::CreateVulkanDevice();
        ASSERT_NE(device, nullptr);

        device->Initialize(Extrinsic::RHI::DeviceCreateDesc{
            .RenderConfig             = renderConfig,
            .InitialFramebufferExtent = window.GetFramebufferExtent(),
            .NativeWindowHandle       = window.GetNativeHandle(),
        });
        ASSERT_FALSE(device->IsOperational());

        // The runtime breadcrumb call site would emit one fallback record per
        // startup; mirror that here so the cycle covers the same path.
        const VulkanOperationalStatus status =
            EvaluateVulkanDeviceOperationalStatus(device.get());
        EXPECT_NE(status.Code, VulkanOperationalStatusCode::Operational);
        RecordVulkanOperationalFallback(status);

        device->Shutdown();
    }

    const VulkanOperationalDiagnosticsSnapshot after =
        GetVulkanOperationalDiagnosticsSnapshot();

    EXPECT_EQ(after.VulkanFallbackToNullCount - before.VulkanFallbackToNullCount,
              kCycles);
    // Each cycle's evaluator returns the same status because the bring-up
    // never advances past the host-support gates with a null window; the
    // counter total must equal the number of cycles regardless of which
    // bucket the histogram lands in.
}

TEST(VulkanOperationalDiagnosticsSnapshot, EvaluateDeviceReturnsNotCompiledForNullDevicePointer)
{
    const VulkanOperationalStatus status = EvaluateVulkanDeviceOperationalStatus(nullptr);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::NotCompiled);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::None);
}
