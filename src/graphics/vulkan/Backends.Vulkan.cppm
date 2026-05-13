module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

export module Extrinsic.Backends.Vulkan;

import Extrinsic.RHI.Device;

namespace Extrinsic::Backends::Vulkan
{
    // Factory — creates the Vulkan backend IDevice.
    // Call once during engine initialization; the returned device owns all
    // Vulkan state.  Destroy it before the window is destroyed.
    export std::unique_ptr<RHI::IDevice> CreateVulkanDevice();

    // CPU-visible status for the most recent promoted Vulkan bootstrap attempt.
    // This is backend-specific diagnostics, not an RHI contract: renderer/runtime
    // code must continue to use IDevice::IsOperational() and must not branch on
    // these values. The snapshot intentionally avoids Vulkan-native types so the
    // umbrella module does not expose Vk* handles or enums.
    export enum class VulkanBootstrapStatus : std::uint8_t
    {
        NotStarted = 0,
        SkippedNoNativeWindow = 1,
        FailedVolkInitialize = 2,
        FailedRequiredInstanceExtensions = 3,
        FailedInstanceCreation = 4,
        FailedSurfaceCreation = 5,
        FailedPhysicalDeviceEnumeration = 6,
        FailedNoSuitablePhysicalDevice = 7,
        ProbedPhysicalDevice = 8,
        CreatedLogicalDevice = 9,
        FailedLogicalDeviceCreation = 10,
        CreatedMemoryAllocator = 11,
        FailedMemoryAllocatorCreation = 12,
        CreatedPerFrameResources = 13,
        FailedPerFrameResourceCreation = 14,
        CreatedSwapchain = 15,
        FailedSwapchainCreation = 16,
        FailedSwapchainImageEnumeration = 17,
        FailedSwapchainImageViewCreation = 18,
        RegisteredSwapchainImages = 19,
    };

    export struct VulkanBootstrapDiagnosticsSnapshot
    {
        VulkanBootstrapStatus Status = VulkanBootstrapStatus::NotStarted;
        std::int32_t LastVkResult = 0;
        std::uint32_t RequiredInstanceExtensionCount = 0;
        std::uint32_t PhysicalDeviceCount = 0;
        std::uint32_t GraphicsQueueFamily = 0;
        std::uint32_t PresentQueueFamily = 0;
        std::uint32_t TransferQueueFamily = 0;
        std::uint32_t FrameCommandPoolCount = 0;
        std::uint32_t FrameCommandBufferCount = 0;
        std::uint32_t FrameFenceCount = 0;
        std::uint32_t FrameImageAcquiredSemaphoreCount = 0;
        std::uint32_t FrameRenderDoneSemaphoreCount = 0;
        std::uint32_t SwapchainImageCount = 0;
        std::uint32_t SwapchainImageViewCount = 0;
        std::uint32_t SwapchainImageHandleCount = 0;
        std::uint32_t SwapchainWidth = 0;
        std::uint32_t SwapchainHeight = 0;
        bool NativeWindowAvailable = false;
        bool VolkInitialized = false;
        bool ValidationRequested = false;
        bool ValidationEnabled = false;
        bool DebugUtilsEnabled = false;
        bool InstanceCreated = false;
        bool SurfaceCreated = false;
        bool PhysicalDeviceSelected = false;
        bool LogicalDeviceCreated = false;
        bool GraphicsQueueFound = false;
        bool PresentQueueFound = false;
        bool TransferQueueFound = false;
        bool GraphicsQueueAcquired = false;
        bool PresentQueueAcquired = false;
        bool TransferQueueAcquired = false;
        bool MemoryAllocatorCreated = false;
        bool PerFrameResourcesCreated = false;
        bool SwapchainExtensionSupported = false;
        bool SwapchainSurfaceSupported = false;
        bool SwapchainCreated = false;
        bool SwapchainImagesEnumerated = false;
        bool SwapchainImageViewsCreated = false;
        bool SwapchainImagesRegistered = false;
        bool DescriptorIndexingSupported = false;
        bool TimelineSemaphoreSupported = false;
        bool DynamicRenderingSupported = false;
        bool BufferDeviceAddressSupported = false;
        bool RequiredDeviceFeaturesSupported = false;
        bool DescriptorIndexingEnabled = false;
        bool TimelineSemaphoreEnabled = false;
        bool DynamicRenderingEnabled = false;
        bool BufferDeviceAddressEnabled = false;
    };

    export [[nodiscard]] VulkanBootstrapDiagnosticsSnapshot GetVulkanBootstrapDiagnosticsSnapshot() noexcept;

    // Backend-local lifecycle diagnostics for the most recent promoted Vulkan
    // BeginFrame/EndFrame/Present/Resize attempts. This is intentionally not an
    // RHI contract and exposes no Vulkan-native types; renderer/runtime code
    // must continue to branch only on IDevice::IsOperational(). The enum values
    // are kept stable so opt-in Vulkan smoke tests can lock guarded direct
    // acquire/submit/present semantics and future operational resize/recreate
    // semantics without making Vulkan mandatory in the default CPU gate.
    export enum class VulkanFrameBeginStatus : std::uint8_t
    {
        NotStarted = 0,
        SkippedNotOperational = 1,
        SkippedNoSwapchain = 2,
        SkippedNoSwapchainImages = 3,
        Acquired = 4,
        FailedAcquire = 5,
        Suboptimal = 6,
        OutOfDate = 7,
    };

    export enum class VulkanFrameEndStatus : std::uint8_t
    {
        NotStarted = 0,
        SkippedNotOperational = 1,
        Submitted = 2,
        FailedSubmit = 3,
    };

    export enum class VulkanFramePresentStatus : std::uint8_t
    {
        NotStarted = 0,
        SkippedNotOperational = 1,
        SkippedNoSwapchain = 2,
        Presented = 3,
        Suboptimal = 4,
        OutOfDate = 5,
        FailedPresent = 6,
    };

    export enum class VulkanFrameResizeStatus : std::uint8_t
    {
        NotStarted = 0,
        RecordedPendingNotOperational = 1,
        RecordedPendingNoSwapchain = 2,
        RecordedPendingRecreate = 3,
        Recreated = 4,
        FailedRecreate = 5,
    };

    export struct VulkanFrameLifecycleDiagnosticsSnapshot
    {
        VulkanFrameBeginStatus BeginStatus = VulkanFrameBeginStatus::NotStarted;
        VulkanFrameEndStatus EndStatus = VulkanFrameEndStatus::NotStarted;
        VulkanFramePresentStatus PresentStatus = VulkanFramePresentStatus::NotStarted;
        VulkanFrameResizeStatus ResizeStatus = VulkanFrameResizeStatus::NotStarted;
        std::int32_t LastVkResult = 0;
        std::uint32_t LastFrameIndex = 0;
        std::uint32_t LastSwapchainImageIndex = 0;
        std::uint32_t LastRequestedWidth = 0;
        std::uint32_t LastRequestedHeight = 0;
        std::uint64_t BeginFrameAttempts = 0;
        std::uint64_t EndFrameAttempts = 0;
        std::uint64_t PresentAttempts = 0;
        std::uint64_t ResizeAttempts = 0;
        bool DeviceOperational = false;
        bool SwapchainAvailable = false;
        bool SwapchainImagesAvailable = false;
        bool DeviceLost = false;
        bool ResizePending = false;
    };

    export [[nodiscard]] VulkanFrameLifecycleDiagnosticsSnapshot
    GetVulkanFrameLifecycleDiagnosticsSnapshot() noexcept;

    // Backend-local service handoff diagnostics for live Vulkan service objects
    // created after guarded bootstrap. These diagnostics are not a renderer branch
    // seam. Public bindless access remains fail-closed until the backend-owned
    // IDevice::IsOperational() predicate is true; the public transfer queue may
    // expose the live async upload service once guarded live prerequisites are
    // ready so opt-in upload smoke tests and streaming seams do not need full
    // renderer operational promotion.
    export enum class VulkanServiceBootstrapStatus : std::uint8_t
    {
        NotStarted = 0,
        SkippedNoBootstrap = 1,
        FailedBindlessHeapCreation = 2,
        FailedGlobalPipelineLayoutCreation = 3,
        FailedTransferQueueCreation = 4,
        Ready = 5,
    };

    export struct VulkanServiceDiagnosticsSnapshot
    {
        VulkanServiceBootstrapStatus Status = VulkanServiceBootstrapStatus::NotStarted;
        std::int32_t LastVkResult = 0;
        std::uint32_t BindlessCapacity = 0;
        std::uint32_t CommandContextRebindCount = 0;
        bool BindlessHeapCreated = false;
        bool GlobalPipelineLayoutCreated = false;
        bool TransferQueueCreated = false;
        bool CommandContextsRebound = false;
        bool LiveOperationalPrerequisitesReady = false;
        bool OperationalSafetyPrerequisitesReady = false;
        bool PublicServicesExposed = false;
        bool PublicServicesRemainFailClosed = true;
        bool PublicBindlessHeapExposed = false;
        bool PublicTransferQueueExposed = false;
    };

    export [[nodiscard]] VulkanServiceDiagnosticsSnapshot GetVulkanServiceDiagnosticsSnapshot() noexcept;

    // Backend-local diagnostics for concrete Vulkan pipeline construction. The
    // promoted renderer/RHI path still gates on IDevice::IsOperational(); this
    // snapshot exists so CPU/null contracts can observe fail-closed pre-bring-up
    // attempts and opt-in Vulkan smoke tests can verify direct backend pipeline
    // creation once guarded bootstrap has created a logical device and global
    // pipeline layout. No Vulkan-native types are exposed here.
    export enum class VulkanPipelineCreationStatus : std::uint8_t
    {
        NotStarted = 0,
        SkippedPreBringUp = 1,
        FailedInvalidDescription = 2,
        FailedShaderRead = 3,
        FailedShaderModuleCreation = 4,
        FailedPipelineCreation = 5,
        CreatedGraphics = 6,
        CreatedCompute = 7,
    };

    export struct VulkanPipelineDiagnosticsSnapshot
    {
        VulkanPipelineCreationStatus Status = VulkanPipelineCreationStatus::NotStarted;
        std::int32_t LastVkResult = 0;
        std::uint32_t ColorTargetCount = 0;
        std::uint32_t PushConstantSize = 0;
        std::uint64_t ShaderBytesRead = 0;
        std::uint64_t SuccessfulPipelineCreations = 0;
        bool DeviceAvailable = false;
        bool DeviceOperational = false;
        bool GlobalPipelineLayoutAvailable = false;
        bool ComputePipeline = false;
    };

    export [[nodiscard]] VulkanPipelineDiagnosticsSnapshot GetVulkanPipelineDiagnosticsSnapshot() noexcept;

    // Debug breadcrumb for fail-closed non-operational bindless allocation attempts.
    export std::uint64_t GetFallbackBindlessAllocationAttemptCount() noexcept;

    // Debug breadcrumb for fail-closed non-operational transfer-queue upload attempts.
    // Increments for every UploadBuffer/UploadTexture call serviced by the
    // FallbackTransferQueue while the device is non-operational.
    export std::uint64_t GetFallbackTransferUploadAttemptCount() noexcept;

    // Debug breadcrumb for fail-closed non-operational pipeline creation attempts.
    // Increments for every VulkanDevice::CreatePipeline call that returns an
    // invalid handle because the device is non-operational or the global
    // pipeline layout is not yet created.
    export std::uint64_t GetFallbackPipelineCreationAttemptCount() noexcept;

    // Debug breadcrumb for fail-closed non-operational BeginFrame attempts.
    // Increments for every VulkanDevice::BeginFrame call that returns false
    // because the device is non-operational or the swapchain is not yet
    // brought up. Lets CPU diagnostics catch a runtime/renderer frame loop
    // that is silently driving frames against a fail-closed Vulkan device,
    // mirroring the bindless/transfer/pipeline counters.
    export std::uint64_t GetFallbackBeginFrameAttemptCount() noexcept;

    // Debug breadcrumb for fail-closed non-operational EndFrame attempts.
    // Increments for every VulkanDevice::EndFrame call whose fail-closed
    // early-return path is taken because the device is non-operational and
    // therefore the matching BeginFrame would also have failed. Pairs with
    // GetFallbackBeginFrameAttemptCount() so CPU diagnostics can observe both
    // halves of a fail-closed renderer/runtime frame loop and assert
    // pairing semantics across Begin/End.
    export std::uint64_t GetFallbackEndFrameAttemptCount() noexcept;

    // Debug breadcrumb for fail-closed non-operational Present attempts.
    // Increments for every VulkanDevice::Present call whose fail-closed
    // early-return path is taken because the device/swapchain is not yet
    // operational. Completes CPU-visible diagnostics for the renderer/runtime
    // BeginFrame -> EndFrame -> Present lifecycle sequence.
    export std::uint64_t GetFallbackPresentAttemptCount() noexcept;

    // Debug breadcrumb for fail-closed non-operational Resize attempts.
    // Increments for every VulkanDevice::Resize call made before the device
    // has an operational swapchain. The requested extent is still recorded so
    // CPU/runtime callers can observe deterministic pending-size state.
    export std::uint64_t GetFallbackResizeAttemptCount() noexcept;

    // Debug breadcrumb for fail-closed Vulkan command-context recording
    // attempts. Increments when an unbound or not-begun VulkanCommandContext
    // receives a command-recording call and skips instead of issuing Vulkan
    // commands against null/non-recording state. This remains backend-local
    // diagnostics; renderer/runtime code must still gate on IDevice::IsOperational().
    export std::uint64_t GetFallbackCommandRecordingAttemptCount() noexcept;

    // Structured reason for the most recent fail-closed CreatePipeline call.
    // Exposed for CPU diagnostics that need to distinguish "device or layout
    // not yet brought up" from post-layout shader/description failures.
    // Bindless and transfer-queue
    // fallbacks intentionally do not yet expose a reason enum because each
    // currently has a single fail-closed reason; revisit when a second
    // emerges. The accessor is process-monotonic in the same sense as the
    // counters: it reports the last observed reason and is never reset across
    // Initialize/Shutdown cycles. `None` indicates no fail-closed
    // CreatePipeline call has been observed since process start.
    export enum class FallbackPipelineReason : std::uint8_t
    {
        None = 0,
        PreBringUp = 1,    // device non-operational or global pipeline layout missing
        ShaderMissing = 2, // post-layout pipeline path rejected missing/invalid shader inputs
    };
    export FallbackPipelineReason GetLastFallbackPipelineReason() noexcept;

    // Aggregate snapshot of all fail-closed diagnostic counters plus the last
    // observed pipeline reason. Each field equals the corresponding individual
    // accessor at the moment it is read. The aggregate read is not a tear-free
    // transaction across fields — each field is loaded with relaxed atomics in
    // order (bindless, transfer, pipeline-count, last-pipeline-reason,
    // begin-frame-count, end-frame-count, present-count, resize-count); a
    // concurrent fallback fire on another thread may land between two field
    // loads. CPU contract tests run single-threaded so this is fine, and the
    // ordering is documented so future operational tests do not assume
    // cross-field atomicity.
    export struct FallbackDiagnosticsSnapshot
    {
        std::uint64_t          BindlessAllocationAttempts = 0;
        std::uint64_t          TransferUploadAttempts     = 0;
        std::uint64_t          PipelineCreationAttempts   = 0;
        FallbackPipelineReason LastPipelineReason         = FallbackPipelineReason::None;
        std::uint64_t          BeginFrameAttempts         = 0;
        std::uint64_t          EndFrameAttempts           = 0;
        std::uint64_t          PresentAttempts            = 0;
        std::uint64_t          ResizeAttempts             = 0;
    };
    export [[nodiscard]] FallbackDiagnosticsSnapshot GetFallbackDiagnosticsSnapshot() noexcept;

    // -----------------------------------------------------------------------
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
        MinimalRecipeRecordingMissing   = 10,
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
        bool MinimalRecipeRecordingPresent    = false;
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

    // -----------------------------------------------------------------------
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
    // -----------------------------------------------------------------------
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
}

