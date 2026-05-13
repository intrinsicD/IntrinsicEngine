#include <gtest/gtest.h>

#include <array>
#include <cstdint>

import Extrinsic.Backends.Vulkan;

namespace
{

using Extrinsic::Backends::Vulkan::EvaluateVulkanOperationalStatus;
using Extrinsic::Backends::Vulkan::VulkanOperationalInputs;
using Extrinsic::Backends::Vulkan::VulkanOperationalReason;
using Extrinsic::Backends::Vulkan::VulkanOperationalStatus;
using Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode;

// All host-support, live bring-up, higher-gate, and validation inputs flipped
// to the values that satisfy the full 9-step gate. Lifecycle loss stays false
// so the evaluator returns `Operational` from this baseline.
constexpr VulkanOperationalInputs MakeOperationalInputs() noexcept
{
    VulkanOperationalInputs inputs{};
    inputs.CompiledIn                       = true;
    inputs.Requested                        = true;
    inputs.HostSupportsRequiredInstance     = true;
    inputs.HostSupportsRequiredSurface      = true;
    inputs.HostSupportsPhysicalDevice       = true;
    inputs.HostSupportsRequiredExtensions   = true;
    inputs.HostSupportsRequiredFeatures     = true;
    inputs.LogicalDeviceReady               = true;
    inputs.AllocatorReady                   = true;
    inputs.SwapchainReady                   = true;
    inputs.CommandSyncReady                 = true;
    inputs.MinimalRecipeRecordingPresent    = true;
    inputs.BarrierValidationClean           = true;
    inputs.PublicServiceReconciled          = true;
    inputs.ValidationClean                  = true;
    inputs.DeviceLost                       = false;
    inputs.SurfaceLost                      = false;
    return inputs;
}

} // namespace

// -----------------------------------------------------------------------------
// Build-time gate: NotCompiled vs NotRequested.
// -----------------------------------------------------------------------------
TEST(VulkanOperationalStatusEvaluator, NotCompiledWhenRequestedButBackendAbsent)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.CompiledIn = false;
    inputs.Requested  = true;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::NotCompiled);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::None);
}

TEST(VulkanOperationalStatusEvaluator, NotRequestedWhenBackendAbsentAndNotRequested)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.CompiledIn = false;
    inputs.Requested  = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::NotRequested);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::None);
}

TEST(VulkanOperationalStatusEvaluator, NotRequestedWhenBackendCompiledButNotRequested)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.Requested = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::NotRequested);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::None);
}

// -----------------------------------------------------------------------------
// Host-support gates → RequestedButUnsupported with the documented reason.
// -----------------------------------------------------------------------------
TEST(VulkanOperationalStatusEvaluator, MissingInstanceReportsMissingInstance)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.HostSupportsRequiredInstance = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButUnsupported);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::MissingInstance);
}

TEST(VulkanOperationalStatusEvaluator, MissingSurfaceReportsMissingSurface)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.HostSupportsRequiredSurface = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButUnsupported);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::MissingSurface);
}

TEST(VulkanOperationalStatusEvaluator, NoPhysicalDeviceReportsNoSuitablePhysicalDevice)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.HostSupportsPhysicalDevice = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButUnsupported);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::NoSuitablePhysicalDevice);
}

TEST(VulkanOperationalStatusEvaluator, MissingExtensionReportsMissingRequiredExtension)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.HostSupportsRequiredExtensions = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButUnsupported);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::MissingRequiredExtension);
}

TEST(VulkanOperationalStatusEvaluator, MissingFeatureReportsMissingRequiredFeature)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.HostSupportsRequiredFeatures = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButUnsupported);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::MissingRequiredFeature);
}

// -----------------------------------------------------------------------------
// Live bring-up gates → RequestedButFailedInit with the documented reason.
// -----------------------------------------------------------------------------
TEST(VulkanOperationalStatusEvaluator, LogicalDeviceNotReadyReportsLogicalDeviceFailed)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.LogicalDeviceReady = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButFailedInit);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::LogicalDeviceFailed);
}

TEST(VulkanOperationalStatusEvaluator, AllocatorNotReadyReportsAllocatorFailed)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.AllocatorReady = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButFailedInit);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::AllocatorFailed);
}

TEST(VulkanOperationalStatusEvaluator, SwapchainNotReadyReportsSwapchainFailed)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.SwapchainReady = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButFailedInit);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::SwapchainFailed);
}

TEST(VulkanOperationalStatusEvaluator, CommandSyncNotReadyReportsCommandSyncFailed)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.CommandSyncReady = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButFailedInit);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::CommandSyncFailed);
}

// -----------------------------------------------------------------------------
// Higher gates → RequestedButIncompleteGate with the documented reason.
// -----------------------------------------------------------------------------
TEST(VulkanOperationalStatusEvaluator, MissingRecipeRecordingReportsIncompleteGate)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.MinimalRecipeRecordingPresent = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButIncompleteGate);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::MinimalRecipeRecordingMissing);
}

TEST(VulkanOperationalStatusEvaluator, BarrierValidationDirtyReportsBarrierValidationFailed)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.BarrierValidationClean = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButIncompleteGate);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::BarrierValidationFailed);
}

TEST(VulkanOperationalStatusEvaluator,
     PublicServiceUnreconciledReportsPublicServiceReconciliationFailed)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.PublicServiceReconciled = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButIncompleteGate);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::PublicServiceReconciliationFailed);
}

// -----------------------------------------------------------------------------
// Validation-layer fail-closed gate → RequestedButValidationFailed.
// -----------------------------------------------------------------------------
TEST(VulkanOperationalStatusEvaluator, ValidationDirtyReportsValidationLayerError)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.ValidationClean = false;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButValidationFailed);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::ValidationLayerError);
}

// -----------------------------------------------------------------------------
// Lifecycle loss short-circuits to fail-closed even from an otherwise
// operational input set.
// -----------------------------------------------------------------------------
TEST(VulkanOperationalStatusEvaluator, DeviceLostReportsDeviceLost)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.DeviceLost = true;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButFailedInit);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::DeviceLost);
}

TEST(VulkanOperationalStatusEvaluator, SurfaceLostReportsSurfaceLost)
{
    VulkanOperationalInputs inputs = MakeOperationalInputs();
    inputs.SurfaceLost = true;

    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::RequestedButFailedInit);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::SurfaceLost);
}

// -----------------------------------------------------------------------------
// `Operational` is reachable only when every gate is satisfied, and flipping
// any single gate input back off returns the evaluator to a non-operational
// status with the corresponding reason.
// -----------------------------------------------------------------------------
TEST(VulkanOperationalStatusEvaluator, AllInputsTrueReachesOperational)
{
    const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(MakeOperationalInputs());
    EXPECT_EQ(status.Code, VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(status.Reason, VulkanOperationalReason::None);
}

TEST(VulkanOperationalStatusEvaluator, FlippingAnyGateInputDropsBackToNonOperational)
{
    // (member-name, expected reason) pairs covering every individual gate.
    // Each entry independently exercises the "drop back" transition.
    const auto baseline = MakeOperationalInputs();
    struct Case
    {
        const char*                 Name;
        VulkanOperationalInputs     Inputs;
        VulkanOperationalStatusCode ExpectedCode;
        VulkanOperationalReason     ExpectedReason;
    };

    std::array<Case, 14> cases{{
        {"!CompiledIn,Requested", [&]{auto i=baseline;i.CompiledIn=false;return i;}(),
         VulkanOperationalStatusCode::NotCompiled, VulkanOperationalReason::None},
        {"CompiledIn,!Requested", [&]{auto i=baseline;i.Requested=false;return i;}(),
         VulkanOperationalStatusCode::NotRequested, VulkanOperationalReason::None},
        {"!Instance", [&]{auto i=baseline;i.HostSupportsRequiredInstance=false;return i;}(),
         VulkanOperationalStatusCode::RequestedButUnsupported, VulkanOperationalReason::MissingInstance},
        {"!Surface", [&]{auto i=baseline;i.HostSupportsRequiredSurface=false;return i;}(),
         VulkanOperationalStatusCode::RequestedButUnsupported, VulkanOperationalReason::MissingSurface},
        {"!PhysDev", [&]{auto i=baseline;i.HostSupportsPhysicalDevice=false;return i;}(),
         VulkanOperationalStatusCode::RequestedButUnsupported, VulkanOperationalReason::NoSuitablePhysicalDevice},
        {"!Ext", [&]{auto i=baseline;i.HostSupportsRequiredExtensions=false;return i;}(),
         VulkanOperationalStatusCode::RequestedButUnsupported, VulkanOperationalReason::MissingRequiredExtension},
        {"!Feat", [&]{auto i=baseline;i.HostSupportsRequiredFeatures=false;return i;}(),
         VulkanOperationalStatusCode::RequestedButUnsupported, VulkanOperationalReason::MissingRequiredFeature},
        {"!LogicalDevice", [&]{auto i=baseline;i.LogicalDeviceReady=false;return i;}(),
         VulkanOperationalStatusCode::RequestedButFailedInit, VulkanOperationalReason::LogicalDeviceFailed},
        {"!Allocator", [&]{auto i=baseline;i.AllocatorReady=false;return i;}(),
         VulkanOperationalStatusCode::RequestedButFailedInit, VulkanOperationalReason::AllocatorFailed},
        {"!Swapchain", [&]{auto i=baseline;i.SwapchainReady=false;return i;}(),
         VulkanOperationalStatusCode::RequestedButFailedInit, VulkanOperationalReason::SwapchainFailed},
        {"!CmdSync", [&]{auto i=baseline;i.CommandSyncReady=false;return i;}(),
         VulkanOperationalStatusCode::RequestedButFailedInit, VulkanOperationalReason::CommandSyncFailed},
        {"!Recipe", [&]{auto i=baseline;i.MinimalRecipeRecordingPresent=false;return i;}(),
         VulkanOperationalStatusCode::RequestedButIncompleteGate,
         VulkanOperationalReason::MinimalRecipeRecordingMissing},
        {"!Barrier", [&]{auto i=baseline;i.BarrierValidationClean=false;return i;}(),
         VulkanOperationalStatusCode::RequestedButIncompleteGate,
         VulkanOperationalReason::BarrierValidationFailed},
        {"!PublicService", [&]{auto i=baseline;i.PublicServiceReconciled=false;return i;}(),
         VulkanOperationalStatusCode::RequestedButIncompleteGate,
         VulkanOperationalReason::PublicServiceReconciliationFailed},
    }};

    for (const Case& c : cases)
    {
        const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(c.Inputs);
        EXPECT_EQ(status.Code, c.ExpectedCode) << "case " << c.Name;
        EXPECT_EQ(status.Reason, c.ExpectedReason) << "case " << c.Name;
    }
}

// -----------------------------------------------------------------------------
// Totality: the evaluator is defined for every input bit-pattern of the 17
// boolean fields. Brute-forcing 2^17 inputs is cheap; we assert every result
// falls within the documented enum ranges, with no UB / undefined enum values.
// -----------------------------------------------------------------------------
TEST(VulkanOperationalStatusEvaluator, IsTotalAcrossAllBooleanCombinations)
{
    constexpr int kBits = 17;
    constexpr std::uint32_t kCombinationCount = 1u << kBits;

    for (std::uint32_t mask = 0u; mask < kCombinationCount; ++mask)
    {
        VulkanOperationalInputs inputs{};
        inputs.CompiledIn                       = (mask & (1u << 0))  != 0u;
        inputs.Requested                        = (mask & (1u << 1))  != 0u;
        inputs.HostSupportsRequiredInstance     = (mask & (1u << 2))  != 0u;
        inputs.HostSupportsRequiredSurface      = (mask & (1u << 3))  != 0u;
        inputs.HostSupportsPhysicalDevice       = (mask & (1u << 4))  != 0u;
        inputs.HostSupportsRequiredExtensions   = (mask & (1u << 5))  != 0u;
        inputs.HostSupportsRequiredFeatures     = (mask & (1u << 6))  != 0u;
        inputs.LogicalDeviceReady               = (mask & (1u << 7))  != 0u;
        inputs.AllocatorReady                   = (mask & (1u << 8))  != 0u;
        inputs.SwapchainReady                   = (mask & (1u << 9))  != 0u;
        inputs.CommandSyncReady                 = (mask & (1u << 10)) != 0u;
        inputs.MinimalRecipeRecordingPresent    = (mask & (1u << 11)) != 0u;
        inputs.BarrierValidationClean           = (mask & (1u << 12)) != 0u;
        inputs.PublicServiceReconciled          = (mask & (1u << 13)) != 0u;
        inputs.ValidationClean                  = (mask & (1u << 14)) != 0u;
        inputs.DeviceLost                       = (mask & (1u << 15)) != 0u;
        inputs.SurfaceLost                      = (mask & (1u << 16)) != 0u;

        const VulkanOperationalStatus status = EvaluateVulkanOperationalStatus(inputs);

        const auto codeValue   = static_cast<std::uint8_t>(status.Code);
        const auto reasonValue = static_cast<std::uint8_t>(status.Reason);
        ASSERT_LE(codeValue,
                  static_cast<std::uint8_t>(VulkanOperationalStatusCode::Operational))
            << "mask=" << mask;
        ASSERT_LE(reasonValue,
                  static_cast<std::uint8_t>(VulkanOperationalReason::SurfaceLost))
            << "mask=" << mask;

        // `Operational` carries `None` as its reason; every other code must
        // pair with one of the non-None reasons except `NotCompiled` /
        // `NotRequested` which are intentionally reason-free.
        if (status.Code == VulkanOperationalStatusCode::Operational)
        {
            ASSERT_EQ(status.Reason, VulkanOperationalReason::None) << "mask=" << mask;
        }
        else if (status.Code == VulkanOperationalStatusCode::NotCompiled ||
                 status.Code == VulkanOperationalStatusCode::NotRequested)
        {
            ASSERT_EQ(status.Reason, VulkanOperationalReason::None) << "mask=" << mask;
        }
        else
        {
            ASSERT_NE(status.Reason, VulkanOperationalReason::None) << "mask=" << mask;
        }
    }
}
