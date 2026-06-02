module;

#include <string_view>

module Extrinsic.Backends.Vulkan;

namespace Extrinsic::Backends::Vulkan
{

// =============================================================================
// GRAPHICS-033A — EvaluateVulkanOperationalStatus
//
// The gate order below mirrors the 9-step checklist in
// `src/graphics/vulkan/README.md`. Each gate returns the first failing
// reason paired with the status code that captures *why* it failed:
//
//   1. Build/run gate            -> NotCompiled / NotRequested
//   2. Host instance/surface     -> RequestedButUnsupported
//   3. Host phys-dev/ext/feat    -> RequestedButUnsupported
//   4. Logical device + features -> RequestedButFailedInit
//   5. Allocator                 -> RequestedButFailedInit
//   6. Swapchain                 -> RequestedButFailedInit
//   7. Command/sync              -> RequestedButFailedInit
//   8. Minimal recipe recording  -> RequestedButIncompleteGate
//   9. Barrier validation        -> RequestedButIncompleteGate
//  10. Public service reconciled -> RequestedButIncompleteGate
//  11. Validation-layer clean    -> RequestedButValidationFailed
//
// Lifecycle loss (DeviceLost, SurfaceLost) is checked first so a previously
// successful bring-up still resolves fail-closed once the runtime observes a
// loss event.
// =============================================================================
VulkanOperationalStatus EvaluateVulkanOperationalStatus(
    const VulkanOperationalInputs& inputs) noexcept
{
    // 1. Build/run gate.
    if (!inputs.CompiledIn)
    {
        if (inputs.Requested)
            return {VulkanOperationalStatusCode::NotCompiled, VulkanOperationalReason::None};
        return {VulkanOperationalStatusCode::NotRequested, VulkanOperationalReason::None};
    }
    if (!inputs.Requested)
        return {VulkanOperationalStatusCode::NotRequested, VulkanOperationalReason::None};

    // Lifecycle loss takes precedence over forward-progress gates: a lost
    // device or surface fails the runtime reconciliation truth table even if
    // every other input is still true.
    if (inputs.SurfaceLost)
        return {VulkanOperationalStatusCode::RequestedButFailedInit,
                VulkanOperationalReason::SurfaceLost};
    if (inputs.DeviceLost)
        return {VulkanOperationalStatusCode::RequestedButFailedInit,
                VulkanOperationalReason::DeviceLost};

    // 2-3. Host support (pre-init capability gates).
    if (!inputs.HostSupportsRequiredInstance)
        return {VulkanOperationalStatusCode::RequestedButUnsupported,
                VulkanOperationalReason::MissingInstance};
    if (!inputs.HostSupportsRequiredSurface)
        return {VulkanOperationalStatusCode::RequestedButUnsupported,
                VulkanOperationalReason::MissingSurface};
    if (!inputs.HostSupportsPhysicalDevice)
        return {VulkanOperationalStatusCode::RequestedButUnsupported,
                VulkanOperationalReason::NoSuitablePhysicalDevice};
    if (!inputs.HostSupportsRequiredExtensions)
        return {VulkanOperationalStatusCode::RequestedButUnsupported,
                VulkanOperationalReason::MissingRequiredExtension};
    if (!inputs.HostSupportsRequiredFeatures)
        return {VulkanOperationalStatusCode::RequestedButUnsupported,
                VulkanOperationalReason::MissingRequiredFeature};

    // 4-7. Live bring-up.
    if (!inputs.LogicalDeviceReady)
        return {VulkanOperationalStatusCode::RequestedButFailedInit,
                VulkanOperationalReason::LogicalDeviceFailed};
    if (!inputs.AllocatorReady)
        return {VulkanOperationalStatusCode::RequestedButFailedInit,
                VulkanOperationalReason::AllocatorFailed};
    if (!inputs.SwapchainReady)
        return {VulkanOperationalStatusCode::RequestedButFailedInit,
                VulkanOperationalReason::SwapchainFailed};
    if (!inputs.CommandSyncReady)
        return {VulkanOperationalStatusCode::RequestedButFailedInit,
                VulkanOperationalReason::CommandSyncFailed};

    // 9. Validation-layer policy fails closed before incomplete-gate reasons
    // so a validation error is reported even when later gates are still
    // unfinished: validation errors during the gate must keep the device
    // non-operational per the truth table.
    if (!inputs.ValidationClean)
        return {VulkanOperationalStatusCode::RequestedButValidationFailed,
                VulkanOperationalReason::ValidationLayerError};

    // 8. Higher-level operational completeness (incomplete-gate reasons).
    if (!inputs.DefaultRecipeRecordingPresent)
        return {VulkanOperationalStatusCode::RequestedButIncompleteGate,
                VulkanOperationalReason::DefaultRecipeRecordingMissing};
    if (!inputs.BarrierValidationClean)
        return {VulkanOperationalStatusCode::RequestedButIncompleteGate,
                VulkanOperationalReason::BarrierValidationFailed};
    if (!inputs.PublicServiceReconciled)
        return {VulkanOperationalStatusCode::RequestedButIncompleteGate,
                VulkanOperationalReason::PublicServiceReconciliationFailed};

    return {VulkanOperationalStatusCode::Operational, VulkanOperationalReason::None};
}

std::string_view ToString(const VulkanOperationalStatusCode code) noexcept
{
    switch (code)
    {
    case VulkanOperationalStatusCode::NotCompiled:                 return "NotCompiled";
    case VulkanOperationalStatusCode::NotRequested:                return "NotRequested";
    case VulkanOperationalStatusCode::RequestedButUnsupported:     return "RequestedButUnsupported";
    case VulkanOperationalStatusCode::RequestedButFailedInit:      return "RequestedButFailedInit";
    case VulkanOperationalStatusCode::RequestedButValidationFailed:return "RequestedButValidationFailed";
    case VulkanOperationalStatusCode::RequestedButIncompleteGate:  return "RequestedButIncompleteGate";
    case VulkanOperationalStatusCode::Operational:                 return "Operational";
    }
    return "Unknown";
}

std::string_view ToString(const VulkanOperationalReason reason) noexcept
{
    switch (reason)
    {
    case VulkanOperationalReason::None:                              return "None";
    case VulkanOperationalReason::MissingInstance:                   return "MissingInstance";
    case VulkanOperationalReason::MissingSurface:                    return "MissingSurface";
    case VulkanOperationalReason::NoSuitablePhysicalDevice:          return "NoSuitablePhysicalDevice";
    case VulkanOperationalReason::MissingRequiredExtension:          return "MissingRequiredExtension";
    case VulkanOperationalReason::MissingRequiredFeature:            return "MissingRequiredFeature";
    case VulkanOperationalReason::LogicalDeviceFailed:               return "LogicalDeviceFailed";
    case VulkanOperationalReason::AllocatorFailed:                   return "AllocatorFailed";
    case VulkanOperationalReason::SwapchainFailed:                   return "SwapchainFailed";
    case VulkanOperationalReason::CommandSyncFailed:                 return "CommandSyncFailed";
    case VulkanOperationalReason::DefaultRecipeRecordingMissing:     return "DefaultRecipeRecordingMissing";
    case VulkanOperationalReason::BarrierValidationFailed:           return "BarrierValidationFailed";
    case VulkanOperationalReason::PublicServiceReconciliationFailed: return "PublicServiceReconciliationFailed";
    case VulkanOperationalReason::ValidationLayerError:              return "ValidationLayerError";
    case VulkanOperationalReason::DeviceLost:                        return "DeviceLost";
    case VulkanOperationalReason::SurfaceLost:                       return "SurfaceLost";
    }
    return "Unknown";
}

} // namespace Extrinsic::Backends::Vulkan
