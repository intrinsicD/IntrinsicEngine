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

        // ---- Frame lifecycle -----------------------------------------
        virtual bool BeginFrame(FrameHandle& outFrame)    = 0;
        virtual void EndFrame(const FrameHandle& frame)   = 0;
        virtual void Present(const FrameHandle& frame)    = 0;

        virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;
        virtual Platform::Extent2D GetBackbufferExtent() const = 0;

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
        // Non-blocking: data is copied into a staging buffer on the host and
        // queued for GPU upload.  Returns a token the caller can poll.
        // Contract: no loader/caller thread ever waits on a GPU fence here —
        // that happens inside CollectCompletedTransfers() on the render thread.
        [[nodiscard]] virtual TransferToken UploadBuffer(BufferHandle dst,
                                                         const void*  data,
                                                         std::uint64_t size,
                                                         std::uint64_t offset = 0)  = 0;

        [[nodiscard]] virtual TransferToken UploadTexture(TextureHandle dst,
                                                          const void*   data,
                                                          std::uint64_t dataSizeBytes,
                                                          std::uint32_t mipLevel   = 0,
                                                          std::uint32_t arrayLayer = 0) = 0;

        // Poll whether a specific transfer has completed on the GPU.
        [[nodiscard]] virtual bool IsTransferComplete(TransferToken token) const = 0;

        // Retire completed staging allocations.  Call once per frame on the render thread.
        virtual void CollectCompletedTransfers() = 0;

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
