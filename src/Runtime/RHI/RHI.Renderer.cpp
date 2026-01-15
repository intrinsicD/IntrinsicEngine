module;
#include "RHI.Vulkan.hpp"
#include <mutex>
#include <memory>

module RHI:Renderer.Impl;
import :Renderer;
import :Image;
import :CommandUtils;
import :Profiler;
import Core;

namespace RHI
{
    SimpleRenderer::SimpleRenderer(std::shared_ptr<VulkanDevice> device, VulkanSwapchain& swapchain)
        : m_Device(device), m_Swapchain(swapchain)
    {
        m_FramesInFlight = m_Device->GetFramesInFlight();
        InitSyncStructures();

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_Device->GetQueueIndices().GraphicsFamily.value();

        VK_CHECK(vkCreateCommandPool(m_Device->GetLogicalDevice(), &poolInfo, nullptr, &m_CommandPool));

        m_CommandBuffers.resize(m_FramesInFlight);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)m_CommandBuffers.size();

        VK_CHECK(vkAllocateCommandBuffers(m_Device->GetLogicalDevice(), &allocInfo, m_CommandBuffers.data()));

        // Optional GPU timestamping; safe to keep null if unsupported.
        m_GpuProfiler = std::make_unique<GpuProfiler>(m_Device);
        if (m_GpuProfiler && !m_GpuProfiler->IsSupported())
        {
            m_GpuProfiler.reset();
        }
    }

    SimpleRenderer::~SimpleRenderer()
    {
        // Wait for GPU to finish before destroying sync objects
        vkDeviceWaitIdle(m_Device->GetLogicalDevice());

        for (size_t i = 0; i < m_FramesInFlight; i++)
        {
            vkDestroySemaphore(m_Device->GetLogicalDevice(), m_ImageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(m_Device->GetLogicalDevice(), m_RenderFinishedSemaphores[i], nullptr);
            vkDestroyFence(m_Device->GetLogicalDevice(), m_InFlightFences[i], nullptr);
        }

        vkDestroyCommandPool(m_Device->GetLogicalDevice(), m_CommandPool, nullptr);
    }

    void SimpleRenderer::InitSyncStructures()
    {
        m_ImageAvailableSemaphores.resize(m_FramesInFlight);
        m_RenderFinishedSemaphores.resize(m_FramesInFlight);
        m_InFlightFences.resize(m_FramesInFlight);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < m_FramesInFlight; i++)
        {
            VK_CHECK(vkCreateSemaphore(m_Device->GetLogicalDevice(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]));
            VK_CHECK(vkCreateSemaphore(m_Device->GetLogicalDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]));
            VK_CHECK(vkCreateFence(m_Device->GetLogicalDevice(), &fenceInfo, nullptr, &m_InFlightFences[i]));
        }
    }

    void SimpleRenderer::BeginFrame()
    {
        // 1. Wait for fence (CPU wait)
        VK_CHECK(vkWaitForFences(m_Device->GetLogicalDevice(), 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX));

        m_Device->FlushDeletionQueue(m_CurrentFrame);

        // Increment global frame counter for deferred resource tracking
        // This provides a monotonically increasing frame number for safe GPU resource deletion
        m_Device->IncrementFrame();

        // 2. Acquire Image
        VkResult result = vkAcquireNextImageKHR(
            m_Device->GetLogicalDevice(),
            m_Swapchain.GetHandle(),
            UINT64_MAX,
            m_ImageAvailableSemaphores[m_CurrentFrame],
            VK_NULL_HANDLE,
            &m_ImageIndex
        );

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_Swapchain.Recreate();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            Core::Log::Error("Failed to acquire swapchain image!");
            return;
        }

        VK_CHECK(vkResetFences(m_Device->GetLogicalDevice(), 1, &m_InFlightFences[m_CurrentFrame]));
        VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];
        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

        if (m_GpuProfiler)
        {
            // Reset query range and write a frame start timestamp.
            m_GpuProfiler->BeginFrame(cmd, m_CurrentFrame, 256);
            m_GpuProfiler->WriteFrameStart(cmd);
        }

        // Ensure the acquired swapchain image is in COLOR_ATTACHMENT_OPTIMAL before any rendering.
        // We do this here so RenderGraph can treat the backbuffer as ready for use.
        //
        // Synchronization note (important): ordering vs the presentation engine is established by the
        // acquire semaphore waited on at queue submit. The wait stage mask MUST be at or before the
        // first command that touches the swapchain image. Since this layout transition happens right
        // at the start of the command buffer, we must not use a later wait stage like
        // COLOR_ATTACHMENT_OUTPUT only.
        {
            VkImage currentImage = m_Swapchain.GetImages()[m_ImageIndex];

            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = currentImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            // No prior GPU work in this submission has accessed the image; the acquire semaphore wait
            // (specified at submit) provides the external ordering with the presentation engine.
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            barrier.srcAccessMask = 0;

            // Destination: we will write it as a color attachment.
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

            VkDependencyInfo depInfo{};
            depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &barrier;

            vkCmdPipelineBarrier2(cmd, &depInfo);
        }

        m_IsFrameStarted = true;
    }

    void SimpleRenderer::EndFrame()
    {
        if (!m_IsFrameStarted) return;

        VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];

        // Transition swapchain image from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR
        // We need to ensure all color attachment writes complete before presenting.
        VkImage currentImage = m_Swapchain.GetImages()[m_ImageIndex];

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = currentImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        // Source: Wait for all color attachment writes to complete
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

        // Destination: Bottom of pipe / no specific access needed before present
        // The render finished semaphore handles synchronization with present
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        barrier.dstAccessMask = 0;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &depInfo);

        if (m_GpuProfiler)
        {
            // Frame end timestamp should be as late as possible in this command buffer.
            m_GpuProfiler->WriteFrameEnd(cmd);
        }

        VK_CHECK(vkEndCommandBuffer(cmd));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {m_ImageAvailableSemaphores[m_CurrentFrame]};

        // Must be <= earliest swapchain image use in the command buffer (the BeginFrame layout transition).
        // Using TOP_OF_PIPE keeps vertex/compute overlap with presentation while still correctly gating the
        // first access to the acquired image.
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphores[m_CurrentFrame]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        VK_CHECK(m_Device->SubmitToGraphicsQueue(submitInfo, m_InFlightFences[m_CurrentFrame]));

        // --- Non-blocking resolve of an older frame (avoid stalls) ---
        if (m_GpuProfiler)
        {
            const uint32_t framesInFlight = m_FramesInFlight;
            const uint32_t resolveFrame = (m_CurrentFrame + 1u) % framesInFlight;

            if (auto resolved = m_GpuProfiler->Resolve(resolveFrame); resolved.has_value())
            {
                Core::Telemetry::TelemetrySystem::Get().SetGpuFrameTimeNs(resolved->GpuFrameTimeNs);
            }
        }

        // 8. Present
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapchains[] = {m_Swapchain.GetHandle()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &m_ImageIndex;

        // --- NEW: Lock Queue for Present ---
        // (Technically PresentQueue might be different, but typically it's the same in this simple setup.
        // Even if different, locking the Graphics queue mutex here is safe enough or we should have a PresentMutex)
        VkResult result = m_Device->Present(presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            m_Swapchain.Recreate();
        }

        m_IsFrameStarted = false;
        m_CurrentFrame = (m_CurrentFrame + 1) % m_FramesInFlight;
    }

    void SimpleRenderer::BindPipeline(const GraphicsPipeline& pipeline)
    {
        vkCmdBindPipeline(m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());
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

        vkCmdSetViewport(m_CommandBuffers[m_CurrentFrame], 0, 1, &viewport);
        vkCmdSetScissor(m_CommandBuffers[m_CurrentFrame], 0, 1, &scissor);
    }

    void SimpleRenderer::OnResize()
    {
        vkDeviceWaitIdle(m_Device->GetLogicalDevice());
        m_Swapchain.Recreate();
        // CreateDepthBuffer(); // No longer needed
    }

    void SimpleRenderer::Draw(uint32_t vertexCount)
    {
        vkCmdDraw(m_CommandBuffers[m_CurrentFrame], vertexCount, 1, 0, 0);
    }

    VkImage SimpleRenderer::GetSwapchainImage(uint32_t index) const
    {
        if (index >= m_Swapchain.GetImages().size()) return VK_NULL_HANDLE;
        return m_Swapchain.GetImages()[index];
    }

    VkImageView SimpleRenderer::GetSwapchainImageView(uint32_t index) const
    {
        if (index >= m_Swapchain.GetImageViews().size()) return VK_NULL_HANDLE;
        return m_Swapchain.GetImageViews()[index];
    }
}
