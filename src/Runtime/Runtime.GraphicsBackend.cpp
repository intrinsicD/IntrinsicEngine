module;
#include <cstring>
#include <memory>
#include "RHI.Vulkan.hpp"

module Runtime.GraphicsBackend;

import Core.Logging;
import Core.Window;
import RHI.Bindless;
import RHI.Context;
#ifdef INTRINSIC_HAS_CUDA
import RHI.CudaDevice;
import RHI.CudaError;
#endif
import RHI.Descriptors;
import RHI.Device;
import RHI.Renderer;
import RHI.Swapchain;
import RHI.Texture;
import RHI.TextureFwd;
import RHI.TextureManager;
import RHI.Transfer;

namespace Runtime
{
    struct GraphicsBackend::Impl
    {
        std::unique_ptr<RHI::VulkanContext> Context;
        std::shared_ptr<RHI::VulkanDevice> Device;
        VkSurfaceKHR Surface = VK_NULL_HANDLE;
        std::unique_ptr<RHI::VulkanSwapchain> Swapchain;
        std::unique_ptr<RHI::SimpleRenderer> Renderer;
        std::unique_ptr<RHI::TransferManager> TransferManager;
        std::unique_ptr<RHI::DescriptorLayout> DescriptorLayout;
        std::unique_ptr<RHI::DescriptorAllocator> DescriptorPool;
        std::unique_ptr<RHI::BindlessDescriptorSystem> BindlessSystem;
        std::unique_ptr<RHI::TextureManager> TextureManager;
        std::shared_ptr<RHI::Texture> DefaultTexture;
        uint32_t DefaultTextureIndex = 0;
#ifdef INTRINSIC_HAS_CUDA
        std::unique_ptr<RHI::CudaDevice> CudaDevice;
#endif
    };

    namespace
    {
        // Helper: create the fallback white-pixel texture used for untextured
        // surfaces.  Declared with a deduced parameter to avoid naming the
        // private GraphicsBackend::Impl type, which Clang 20 modules enforce
        // access control on even inside the implementation unit.
        void CreateDefaultTexture(auto& impl)
        {
            const RHI::TextureHandle handle = impl.TextureManager->CreatePending(1, 1, VK_FORMAT_R8G8B8A8_SRGB);
            impl.DefaultTexture = std::make_shared<RHI::Texture>(*impl.TextureManager, *impl.Device, handle);

            // Upload a single white pixel (RGBA8) via the transfer queue.
            const uint32_t white = 0xFFFFFFFFu;

            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(impl.Device->GetPhysicalDevice(), &props);

            auto alloc = impl.TransferManager->AllocateStagingForImage(
                sizeof(white),
                /*texelBlockSize*/ 4,
                /*rowPitchBytes*/ 4,
                static_cast<size_t>(props.limits.optimalBufferCopyOffsetAlignment),
                static_cast<size_t>(props.limits.optimalBufferCopyRowPitchAlignment));

            if (alloc.Buffer != VK_NULL_HANDLE && alloc.MappedPtr != nullptr)
            {
                std::memcpy(alloc.MappedPtr, &white, sizeof(white));

                VkCommandBuffer cmd = impl.TransferManager->Begin();
                VkImage dstImage = impl.DefaultTexture->GetImage();

                if (dstImage != VK_NULL_HANDLE)
                {
                    VkImageMemoryBarrier2 barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                    barrier.srcAccessMask = 0;
                    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = dstImage;
                    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                    VkDependencyInfo dep{};
                    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    dep.imageMemoryBarrierCount = 1;
                    dep.pImageMemoryBarriers = &barrier;
                    vkCmdPipelineBarrier2(cmd, &dep);

                    VkBufferImageCopy region{};
                    region.bufferOffset = static_cast<VkDeviceSize>(alloc.Offset);
                    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    region.imageExtent = {1, 1, 1};
                    vkCmdCopyBufferToImage(cmd, alloc.Buffer, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

                    VkImageMemoryBarrier2 readBarrier = barrier;
                    readBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                    readBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    readBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                    readBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                    readBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    readBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                    VkDependencyInfo dep2{};
                    dep2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    dep2.imageMemoryBarrierCount = 1;
                    dep2.pImageMemoryBarriers = &readBarrier;
                    vkCmdPipelineBarrier2(cmd, &dep2);
                }

                (void)impl.TransferManager->Submit(cmd);
            }
            else
            {
                Core::Log::Warn("Default texture staging allocation failed; default texture may appear black.");
            }

            // Bindless slot 0 is reserved for the default/error texture.
            impl.DefaultTextureIndex = 0;
            impl.BindlessSystem->SetTexture(impl.DefaultTextureIndex,
                                            impl.DefaultTexture->GetView(),
                                            impl.DefaultTexture->GetSampler());

            // Plumb default descriptor into the TextureManager so freed slots become safe to sample.
            impl.TextureManager->SetDefaultDescriptor(impl.DefaultTexture->GetView(), impl.DefaultTexture->GetSampler());
        }
    }

    GraphicsBackend::GraphicsBackend(Core::Windowing::Window& window, const GraphicsBackendConfig& config)
        : m_Impl(std::make_unique<Impl>())
    {
        Core::Log::Info("GraphicsBackend: Initializing...");

        // 1. Vulkan Context
        RHI::ContextConfig ctxConfig{config.AppName, config.EnableValidation};
        m_Impl->Context = std::make_unique<RHI::VulkanContext>(ctxConfig);

        // 2. Surface
        if (!window.CreateSurface(m_Impl->Context->GetInstance(), nullptr, &m_Impl->Surface))
        {
            Core::Log::Error("FATAL: Failed to create Vulkan surface for app '{}'.", config.AppName);
            Core::Log::Error("Fix: ensure the window system and GPU driver support Vulkan surface creation "
                             "(Wayland/X11/Win32) and that the process has display access.");
            std::exit(-1);
        }

        // 3. Device
        m_Impl->Device = std::make_shared<RHI::VulkanDevice>(*m_Impl->Context, m_Impl->Surface);
        if (!m_Impl->Device || !m_Impl->Device->IsValid() ||
            m_Impl->Device->GetLogicalDevice() == VK_NULL_HANDLE)
        {
            Core::Log::Error("FATAL: Failed to initialize Vulkan device for app '{}'.", config.AppName);
            std::exit(-1);
        }

#ifdef INTRINSIC_HAS_CUDA
        if (auto cudaDevice = RHI::CudaDevice::Create(); cudaDevice)
        {
            m_Impl->CudaDevice = std::move(*cudaDevice);
        }
        else
        {
            Core::Log::Warn("GraphicsBackend: CUDA backend unavailable ({})",
                            RHI::CudaErrorToString(cudaDevice.error()));
        }
#endif

        // 4. Bindless + TextureManager
        m_Impl->BindlessSystem = std::make_unique<RHI::BindlessDescriptorSystem>(*m_Impl->Device);
        m_Impl->TextureManager = std::make_unique<RHI::TextureManager>(*m_Impl->Device, *m_Impl->BindlessSystem);

        // 5. Swapchain & Renderer
        m_Impl->Swapchain = std::make_unique<RHI::VulkanSwapchain>(m_Impl->Device, window);
        m_Impl->Renderer = std::make_unique<RHI::SimpleRenderer>(m_Impl->Device, *m_Impl->Swapchain);

        // 6. Transfer Manager
        m_Impl->TransferManager = std::make_unique<RHI::TransferManager>(*m_Impl->Device);

        // 7. Descriptor plumbing
        m_Impl->DescriptorLayout = std::make_unique<RHI::DescriptorLayout>(*m_Impl->Device);
        m_Impl->DescriptorPool = std::make_unique<RHI::DescriptorAllocator>(*m_Impl->Device);

        // 8. Default texture (bindless slot 0)
        CreateDefaultTexture(*m_Impl);

        Core::Log::Info("GraphicsBackend: Initialization complete.");
    }

    GraphicsBackend::~GraphicsBackend()
    {
        if (!m_Impl)
        {
            return;
        }

        if (m_Impl->Device && m_Impl->Device->GetLogicalDevice() != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_Impl->Device->GetLogicalDevice());
        }

        // Destroy default texture first (holds a bindless slot).
        m_Impl->DefaultTexture.reset();

        // Texture pool: process any final deletions and clear.
        if (m_Impl->TextureManager)
        {
            m_Impl->TextureManager->ProcessDeletions();
            m_Impl->TextureManager->Clear();
        }

        // Descriptor systems.
        m_Impl->BindlessSystem.reset();
        m_Impl->DescriptorPool.reset();
        m_Impl->DescriptorLayout.reset();

        // Presentation.
        m_Impl->Renderer.reset();
        m_Impl->Swapchain.reset();

        // Transfer.
        m_Impl->TransferManager.reset();

        // Texture system (after descriptors and transfer are gone).
        m_Impl->TextureManager.reset();

        // Flush deferred VkObject destruction.
        if (m_Impl->Device)
        {
            m_Impl->Device->FlushAllDeletionQueues();
        }

#ifdef INTRINSIC_HAS_CUDA
        m_Impl->CudaDevice.reset();
#endif

        m_Impl->Device.reset();

        // Surface and context.
        if (m_Impl->Context && m_Impl->Surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_Impl->Context->GetInstance(), m_Impl->Surface, nullptr);
        }
        m_Impl->Context.reset();

        Core::Log::Info("GraphicsBackend: Shutdown complete.");
    }

    RHI::VulkanContext& GraphicsBackend::GetContext() const
    {
        return *m_Impl->Context;
    }

    RHI::VulkanDevice& GraphicsBackend::GetDevice() const
    {
        return *m_Impl->Device;
    }

    const std::shared_ptr<RHI::VulkanDevice>& GraphicsBackend::GetDeviceShared() const
    {
        return m_Impl->Device;
    }

    RHI::VulkanSwapchain& GraphicsBackend::GetSwapchain() const
    {
        return *m_Impl->Swapchain;
    }

    RHI::SimpleRenderer& GraphicsBackend::GetRenderer() const
    {
        return *m_Impl->Renderer;
    }

    RHI::TransferManager& GraphicsBackend::GetTransferManager() const
    {
        return *m_Impl->TransferManager;
    }

    RHI::DescriptorLayout& GraphicsBackend::GetDescriptorLayout() const
    {
        return *m_Impl->DescriptorLayout;
    }

    RHI::DescriptorAllocator& GraphicsBackend::GetDescriptorPool() const
    {
        return *m_Impl->DescriptorPool;
    }

    RHI::BindlessDescriptorSystem& GraphicsBackend::GetBindlessSystem() const
    {
        return *m_Impl->BindlessSystem;
    }

    RHI::TextureManager& GraphicsBackend::GetTextureManager() const
    {
        return *m_Impl->TextureManager;
    }

    uint32_t GraphicsBackend::GetDefaultTextureIndex() const
    {
        return m_Impl->DefaultTextureIndex;
    }

#ifdef INTRINSIC_HAS_CUDA
    RHI::CudaDevice* GraphicsBackend::GetCudaDevice() const
    {
        return m_Impl->CudaDevice.get();
    }
#endif

    void GraphicsBackend::SetPresentPolicy(RHI::PresentPolicy policy)
    {
        if (m_Impl->Swapchain && m_Impl->Swapchain->GetPresentPolicy() != policy)
        {
            m_Impl->Swapchain->SetPresentPolicy(policy);
            // Recreate() internally calls WaitForGraphicsIdle() before rebuilding.
            m_Impl->Swapchain->Recreate();
            Core::Log::Info("GraphicsBackend: Present policy changed to {}", RHI::ToString(policy));
        }
    }

    RHI::PresentPolicy GraphicsBackend::GetPresentPolicy() const
    {
        return m_Impl->Swapchain ? m_Impl->Swapchain->GetPresentPolicy() : RHI::PresentPolicy::VSync;
    }

    void GraphicsBackend::OnResize()
    {
        m_Impl->Renderer->OnResize();
    }

    void GraphicsBackend::CollectGpuDeferredDestructions()
    {
        if (m_Impl->Device)
        {
            m_Impl->Device->CollectGarbage();
        }
    }

    void GraphicsBackend::GarbageCollectTransfers()
    {
        m_Impl->TransferManager->GarbageCollect();
    }

    void GraphicsBackend::ProcessTextureDeletions()
    {
        if (m_Impl->TextureManager)
        {
            m_Impl->TextureManager->ProcessDeletions();
        }
    }

    void GraphicsBackend::WaitIdle()
    {
        if (m_Impl->Device)
        {
            vkDeviceWaitIdle(m_Impl->Device->GetLogicalDevice());
        }
    }

    void GraphicsBackend::FlushDeletionQueues()
    {
        if (m_Impl->Device)
        {
            m_Impl->Device->FlushAllDeletionQueues();
        }
    }

    void GraphicsBackend::ClearTextureManager()
    {
        if (m_Impl->TextureManager)
        {
            m_Impl->TextureManager->ProcessDeletions();
            m_Impl->TextureManager->Clear();
        }
    }
}
