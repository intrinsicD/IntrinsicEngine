module;
#include <vector>
#include <memory>
#include "RHI.Vulkan.hpp"

module Graphics:Presentation.Impl;

import :Presentation;
import RHI;

namespace Graphics
{
    PresentationSystem::PresentationSystem(std::shared_ptr<RHI::VulkanDevice> device,
                                           RHI::VulkanSwapchain& swapchain,
                                           RHI::SimpleRenderer& renderer)
        : m_Device(std::move(device)),
          m_Swapchain(swapchain),
          m_Renderer(renderer)
    {
        // Resize depth images to match frames-in-flight (usually 2).
        m_DepthImages.resize(m_Renderer.GetFramesInFlight());
    }

    PresentationSystem::~PresentationSystem()
    {
        // Explicit cleanup.
        for (auto& img : m_DepthImages)
        {
            img.reset();
        }
    }

    bool PresentationSystem::BeginFrame()
    {
        m_Renderer.BeginFrame();
        return m_Renderer.IsFrameInProgress();
    }

    void PresentationSystem::EndFrame()
    {
        m_Renderer.EndFrame();
    }

    uint32_t PresentationSystem::GetFrameIndex() const
    {
        return m_Renderer.GetCurrentFrameIndex();
    }

    uint32_t PresentationSystem::GetImageIndex() const
    {
        return m_Renderer.GetImageIndex();
    }

    VkImage PresentationSystem::GetBackbuffer() const
    {
        return m_Renderer.GetSwapchainImage(GetImageIndex());
    }

    VkImageView PresentationSystem::GetBackbufferView() const
    {
        return m_Renderer.GetSwapchainImageView(GetImageIndex());
    }

    VkFormat PresentationSystem::GetBackbufferFormat() const
    {
        return m_Swapchain.GetImageFormat();
    }

    VkExtent2D PresentationSystem::GetResolution() const
    {
        return m_Swapchain.GetExtent();
    }

    VkCommandBuffer PresentationSystem::GetCommandBuffer() const
    {
        return m_Renderer.GetCommandBuffer();
    }

    RHI::VulkanImage& PresentationSystem::GetDepthBuffer()
    {
        const uint32_t frameIndex = GetFrameIndex();
        auto& depthImg = m_DepthImages[frameIndex];
        const auto extent = GetResolution();

        // If not allocated or size mismatch (resize event), re-create.
        if (!depthImg || depthImg->GetWidth() != extent.width || depthImg->GetHeight() != extent.height)
        {
            VkFormat depthFormat = RHI::VulkanImage::FindDepthFormat(*m_Device);
            depthImg = std::make_unique<RHI::VulkanImage>(
                *m_Device,
                extent.width,
                extent.height,
                1,
                depthFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT
            );
        }

        return *depthImg;
    }

    void PresentationSystem::OnResize()
    {
        // Clearing depth buffers forces reallocation on next use (in GetDepthBuffer).
        for (auto& img : m_DepthImages)
        {
            img.reset();
        }
    }
}
