module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

export module Extrinsic.Backends.Vulkan:OperationalStatus;

import Extrinsic.RHI.Device;

namespace Extrinsic::Backends::Vulkan
{
    // -------------------------------------------------------------------------
    // GRAPHICS-033A: operational-status evaluator surface.
    //
    // Single source of truth for whether the promoted Vulkan backend is
    // operational. Renderer/runtime code must keep branching on
    // `IDevice::IsOperational()`; this evaluator backs that predicate and
    // exposes a CPU-public reason taxonomy for diagnostics + the runtime
    // reconciliation truth table documented in `src/graphics/vulkan/README.md`.
    //
    // `VulkanOperationalInputs` carries only backend-public booleans (no
    // `Vk*` handles) so the evaluator is pure, total, and unit-testable
    // without instantiating a Vulkan device. Backend code adapts live
    // `VulkanDevice` state into this struct; future status/reason values
    // append to the enums rather than rewriting existing entries.
    //
    // These types live in a dedicated partition so the `:Device` partition
    // can name `VulkanOperationalInputs` as the return type of
    // `VulkanDevice::BuildOperationalInputs()` without depending on the
    // umbrella module's primary translation unit. Consumers that
    // `import Extrinsic.Backends.Vulkan` still see every symbol because the
    // umbrella re-exports this partition with `export import :OperationalStatus`.
    // -------------------------------------------------------------------------
    export enum class VulkanOperationalStatusCode : std::uint8_t
    {
        NotCompiled                 = 0,
        NotRequested                = 1,
        RequestedButUnsupported     = 2,
        RequestedButFailedInit      = 3,
        RequestedButValidationFailed = 4,
        RequestedButIncompleteGate  = 5,
        Operational                 = 6,
    };

    export enum class VulkanOperationalReason : std::uint8_t
    {
        None                            = 0,
        MissingInstance                 = 1,
        MissingSurface                  = 2,
        NoSuitablePhysicalDevice        = 3,
        MissingRequiredExtension        = 4,
        MissingRequiredFeature          = 5,
        LogicalDeviceFailed             = 6,
        AllocatorFailed                 = 7,
        SwapchainFailed                 = 8,
        CommandSyncFailed               = 9,
        DefaultRecipeRecordingMissing   = 10,
        BarrierValidationFailed         = 11,
        PublicServiceReconciliationFailed = 12,
        ValidationLayerError            = 13,
        DeviceLost                      = 14,
        SurfaceLost                     = 15,
    };

    // CPU-public, non-native boolean inputs to the operational-status gate.
    // Each `Has*`/`*Clean`/`*Ready` field corresponds to one step of the
    // 9-step gate documented in `src/graphics/vulkan/README.md`. Lifecycle
    // loss states (`DeviceLost`, `SurfaceLost`) are checked first because
    // they fail-close even after a previously successful bring-up.
    export struct VulkanOperationalInputs
    {
        // Build-time gate (step 1).
        bool CompiledIn                       = false;
        bool Requested                        = false;

        // Host support (step 2/3, pre-init capability checks).
        bool HostSupportsRequiredInstance     = false;
        bool HostSupportsRequiredSurface      = false;
        bool HostSupportsPhysicalDevice       = false;
        bool HostSupportsRequiredExtensions   = false;
        bool HostSupportsRequiredFeatures     = false;

        // Live bring-up (steps 3-5: logical device, allocator, swapchain,
        // command pools + per-frame sync).
        bool LogicalDeviceReady               = false;
        bool AllocatorReady                   = false;
        bool SwapchainReady                   = false;
        bool CommandSyncReady                 = false;

        // Higher gates (steps 6-8).
        bool DefaultRecipeRecordingPresent    = false;
        bool BarrierValidationClean           = false;
        bool PublicServiceReconciled          = false;

        // Validation layer (step 9, fail-closed when validation errors fire).
        bool ValidationClean                  = true;

        // Post-init lifecycle loss; these short-circuit to fail-closed
        // regardless of how far bring-up previously progressed.
        bool DeviceLost                       = false;
        bool SurfaceLost                      = false;
    };

    export struct VulkanOperationalStatus
    {
        VulkanOperationalStatusCode Code   = VulkanOperationalStatusCode::NotCompiled;
        VulkanOperationalReason     Reason = VulkanOperationalReason::None;
    };

    // Total, pure evaluator. The README locks the gate order; this function
    // returns the first failing gate's reason paired with the appropriate
    // status code. Only when every gate passes does it return
    // `{Operational, None}`. Append-only on both enums.
    export [[nodiscard]] VulkanOperationalStatus
    EvaluateVulkanOperationalStatus(const VulkanOperationalInputs& inputs) noexcept;

    // Stable string names for diagnostics consumers (logs, breadcrumbs, tests).
    // Append-only and matched 1:1 with the enum values; deliberately not
    // localized.
    export [[nodiscard]] std::string_view ToString(VulkanOperationalStatusCode code) noexcept;
    export [[nodiscard]] std::string_view ToString(VulkanOperationalReason reason) noexcept;

    // -------------------------------------------------------------------------
    // GRAPHICS-033B: process-monotonic operational diagnostics snapshot.
    //
    // Counters are bumped by `RecordVulkanOperationalFallback()` at the
    // runtime startup reconciliation point (one call per `Engine::Initialize`
    // on a non-operational requested-Vulkan device) and by
    // `NoteVulkanOperationalDeviceLostDrop()` when a previously operational
    // device transitions to non-operational because of `VK_ERROR_DEVICE_LOST`.
    // They are never reset across `Initialize`/`Shutdown` cycles, mirroring
    // the existing `FallbackDiagnosticsSnapshot` semantics. Renderer/runtime
    // code keeps branching only on `IDevice::IsOperational()`; this surface
    // is diagnostics-only.
    // -------------------------------------------------------------------------
    export inline constexpr std::size_t kVulkanOperationalReasonCount = 16;

    export struct VulkanOperationalDiagnosticsSnapshot
    {
        std::uint64_t VulkanFallbackToNullCount             = 0;
        std::uint64_t VulkanInitFailureCount                = 0;
        std::uint64_t VulkanValidationErrorCount            = 0;
        std::uint64_t VulkanOperationalGateFailureCount     = 0;
        std::uint64_t VulkanDeviceLostOperationalDropCount  = 0;
        std::array<std::uint32_t, kVulkanOperationalReasonCount> ReasonHistogram{};
    };

    export [[nodiscard]] VulkanOperationalDiagnosticsSnapshot
    GetVulkanOperationalDiagnosticsSnapshot() noexcept;

    // Bumps `VulkanFallbackToNullCount`, the matching `ReasonHistogram`
    // bucket, and the path-specific counter for `status.Code`:
    //   - `RequestedButFailedInit`       -> `VulkanInitFailureCount`
    //   - `RequestedButValidationFailed` -> `VulkanValidationErrorCount`
    //   - `RequestedButIncompleteGate`   -> `VulkanOperationalGateFailureCount`
    //   - `NotCompiled`, `RequestedButUnsupported` -> fallback + histogram only
    // No-op when `status.Code` is `Operational` or `NotRequested` (runtime
    // never observes a fallback in those rows of the truth table).
    export void RecordVulkanOperationalFallback(VulkanOperationalStatus status) noexcept;

    // Bumps `VulkanDeviceLostOperationalDropCount`. Called by the Vulkan
    // backend when `VK_ERROR_DEVICE_LOST` drops a previously operational
    // device to non-operational. Process-monotonic.
    export void NoteVulkanOperationalDeviceLostDrop() noexcept;

    // Re-evaluates the operational status of `device`, which must have been
    // produced by `CreateVulkanDevice()`. The runtime startup breadcrumb path
    // is the only consumer; behavior is undefined for other `IDevice`
    // implementations. Returns `{NotCompiled, None}` for a null device.
    export [[nodiscard]] VulkanOperationalStatus
    EvaluateVulkanDeviceOperationalStatus(const RHI::IDevice* device) noexcept;

    // GRAPHICS-033E: backend-public snapshot of the live `VulkanOperationalInputs`
    // for `device`, which must have been produced by `CreateVulkanDevice()`.
    // Behavior is undefined for other `IDevice` implementations. Returns a
    // zero-initialized struct for a null device. Used by contract tests to
    // observe the publish-side state of individual gate inputs (notably
    // `BarrierValidationClean`) without re-running the evaluator.
    export [[nodiscard]] VulkanOperationalInputs
    GetVulkanDeviceOperationalInputs(const RHI::IDevice* device) noexcept;
}
