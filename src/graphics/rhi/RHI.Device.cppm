module;

#include <cstdint>
#include <memory>
#include <span>

export module Extrinsic.RHI.Device;

import Extrinsic.Platform.Window;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
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
    export class IDevice
    {
    public:
        virtual ~IDevice() = default;

        // ---- Device lifecycle ----------------------------------------
        virtual void Initialize(Platform::IWindow& window, const Core::Config::RenderConfig& config) = 0;
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

        // ---- Frame lifecycle -----------------------------------------
        virtual bool BeginFrame(FrameHandle& outFrame)    = 0;
        virtual void EndFrame(const FrameHandle& frame)   = 0;
        virtual void Present(const FrameHandle& frame)    = 0;

        virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;
        [[nodiscard]] virtual Platform::Extent2D GetBackbufferExtent() const = 0;

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
    };
}
