module;
#include "RHI.Vulkan.hpp"

module Runtime.RHI.Renderer;
import Core.Logging;
import Runtime.RHI.Image;

namespace Runtime::RHI
{
    SimpleRenderer::SimpleRenderer(VulkanDevice& device, VulkanSwapchain& swapchain)
        : m_Device(device), m_Swapchain(swapchain)
    {
        InitSyncStructures();

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_Device.GetCommandPool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        CreateDepthBuffer();

        if (vkAllocateCommandBuffers(m_Device.GetLogicalDevice(), &allocInfo, &m_CommandBuffer) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to allocate command buffers!");
        }
    }

    SimpleRenderer::~SimpleRenderer()
    {
        // Wait for GPU to finish before destroying sync objects
        vkDeviceWaitIdle(m_Device.GetLogicalDevice());

        delete m_DepthImage;

        vkDestroySemaphore(m_Device.GetLogicalDevice(), m_ImageAvailableSemaphore, nullptr);
        vkDestroySemaphore(m_Device.GetLogicalDevice(), m_RenderFinishedSemaphore, nullptr);
        vkDestroyFence(m_Device.GetLogicalDevice(), m_InFlightFence, nullptr);
    }

    void SimpleRenderer::InitSyncStructures()
    {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        vkCreateSemaphore(m_Device.GetLogicalDevice(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphore);
        vkCreateSemaphore(m_Device.GetLogicalDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphore);
        vkCreateFence(m_Device.GetLogicalDevice(), &fenceInfo, nullptr, &m_InFlightFence);
    }
void SimpleRenderer::TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
        // 1. Declare and Initialize Barrier FIRST
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;

        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        // 2. Determine Aspect Mask and Destination Stages (Based on New Layout)
        if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        else {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            }
            else if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                barrier.dstAccessMask = 0;
            }
            else {
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
            }
        }

        // 3. Determine Source Stages (Based on Old Layout)
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            barrier.srcAccessMask = 0;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        else {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        }

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(m_CommandBuffer, &depInfo);
    }

    void SimpleRenderer::BeginFrame()
    {
        // 1. Wait for fence (CPU wait)
        vkWaitForFences(m_Device.GetLogicalDevice(), 1, &m_InFlightFence, VK_TRUE, UINT64_MAX);

        // 2. Acquire Image
        VkResult result = vkAcquireNextImageKHR(
            m_Device.GetLogicalDevice(),
            m_Swapchain.GetHandle(),
            UINT64_MAX,
            m_ImageAvailableSemaphore,
            VK_NULL_HANDLE,
            &m_ImageIndex
        );

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_Swapchain.Recreate();
            return;
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            Core::Log::Error("Failed to acquire swapchain image!");
            return;
        }

        // Reset Fence (We are committing to drawing now)
        vkResetFences(m_Device.GetLogicalDevice(), 1, &m_InFlightFence);
        vkResetCommandBuffer(m_CommandBuffer, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);

        // 3. Get the image we just acquired
        VkImage currentImage = m_Swapchain.GetImages()[m_ImageIndex];

        // 4. Barrier: Undefined -> Color Attachment (Must happen BEFORE BeginRendering)
        TransitionImageLayout(m_DepthImage->GetHandle(),
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        TransitionImageLayout(currentImage,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        // 5. Setup Rendering
        VkClearValue clearColor = {{{0.0f, 1.0f, 0.0f, 1.0f}}}; // Green

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = m_Swapchain.GetImageViews()[m_ImageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue = clearColor;

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = m_DepthImage->GetView();
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {1.0f, 0}; // Far plane is 1.0

        VkRenderingInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea = {{0, 0}, m_Swapchain.GetExtent()};
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;
        renderInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(m_CommandBuffer, &renderInfo);
        m_IsFrameStarted = true;
    }

    void SimpleRenderer::EndFrame()
    {
        if (!m_IsFrameStarted) return;

        vkCmdEndRendering(m_CommandBuffer);

        // 6. Barrier: Color Attachment -> Present Source
        VkImage currentImage = m_Swapchain.GetImages()[m_ImageIndex];
        TransitionImageLayout(currentImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        vkEndCommandBuffer(m_CommandBuffer);

        // 7. Submit
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {m_ImageAvailableSemaphore};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffer;

        VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphore};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        vkQueueSubmit(m_Device.GetGraphicsQueue(), 1, &submitInfo, m_InFlightFence);

        // 8. Present
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapchains[] = {m_Swapchain.GetHandle()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &m_ImageIndex;

        VkResult result = vkQueuePresentKHR(m_Device.GetPresentQueue(), &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            m_Swapchain.Recreate();
            CreateDepthBuffer();
        }

        m_IsFrameStarted = false;
    }

    void SimpleRenderer::BindPipeline(const GraphicsPipeline& pipeline)
    {
        vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());
    }

    void SimpleRenderer::SetViewport(uint32_t width, uint32_t height)
    {
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)width;
        viewport.height = (float)height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {width, height};

        vkCmdSetViewport(m_CommandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(m_CommandBuffer, 0, 1, &scissor);
    }

    void SimpleRenderer::Draw(uint32_t vertexCount)
    {
        vkCmdDraw(m_CommandBuffer, vertexCount, 1, 0, 0);
    }

    void SimpleRenderer::CreateDepthBuffer()
    {
        if (m_DepthImage) delete m_DepthImage;

        VkExtent2D extent = m_Swapchain.GetExtent();
        VkFormat depthFormat = VulkanImage::FindDepthFormat(m_Device);

        m_DepthImage = new VulkanImage(
            m_Device,
            extent.width, extent.height,
            depthFormat,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );
    }
}
