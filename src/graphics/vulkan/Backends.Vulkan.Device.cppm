module;

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Device;

export import Extrinsic.Core.Config.Render;
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
export import Extrinsic.Platform.Window;
export import :CommandPools;
export import :Descriptors;
export import :Diagnostics;
export import :Memory;
export import :Pipelines;
export import :Queues;
export import :Surface;
export import :Swapchain;
export import :Sync;
export import :Transfer;

namespace Extrinsic::Backends::Vulkan
{
    export class VulkanDevice final : public RHI::IDevice
    {
    public:
        VulkanDevice()  = default;
        ~VulkanDevice() override;

        void Initialize(Platform::IWindow& window,
                        const Core::Config::RenderConfig& config) override;
        void Shutdown()  override;
        void WaitIdle()  override;

        [[nodiscard]] bool IsOperational() const noexcept override { return m_Operational; }

        bool BeginFrame(RHI::FrameHandle& outFrame) override;
        void EndFrame(const RHI::FrameHandle& frame) override;
        void Present(const RHI::FrameHandle& frame) override;

        void Resize(uint32_t width, uint32_t height) override;
        [[nodiscard]] Platform::Extent2D GetBackbufferExtent() const override;

        void SetPresentMode(RHI::PresentMode mode) override;
        [[nodiscard]] RHI::PresentMode GetPresentMode() const override { return m_PresentMode; }

        [[nodiscard]] RHI::TextureHandle GetBackbufferHandle(const RHI::FrameHandle& frame) const override;

        [[nodiscard]] RHI::ICommandContext& GetGraphicsContext(uint32_t frameIndex) override;

        [[nodiscard]] RHI::BufferHandle CreateBuffer(const RHI::BufferDesc& desc) override;
        void DestroyBuffer(RHI::BufferHandle handle) override;
        void WriteBuffer(RHI::BufferHandle handle, const void* data,
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

        [[nodiscard]] RHI::ITransferQueue& GetTransferQueue() override { return *m_TransferQueue; }
        [[nodiscard]] RHI::IBindlessHeap&  GetBindlessHeap()  override { return *m_BindlessHeap; }
        [[nodiscard]] RHI::IProfiler*      GetProfiler()      override { return m_Profiler.get(); }
        [[nodiscard]] uint32_t GetFramesInFlight()    const override { return kMaxFramesInFlight; }
        [[nodiscard]] uint64_t GetGlobalFrameNumber() const override { return m_GlobalFrameNumber; }

    private:
        void CreateInstance(const Core::Config::RenderConfig& cfg);
        void CreateSurface(Platform::IWindow& window);
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateVma();
        void CreateSwapchain(uint32_t width, uint32_t height);
        void DestroySwapchain();
        void RegisterSwapchainImages();
        void CreatePerFrameResources();
        void DestroyPerFrameResources();
        void CreateGlobalPipelineLayout();
        void CreateDefaultSampler();

        [[nodiscard]] uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
        [[nodiscard]] VkFormat FindDepthFormat() const;
        [[nodiscard]] bool     SupportsFormat(VkFormat fmt, VkFormatFeatureFlags feats) const;

        [[nodiscard]] VkCommandBuffer BeginOneShot();
        void EndOneShot(VkCommandBuffer cmd);

        void DeferDelete(VulkanDeferredDelete fn);
        void FlushDeletionQueue(uint32_t frameSlot);

        VkInstance               m_Instance       = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_Messenger      = VK_NULL_HANDLE;
        VkSurfaceKHR             m_Surface        = VK_NULL_HANDLE;
        VkPhysicalDevice         m_PhysDevice     = VK_NULL_HANDLE;
        VkDevice                 m_Device         = VK_NULL_HANDLE;
        VmaAllocator             m_Vma            = VK_NULL_HANDLE;

        VkQueue  m_GraphicsQueue    = VK_NULL_HANDLE;
        VkQueue  m_PresentQueue     = VK_NULL_HANDLE;
        VkQueue  m_TransferVkQueue  = VK_NULL_HANDLE;
        uint32_t m_GraphicsFamily = 0;
        uint32_t m_PresentFamily  = 0;
        uint32_t m_TransferFamily = 0;
        std::mutex m_QueueMutex;

        VkSwapchainKHR              m_Swapchain        = VK_NULL_HANDLE;
        VkFormat                    m_SwapchainFormat  = VK_FORMAT_UNDEFINED;
        VkExtent2D                  m_SwapchainExtent  = {};
        std::vector<VkImage>        m_SwapchainImages;
        std::vector<VkImageView>    m_SwapchainViews;
        std::vector<RHI::TextureHandle> m_SwapchainHandles;

        std::array<PerFrame, kMaxFramesInFlight> m_Frames;
        uint32_t m_FrameSlot         = 0;
        uint64_t m_GlobalFrameNumber = 0;

        std::array<VulkanCommandContext, kMaxFramesInFlight> m_CmdContexts;

        Core::ResourcePool<VulkanBuffer,   RHI::BufferHandle,   kMaxFramesInFlight> m_Buffers;
        Core::ResourcePool<VulkanImage,    RHI::TextureHandle,  kMaxFramesInFlight> m_Images;
        Core::ResourcePool<VulkanSampler,  RHI::SamplerHandle,  kMaxFramesInFlight> m_Samplers;
        Core::ResourcePool<VulkanPipeline, RHI::PipelineHandle, kMaxFramesInFlight> m_Pipelines;

        std::unique_ptr<VulkanBindlessHeap>   m_BindlessHeap;
        std::unique_ptr<VulkanProfiler>       m_Profiler;
        std::unique_ptr<VulkanTransferQueue>  m_TransferQueue;

        VkPipelineLayout m_GlobalPipelineLayout = VK_NULL_HANDLE;
        RHI::SamplerHandle m_DefaultSamplerHandle{};

        RHI::PresentMode m_PresentMode  = RHI::PresentMode::VSync;
        bool             m_Operational  = false;
        bool             m_NeedsResize  = false;
        bool             m_ValidationEnabled = false;
    };
}

