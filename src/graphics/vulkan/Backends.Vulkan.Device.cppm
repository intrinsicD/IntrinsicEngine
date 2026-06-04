module;

#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Device;

export import Extrinsic.Core.Config.Render;
export import Extrinsic.Core.Geometry2D;
export import Extrinsic.Core.ResourcePool;
export import Extrinsic.RHI.Bindless;
export import Extrinsic.RHI.CommandContext;
export import Extrinsic.RHI.Descriptors;
export import Extrinsic.RHI.Device;
export import Extrinsic.RHI.FrameHandle;
export import Extrinsic.RHI.Handles;
export import Extrinsic.RHI.Profiler;
export import Extrinsic.RHI.Transfer;
export import Extrinsic.RHI.TransferQueue;
export import Extrinsic.RHI.Types;
export import :CommandPools;
export import :Descriptors;
export import :Diagnostics;
export import :Memory;
export import :OperationalStatus;
export import :Pipelines;
export import :Queues;
export import :Surface;
export import :Swapchain;
export import :Sync;
export import :Transfer;
import Extrinsic.Core.Logging;

namespace Extrinsic::Backends::Vulkan
{
    void NoteFallbackBindlessAllocationAttempt();
    void NoteFallbackTransferUploadAttempt();
    // NoteFallbackPipelineCreationAttempt(FallbackPipelineReason) is defined
    // in the implementation unit Backends.Vulkan.Device.cpp. It has no inline
    // callers in this partition, so no forward declaration is needed here.

    export class VulkanDevice final : public RHI::IDevice
    {
    public:
        VulkanDevice()  = default;
        ~VulkanDevice() override;

        void Initialize(const RHI::DeviceCreateDesc& desc) override;
        void Shutdown()  override;
        void WaitIdle()  override;

        [[nodiscard]] bool IsOperational() const noexcept override;

        // GRAPHICS-033E: receive the renderer's most recent recipe-aware
        // validation outcome. Stored in `m_LatestRecipeValidationClean` and
        // consumed by `BuildOperationalInputs()` to drive gate 7 of the
        // operational checklist.
        void NoteRecipeGraphValidation(bool clean) noexcept override;

        bool BeginFrame(RHI::FrameHandle& outFrame) override;
        void EndFrame(const RHI::FrameHandle& frame) override;
        void Present(const RHI::FrameHandle& frame) override;

        void Resize(uint32_t width, uint32_t height) override;
        [[nodiscard]] Core::Extent2D GetBackbufferExtent() const override;
        [[nodiscard]] RHI::Format GetBackbufferFormat() const override;

        void SetPresentMode(RHI::PresentMode mode) override;
        [[nodiscard]] RHI::PresentMode GetPresentMode() const override { return m_PresentMode; }

        // TODO(GRAPHICS-018): complete operational bring-up helper surfaces:
        // swapchain acquire/present/recreation diagnostics, global pipeline
        // layout/default sampler creation, memory type/depth format/
        // format-support queries, and bindless/transfer reconciliation.

        [[nodiscard]] RHI::TextureHandle GetBackbufferHandle(const RHI::FrameHandle& frame) const override;

        [[nodiscard]] RHI::ICommandContext& GetGraphicsContext(uint32_t frameIndex) override;

        [[nodiscard]] RHI::BufferHandle CreateBuffer(const RHI::BufferDesc& desc) override;
        void DestroyBuffer(RHI::BufferHandle handle) override;
        void WriteBuffer(RHI::BufferHandle handle, const void* data,
                         uint64_t size, uint64_t offset) override;
        void ReadBuffer(RHI::BufferHandle handle, void* data,
                        uint64_t size, uint64_t offset) override;
        [[nodiscard]] uint64_t GetBufferDeviceAddress(RHI::BufferHandle handle) const override;

        [[nodiscard]] RHI::TextureHandle CreateTexture(const RHI::TextureDesc& desc) override;
        void DestroyTexture(RHI::TextureHandle handle) override;
        void WriteTexture(RHI::TextureHandle handle, const void* data,
                          uint64_t dataSizeBytes, uint32_t mipLevel,
                          uint32_t arrayLayer) override;

        [[nodiscard]] RHI::SamplerHandle CreateSampler(const RHI::SamplerDesc& desc) override;
        void DestroySampler(RHI::SamplerHandle handle) override;

        [[nodiscard]] RHI::PipelineHandle CreatePipeline(const RHI::PipelineDesc& desc) override;
        void DestroyPipeline(RHI::PipelineHandle handle) override;

        [[nodiscard]] RHI::ITransferQueue& GetTransferQueue() override;
        [[nodiscard]] RHI::IBindlessHeap& GetBindlessHeap() override;
        [[nodiscard]] RHI::IProfiler*      GetProfiler()      override { return m_Profiler.get(); }
        [[nodiscard]] uint32_t GetFramesInFlight()    const override { return kMaxFramesInFlight; }
        [[nodiscard]] uint64_t GetGlobalFrameNumber() const override { return m_GlobalFrameNumber; }

    private:
        // GRAPHICS-033A: `EvaluateVulkanDeviceOperationalStatus` is the
        // backend-public re-evaluator for `RHI::IDevice*` produced by
        // `CreateVulkanDevice()`. It needs to read the private operational
        // inputs without exposing them as a public method on `IDevice`
        // (renderer/runtime code must keep branching on
        // `IDevice::IsOperational()`). The friendship keeps the read-only
        // access surface internal to the module.
        friend VulkanOperationalStatus EvaluateVulkanDeviceOperationalStatus(
            const RHI::IDevice* device) noexcept;
        // GRAPHICS-033E: read-only snapshot of `BuildOperationalInputs()` for
        // contract tests that need to observe individual gate inputs without
        // exposing them on `RHI::IDevice`.
        friend VulkanOperationalInputs GetVulkanDeviceOperationalInputs(
            const RHI::IDevice* device) noexcept;

        class FallbackBindlessHeap final : public RHI::IBindlessHeap
        {
        public:
            [[nodiscard]] RHI::BindlessIndex AllocateTextureSlot(RHI::TextureHandle, RHI::SamplerHandle) override
            {
                NoteFallbackBindlessAllocationAttempt();
                Core::Log::Warn("[VulkanDevice] Fallback bindless heap rejected texture allocation; device is non-operational");
                return RHI::kInvalidBindlessIndex;
            }

            void UpdateTextureSlot(RHI::BindlessIndex, RHI::TextureHandle, RHI::SamplerHandle) override {}
            void FreeSlot(RHI::BindlessIndex) override {}
            void FlushPending() override {}

            [[nodiscard]] std::uint32_t GetCapacity() const override { return 0; }
            [[nodiscard]] std::uint32_t GetAllocatedSlotCount() const override { return 0; }
        };

        class FallbackTransferQueue final : public RHI::ITransferQueue
        {
        public:
            [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                          const void*,
                                                          std::uint64_t,
                                                          std::uint64_t) override
            {
                NoteFallbackTransferUploadAttempt();
                Core::Log::Warn("[VulkanDevice] Fallback transfer queue rejected buffer upload; device is non-operational");
                return {};
            }

            [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                          std::span<const std::byte>,
                                                          std::uint64_t) override
            {
                NoteFallbackTransferUploadAttempt();
                Core::Log::Warn("[VulkanDevice] Fallback transfer queue rejected buffer upload; device is non-operational");
                return {};
            }

            [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle,
                                                           const void*,
                                                           std::uint64_t,
                                                           std::uint32_t,
                                                           std::uint32_t) override
            {
                NoteFallbackTransferUploadAttempt();
                Core::Log::Warn("[VulkanDevice] Fallback transfer queue rejected texture upload; device is non-operational");
                return {};
            }

            [[nodiscard]] RHI::TransferToken UploadTextureFullChain(RHI::TextureHandle,
                                                                    std::span<const std::byte>) override
            {
                NoteFallbackTransferUploadAttempt();
                Core::Log::Warn("[VulkanDevice] Fallback transfer queue rejected full-chain texture upload; device is non-operational");
                return {};
            }

            [[nodiscard]] bool IsComplete(RHI::TransferToken token) const override { return !token.IsValid(); }
            void CollectCompleted() override {}
        };

        // BeginOneShot/EndOneShot submit to the graphics queue and wait for it
        // to become idle. These helpers are init/load-time only; runtime
        // uploads must use IDevice::GetTransferQueue() / ITransferQueue.
        [[nodiscard]] VkCommandBuffer BeginOneShot();
        [[nodiscard]] bool EndOneShot(VkCommandBuffer cmd);

        [[nodiscard]] bool HasLiveOperationalPrerequisites() const noexcept;
        [[nodiscard]] bool HasOperationalSafetyPrerequisites() const noexcept;
        [[nodiscard]] VulkanOperationalInputs BuildOperationalInputs() const noexcept;
        [[nodiscard]] bool ComputeOperationalPredicate() const noexcept;
        void RefreshOperationalState() noexcept;
        void NoteDeviceLostIfNeeded(VkResult result) noexcept;
        [[nodiscard]] VkResult CreateSwapchainResources(std::uint32_t requestedWidth,
                                                        std::uint32_t requestedHeight,
                                                        VkSwapchainKHR oldSwapchain,
                                                        VulkanSwapchainState& outState);
        void DestroySwapchainState(VulkanSwapchainState& state);
        void AdoptSwapchainState(VulkanSwapchainState&& state);
        void ResetFrameAcquisitionState() noexcept;

        void DeferDelete(VulkanDeferredDelete fn);
        void FlushDeletionQueue(uint32_t frameSlot);

        VkInstance               m_Instance       = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_Messenger      = VK_NULL_HANDLE;
        VkSurfaceKHR             m_Surface        = VK_NULL_HANDLE;
        VkPhysicalDevice         m_PhysDevice     = VK_NULL_HANDLE;
        VkDevice                 m_Device         = VK_NULL_HANDLE;
        VmaAllocator             m_Vma            = VK_NULL_HANDLE;

        VkQueue  m_GraphicsQueue     = VK_NULL_HANDLE;
        VkQueue  m_AsyncComputeQueue = VK_NULL_HANDLE;
        VkQueue  m_PresentQueue      = VK_NULL_HANDLE;
        VkQueue  m_TransferVkQueue   = VK_NULL_HANDLE;
        uint32_t m_GraphicsFamily = 0;
        uint32_t m_AsyncComputeFamily = 0;
        uint32_t m_AsyncComputeQueueIndex = 0;
        uint32_t m_PresentFamily  = 0;
        uint32_t m_TransferFamily = 0;
        std::mutex m_QueueMutex;

        VkSwapchainKHR              m_Swapchain        = VK_NULL_HANDLE;
        VkFormat                    m_SwapchainFormat  = VK_FORMAT_UNDEFINED;
        VkExtent2D                  m_SwapchainExtent  = {};
        VkExtent2D                  m_PendingResizeExtent = {};
        std::vector<VkImage>        m_SwapchainImages;
        std::vector<VkImageView>    m_SwapchainViews;
        std::vector<RHI::TextureHandle> m_SwapchainHandles;

        std::array<PerFrame, kMaxFramesInFlight> m_Frames;
        uint32_t m_FrameSlot         = 0;
        uint64_t m_GlobalFrameNumber = 0;

        std::array<VulkanCommandContext, kMaxFramesInFlight> m_CmdContexts;
        VkCommandPool   m_OneShotCmdPool   = VK_NULL_HANDLE;
        VkCommandBuffer m_OneShotCmdBuffer = VK_NULL_HANDLE;
        bool            m_OneShotRecording = false;
        std::mutex      m_OneShotMutex;

        Core::ResourcePool<VulkanBuffer,   RHI::BufferHandle,   kMaxFramesInFlight> m_Buffers;
        Core::ResourcePool<VulkanImage,    RHI::TextureHandle,  kMaxFramesInFlight> m_Images;
        Core::ResourcePool<VulkanSampler,  RHI::SamplerHandle,  kMaxFramesInFlight> m_Samplers;
        Core::ResourcePool<VulkanPipeline, RHI::PipelineHandle, kMaxFramesInFlight> m_Pipelines;

        std::unique_ptr<VulkanBindlessHeap>   m_BindlessHeap;
        std::unique_ptr<VulkanProfiler>       m_Profiler;
        std::unique_ptr<VulkanTransferQueue>  m_TransferQueue;
        FallbackBindlessHeap                  m_FallbackBindlessHeap;
        FallbackTransferQueue                 m_FallbackTransferQueue;

        VkPipelineLayout m_GlobalPipelineLayout = VK_NULL_HANDLE;
        RHI::SamplerHandle m_DefaultSamplerHandle{};

        // GRAPHICS-033E: backs gate 7 (`BarrierValidationClean`). Reset to
        // `false` in `Initialize()` so cold startup is fail-closed until the
        // renderer publishes the first clean recipe-aware validation outcome.
        std::atomic<bool> m_LatestRecipeValidationClean{false};

        RHI::PresentMode m_PresentMode  = RHI::PresentMode::VSync;
        bool             m_Operational  = false;
        bool             m_DeviceLost   = false;
        bool             m_HasPendingResize = false;
        bool             m_ValidationEnabled = false;
        // Set during logical-device feature negotiation; sampler creation still
        // remains guarded by the non-operational device state until full
        // swapchain/resource bring-up lands.
        bool             m_SamplerAnisotropySupported = false;
    };
}
