module;
#include <cstring>
#include <memory>
#include "RHI.Vulkan.hpp"

module Runtime.GraphicsBackend;

import Core.Logging;
import Core.Window;
import RHI;

namespace Runtime
{
    GraphicsBackend::GraphicsBackend(Core::Windowing::Window& window, const GraphicsBackendConfig& config)
        : m_Window(&window)
    {
        Core::Log::Info("GraphicsBackend: Initializing...");

        // 1. Vulkan Context
        RHI::ContextConfig ctxConfig{config.AppName, config.EnableValidation};
        m_Context = std::make_unique<RHI::VulkanContext>(ctxConfig);

        // 2. Surface
        if (!window.CreateSurface(m_Context->GetInstance(), nullptr, &m_Surface))
        {
            Core::Log::Error("FATAL: Failed to create Vulkan Surface");
            std::exit(-1);
        }

        // 3. Device
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, m_Surface);

        // 4. Bindless + TextureSystem
        m_BindlessSystem = std::make_unique<RHI::BindlessDescriptorSystem>(*m_Device);
        m_TextureSystem = std::make_unique<RHI::TextureSystem>(*m_Device, *m_BindlessSystem);

        // 5. Swapchain & Renderer
        m_Swapchain = std::make_unique<RHI::VulkanSwapchain>(m_Device, window);
        m_Renderer = std::make_unique<RHI::SimpleRenderer>(m_Device, *m_Swapchain);

        // 6. Transfer Manager
        m_TransferManager = std::make_unique<RHI::TransferManager>(*m_Device);

        // 7. Descriptor plumbing
        m_DescriptorLayout = std::make_unique<RHI::DescriptorLayout>(*m_Device);
        m_DescriptorPool = std::make_unique<RHI::DescriptorAllocator>(*m_Device);

        // 8. Default texture (bindless slot 0)
        CreateDefaultTexture();

        Core::Log::Info("GraphicsBackend: Initialization complete.");
    }

    GraphicsBackend::~GraphicsBackend()
    {
        if (m_Device)
        {
            vkDeviceWaitIdle(m_Device->GetLogicalDevice());
        }

        // Destroy default texture first (holds a bindless slot).
        m_DefaultTexture.reset();

        // Texture pool: process any final deletions and clear.
        if (m_TextureSystem)
        {
            m_TextureSystem->ProcessDeletions();
            m_TextureSystem->Clear();
        }

        // Descriptor systems.
        m_BindlessSystem.reset();
        m_DescriptorPool.reset();
        m_DescriptorLayout.reset();

        // Presentation.
        m_Renderer.reset();
        m_Swapchain.reset();

        // Transfer.
        m_TransferManager.reset();

        // Texture system (after descriptors and transfer are gone).
        m_TextureSystem.reset();

        // Flush deferred VkObject destruction.
        if (m_Device)
        {
            m_Device->FlushAllDeletionQueues();
        }

        m_Device.reset();

        // Surface and context.
        if (m_Context && m_Surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_Context->GetInstance(), m_Surface, nullptr);
        }
        m_Context.reset();

        Core::Log::Info("GraphicsBackend: Shutdown complete.");
    }

    void GraphicsBackend::CreateDefaultTexture()
    {
        const RHI::TextureHandle handle = m_TextureSystem->CreatePending(1, 1, VK_FORMAT_R8G8B8A8_SRGB);
        m_DefaultTexture = std::make_shared<RHI::Texture>(*m_TextureSystem, *m_Device, handle);

        // Upload a single white pixel (RGBA8) via the transfer queue.
        const uint32_t white = 0xFFFFFFFFu;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_Device->GetPhysicalDevice(), &props);

        auto alloc = m_TransferManager->AllocateStagingForImage(
            sizeof(white),
            /*texelBlockSize*/ 4,
            /*rowPitchBytes*/ 4,
            static_cast<size_t>(props.limits.optimalBufferCopyOffsetAlignment),
            static_cast<size_t>(props.limits.optimalBufferCopyRowPitchAlignment));

        if (alloc.Buffer != VK_NULL_HANDLE && alloc.MappedPtr != nullptr)
        {
            std::memcpy(alloc.MappedPtr, &white, sizeof(white));

            VkCommandBuffer cmd = m_TransferManager->Begin();
            VkImage dstImage = m_DefaultTexture->GetImage();

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

            (void)m_TransferManager->Submit(cmd);
        }
        else
        {
            Core::Log::Warn("Default texture staging allocation failed; default texture may appear black.");
        }

        // Bindless slot 0 is reserved for the default/error texture.
        m_DefaultTextureIndex = 0;
        m_BindlessSystem->SetTexture(m_DefaultTextureIndex, *m_DefaultTexture);

        // Plumb default descriptor into the TextureSystem so freed slots become safe to sample.
        m_TextureSystem->SetDefaultDescriptor(m_DefaultTexture->GetView(), m_DefaultTexture->GetSampler());
    }

    void GraphicsBackend::OnResize()
    {
        m_Renderer->OnResize();
    }

    void GraphicsBackend::GarbageCollectTransfers()
    {
        m_TransferManager->GarbageCollect();
    }

    void GraphicsBackend::ProcessTextureDeletions()
    {
        if (m_TextureSystem)
        {
            m_TextureSystem->ProcessDeletions();
        }
    }

    void GraphicsBackend::WaitIdle()
    {
        if (m_Device)
        {
            vkDeviceWaitIdle(m_Device->GetLogicalDevice());
        }
    }

    void GraphicsBackend::FlushDeletionQueues()
    {
        if (m_Device)
        {
            m_Device->FlushAllDeletionQueues();
        }
    }

    void GraphicsBackend::ClearTextureSystem()
    {
        if (m_TextureSystem)
        {
            m_TextureSystem->ProcessDeletions();
            m_TextureSystem->Clear();
        }
    }
}
