module;
#include <cstdint>
#include <memory>

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
    // are kept stable so opt-in Vulkan smoke tests can lock future operational
    // acquire/submit/present/resize semantics without making Vulkan mandatory in
    // the default CPU gate.
    export enum class VulkanFrameBeginStatus : std::uint8_t
    {
        NotStarted = 0,
        SkippedNotOperational = 1,
        SkippedNoSwapchain = 2,
        SkippedNoSwapchainImages = 3,
        Acquired = 4,
        FailedAcquire = 5,
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
    };

    export [[nodiscard]] VulkanFrameLifecycleDiagnosticsSnapshot
    GetVulkanFrameLifecycleDiagnosticsSnapshot() noexcept;

    // Backend-local service handoff diagnostics for live Vulkan service objects
    // created after guarded bootstrap. These diagnostics are not an RHI branch
    // seam; public GetBindlessHeap()/GetTransferQueue() accessors remain
    // fail-closed until IDevice::IsOperational() becomes true.
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
        bool PublicServicesRemainFailClosed = true;
    };

    export [[nodiscard]] VulkanServiceDiagnosticsSnapshot GetVulkanServiceDiagnosticsSnapshot() noexcept;

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

    // Structured reason for the most recent fail-closed CreatePipeline call.
    // Exposed for CPU diagnostics that need to distinguish "device or layout
    // not yet brought up" from "operational guard reached but shader/pipeline
    // construction is still unimplemented". Bindless and transfer-queue
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
        ShaderMissing = 2, // operational guard reached but shader/pipeline construction unimplemented
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
}

