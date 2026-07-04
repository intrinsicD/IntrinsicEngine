module;

#include <cstdint>
#include <memory>
#include <span>

export module Extrinsic.RHI.Device;

import Extrinsic.Core.Geometry2D;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.QueueAffinity;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.Bindless;
import Extrinsic.Core.Config.Render;

// ============================================================
// IDevice — the single API-agnostic GPU device interface.
//
// All resource lifetimes are managed through typed handles.
// No Vulkan / OpenGL / DX type ever appears in this interface.
// ============================================================

namespace Extrinsic::RHI
{
    // ============================================================
    // DeviceCreateDesc — backend-neutral seam between runtime and
    // RHI. Runtime fills the native window handle + framebuffer
    // extent from its platform window (see ARCH-005 / WORKSHOP-002);
    // RHI never imports `Extrinsic.Platform.*` to keep
    // `graphics/rhi -> core` clean. Backends that need a window
    // surface cast `NativeWindowHandle` to their platform-native
    // type; backends that don't (e.g. the Null device) use only
    // `InitialFramebufferExtent`.
    // ============================================================
    export struct DeviceCreateDesc
    {
        Core::Config::RenderConfig RenderConfig{};
        Core::Extent2D             InitialFramebufferExtent{};
        void*                      NativeWindowHandle{nullptr};
    };

    export [[nodiscard]] DeviceCreateDesc MakeDeviceCreateDesc(
        const Core::Config::RenderConfig& renderConfig,
        Core::Extent2D initialFramebufferExtent,
        void* nativeWindowHandle) noexcept;

    export struct QueueTimelineWaitDesc
    {
        QueueAffinity Queue = QueueAffinity::Graphics;
        QueueAffinity SignalQueue = QueueAffinity::Graphics;
        std::uint64_t Value = 0;
    };

    export struct QueueTimelineSignalDesc
    {
        QueueAffinity Queue = QueueAffinity::Graphics;
        std::uint64_t Value = 0;
    };

    export struct QueueSubmitBatchDesc
    {
        QueueAffinity Queue = QueueAffinity::Graphics;
        std::span<const QueueTimelineWaitDesc> Waits{};
        std::span<const QueueTimelineSignalDesc> Signals{};
    };

    export struct FrameQueueSubmitPlanDesc
    {
        std::span<const QueueSubmitBatchDesc> Batches{};
    };

    export struct ResourceMemoryRequirements
    {
        std::uint64_t SizeBytes = 0;
        std::uint64_t AlignmentBytes = 0;
        std::uint32_t MemoryTypeBits = 0;
        bool DedicatedAllocationRequired = false;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return SizeBytes != 0u && AlignmentBytes != 0u && MemoryTypeBits != 0u;
        }
    };

    export struct MemoryBlockDesc
    {
        std::uint64_t SizeBytes = 0;
        std::uint32_t MemoryTypeBits = 0;
        const char* DebugName = nullptr;
    };

    export struct MemoryBlockInfo
    {
        std::uint64_t SizeBytes = 0;
        std::uint32_t MemoryTypeBits = 0;
        std::uint32_t SelectedMemoryTypeBit = 0;
        bool IsValid = false;
    };

    export struct PlacedResourceBinding
    {
        MemoryBlockHandle Block{};
        std::uint64_t OffsetBytes = 0;
    };

    export struct PlacedBufferDesc
    {
        BufferDesc Desc{};
        PlacedResourceBinding Placement{};
    };

    export struct PlacedTextureDesc
    {
        TextureDesc Desc{};
        PlacedResourceBinding Placement{};
    };

    export struct PlacedResourceInfo
    {
        MemoryBlockHandle Block{};
        std::uint64_t OffsetBytes = 0;
        std::uint64_t SizeBytes = 0;
        std::uint64_t AlignmentBytes = 0;
        std::uint32_t MemoryTypeBit = 0;
        bool IsPlaced = false;
    };

    export class IDevice
    {
    public:
        virtual ~IDevice() = default;

        // ---- Device lifecycle ----------------------------------------
        virtual void Initialize(const DeviceCreateDesc& desc) = 0;
        virtual void Shutdown()   = 0;
        virtual void WaitIdle()   = 0;

        // ---- Backend status ------------------------------------------
        /// Whether this backend can actually issue GPU work. Real backends
        /// inherit the default true. Stub / null backends override this to
        /// false so callers can short-circuit cleanly without waiting for
        /// per-call failures. Must be stable for the lifetime of the
        /// device (return the same value on every call).
        ///
        /// Upstream managers (BufferManager, TextureManager, etc.) consult
        /// this inside Create() and return Core::ErrorCode::DeviceNotOperational
        /// when false, so no GPU-shaped call touches a stub backend.
        [[nodiscard]] virtual bool IsOperational() const noexcept { return true; }

        // GRAPHICS-033E: the renderer publishes the outcome of the most recent
        // recipe-aware render-graph validation via this setter. Backends that
        // need the bit (Vulkan, for the `BarrierValidationClean` operational
        // gate) override this; the default is a no-op so non-Vulkan backends
        // remain unaffected. Renderer/runtime code keeps branching on
        // `IsOperational()`; this surface exists only to feed the operational
        // gate's input.
        virtual void NoteRecipeGraphValidation(bool /*clean*/) noexcept {}

        // ---- Frame lifecycle -----------------------------------------
        virtual bool BeginFrame(FrameHandle& outFrame)    = 0;
        virtual void EndFrame(const FrameHandle& frame)   = 0;
        virtual void Present(const FrameHandle& frame)    = 0;

        virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;
        [[nodiscard]] virtual Core::Extent2D GetBackbufferExtent() const = 0;
        [[nodiscard]] virtual Format GetBackbufferFormat() const { return Format::RGBA8_UNORM; }

        /// Change swapchain present mode.  Takes effect on the next Resize()
        /// or the next swapchain recreation — no immediate GPU stall.
        virtual void SetPresentMode(PresentMode mode) = 0;
        [[nodiscard]] virtual PresentMode GetPresentMode() const = 0;

        /// Return a TextureHandle for the swapchain image corresponding to this frame.
        /// The handle is valid only for the duration of the frame (between BeginFrame and Present).
        /// Use it in TextureBarrier() and RenderPassDesc::ColorAttachment to target the backbuffer.
        /// The handle is NOT managed by TextureManager — do not call DestroyTexture on it.
        [[nodiscard]] virtual TextureHandle GetBackbufferHandle(const FrameHandle& frame) const = 0;

        // ---- Command context -----------------------------------------
        virtual ICommandContext& GetGraphicsContext(std::uint32_t frameIndex) = 0;

        // ---- Buffer resources ----------------------------------------
        [[nodiscard]] virtual BufferHandle  CreateBuffer(const BufferDesc& desc)  = 0;
        virtual void                        DestroyBuffer(BufferHandle handle)    = 0;

        /// Upload CPU data into the buffer.  offset + size must fit within SizeBytes.
        virtual void WriteBuffer(BufferHandle handle, const void* data,
                                 std::uint64_t size, std::uint64_t offset = 0)    = 0;

        /// GRAPHICS-033D — read back CPU-visible bytes from a buffer. Used by
        /// the opt-in `gpu;vulkan` visible-triangle smoke (and the canonical
        /// GRAPHICS-076/081 default-recipe equivalent once those land) to drain
        /// the destination of a prior `ICommandContext::CopyTextureToBuffer`
        /// into the test's reusable readback harness.
        ///
        /// Contract:
        ///   - Backends MUST `WaitIdle()` (or equivalent fine-grained wait) on
        ///     entry so the copy that produced the bytes has finished.
        ///   - `handle` must reference a buffer created with `HostVisible=true`
        ///     and `BufferUsage::TransferDst`. Backends without host-visible
        ///     support silently no-op (the default body below).
        ///   - `offset + size` must fit within the buffer's SizeBytes; out-of-
        ///     range requests are dropped without writing to `data`.
        ///
        /// The default body is a no-op so the Null backend and CPU contract
        /// mocks remain unchanged; the Vulkan backend overrides it.
        virtual void ReadBuffer(BufferHandle handle, void* data,
                                std::uint64_t size, std::uint64_t offset = 0)
        {
            (void)handle;
            (void)data;
            (void)size;
            (void)offset;
        }

        /// Return the GPU virtual address of a device-local buffer for BDA use.
        /// The buffer must have been created with BufferUsage::Storage.
        /// Returns 0 when BDA is unsupported or the handle is invalid.
        [[nodiscard]] virtual std::uint64_t GetBufferDeviceAddress(BufferHandle handle) const = 0;

        // ---- Texture resources ---------------------------------------
        [[nodiscard]] virtual TextureHandle  CreateTexture(const TextureDesc& desc) = 0;
        virtual void                         DestroyTexture(TextureHandle handle)   = 0;

        /// Upload a mip level from CPU memory.
        virtual void WriteTexture(TextureHandle handle,
                                  const void* data, std::uint64_t dataSizeBytes,
                                  std::uint32_t mipLevel = 0,
                                  std::uint32_t arrayLayer = 0)                    = 0;

        // ---- Sampler resources ---------------------------------------
        [[nodiscard]] virtual SamplerHandle  CreateSampler(const SamplerDesc& desc) = 0;
        virtual void                         DestroySampler(SamplerHandle handle)   = 0;

        // ---- Pipeline resources --------------------------------------
        [[nodiscard]] virtual PipelineHandle  CreatePipeline(const PipelineDesc& desc) = 0;
        virtual void                          DestroyPipeline(PipelineHandle handle)   = 0;

        // ---- Async transfer ------------------------------------------
        // All upload/poll/collect operations are on ITransferQueue.
        // GpuAssetCache and other streaming consumers inject ITransferQueue&
        // directly — they never need the full IDevice interface.
        [[nodiscard]] virtual ITransferQueue& GetTransferQueue() = 0;

        // ---- Bindless heap -------------------------------------------
        // Returns the global bindless heap for this device.
        // Lifetime is tied to the device.  Never null after Initialize().
        [[nodiscard]] virtual IBindlessHeap& GetBindlessHeap() = 0;

        // ---- GPU profiler --------------------------------------------
        // Returns nullptr when profiling is disabled (e.g. release without Tracy).
        [[nodiscard]] virtual IProfiler* GetProfiler() = 0;

        // ---- Frame utilities -----------------------------------------
        [[nodiscard]] virtual std::uint32_t GetFramesInFlight()    const = 0;
        [[nodiscard]] virtual std::uint64_t GetGlobalFrameNumber() const = 0;

        // ---- Queue submit planning -----------------------------------
        // Backend-neutral queue seam used by the renderer's multi-queue
        // submit planner. The default preserves the historical single graphics
        // command context until a backend overrides specific queues.
        [[nodiscard]] virtual QueueCapabilityProfile GetQueueCapabilityProfile() const noexcept { return {}; }
        virtual ICommandContext& GetQueueContext(QueueAffinity affinity, std::uint32_t frameIndex)
        {
            (void)affinity;
            return GetGraphicsContext(frameIndex);
        }
        [[nodiscard]] virtual bool BeginFrameQueueSubmitPlan(const FrameHandle& frame,
                                                             const FrameQueueSubmitPlanDesc& plan)
        {
            (void)frame;
            (void)plan;
            return false;
        }
        virtual ICommandContext& GetQueueSubmitContext(QueueAffinity affinity,
                                                       std::uint32_t frameIndex,
                                                       std::uint32_t batchIndex)
        {
            (void)batchIndex;
            return GetQueueContext(affinity, frameIndex);
        }

        // ---- Placed resource memory ----------------------------------
        // GRAPHICS-118 Slice B introduces the backend-neutral memory-block
        // seam used by transient alias planning. Backends that have not yet
        // implemented real binding fail closed by returning invalid handles or
        // empty requirement/info records.
        [[nodiscard]] virtual ResourceMemoryRequirements GetBufferMemoryRequirements(
            const BufferDesc& desc) const noexcept
        {
            (void)desc;
            return {};
        }

        [[nodiscard]] virtual ResourceMemoryRequirements GetTextureMemoryRequirements(
            const TextureDesc& desc) const noexcept
        {
            (void)desc;
            return {};
        }

        [[nodiscard]] virtual MemoryBlockHandle CreateMemoryBlock(const MemoryBlockDesc& desc)
        {
            (void)desc;
            return {};
        }

        virtual void DestroyMemoryBlock(MemoryBlockHandle handle)
        {
            (void)handle;
        }

        [[nodiscard]] virtual MemoryBlockInfo GetMemoryBlockInfo(MemoryBlockHandle handle) const noexcept
        {
            (void)handle;
            return {};
        }

        [[nodiscard]] virtual BufferHandle CreatePlacedBuffer(const PlacedBufferDesc& desc)
        {
            (void)desc;
            return {};
        }

        [[nodiscard]] virtual TextureHandle CreatePlacedTexture(const PlacedTextureDesc& desc)
        {
            (void)desc;
            return {};
        }

        [[nodiscard]] virtual PlacedResourceInfo GetBufferMemoryPlacement(BufferHandle handle) const noexcept
        {
            (void)handle;
            return {};
        }

        [[nodiscard]] virtual PlacedResourceInfo GetTextureMemoryPlacement(TextureHandle handle) const noexcept
        {
            (void)handle;
            return {};
        }
    };
}
