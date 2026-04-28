module;
#include <cassert>
#include <chrono>
#include <memory>
#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "RHI.Vulkan.hpp"

module RHI.Renderer;
import RHI.Device;
import RHI.Swapchain;
import RHI.Pipeline;
import RHI.Image;
import RHI.CommandUtils;
import RHI.Profiler;
import Core.Logging;
import Core.Telemetry;

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
        poolInfo.queueFamilyIndex = m_Device->GetQueueIndices().Graphics();

        VK_CHECK_FATAL(vkCreateCommandPool(m_Device->GetLogicalDevice(), &poolInfo, nullptr, &m_CommandPool));

        m_CommandBuffers.resize(m_FramesInFlight);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)m_CommandBuffers.size();

        VK_CHECK_FATAL(vkAllocateCommandBuffers(m_Device->GetLogicalDevice(), &allocInfo, m_CommandBuffers.data()));

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

    void SimpleRenderer::RequestShutdown()
    {
        m_ShutdownRequested = true;
    }

    std::optional<GpuTimestampFrame> SimpleRenderer::ConsumeResolvedGpuProfile()
    {
        return std::exchange(m_ResolvedGpuProfile, std::nullopt);
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
            VK_CHECK_FATAL(vkCreateSemaphore(m_Device->GetLogicalDevice(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]));
            VK_CHECK_FATAL(vkCreateSemaphore(m_Device->GetLogicalDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]));
            VK_CHECK_FATAL(vkCreateFence(m_Device->GetLogicalDevice(), &fenceInfo, nullptr, &m_InFlightFences[i]));
        }
    }

    void SimpleRenderer::BeginFrame()
    {
        if (m_ShutdownRequested)
        {
            m_IsFrameStarted = false;
            return;
        }

        if (!m_Device || m_Device->GetLogicalDevice() == VK_NULL_HANDLE)
        {
            m_IsFrameStarted = false;
            return;
        }

        // 1. Wait for fence (CPU wait) — timed for telemetry
        constexpr uint64_t kFenceWaitTimeoutNs = 16'000'000ull; // ~16 ms: keep the frame loop responsive.
        const auto fenceWaitStart = std::chrono::high_resolution_clock::now();
        const VkResult fenceWaitResult = vkWaitForFences(
            m_Device->GetLogicalDevice(), 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, kFenceWaitTimeoutNs);
        const auto fenceWaitEnd = std::chrono::high_resolution_clock::now();
        const uint64_t fenceWaitNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(fenceWaitEnd - fenceWaitStart).count());

        if (fenceWaitResult != VK_SUCCESS)
        {
            if (fenceWaitResult == VK_TIMEOUT)
            {
                m_IsFrameStarted = false;
                return;
            }

            if (fenceWaitResult == VK_ERROR_DEVICE_LOST)
            {
                Core::Log::Error("SimpleRenderer::BeginFrame(): device lost while waiting for frame fence.");
                RequestShutdown();
                m_IsFrameStarted = false;
                return;
            }

            Core::Log::Error("SimpleRenderer::BeginFrame(): vkWaitForFences failed (VkResult={}).",
                             static_cast<int>(fenceWaitResult));
            m_IsFrameStarted = false;
            return;
        }

        // 2. Reclaim resources that were scheduled on this frame-slot.
        // IMPORTANT: this must happen before advancing the global frame epoch used by SafeDestroy().
        m_Device->FlushDeletionQueue(m_CurrentFrame);

        // 3. Acquire Image — timed for telemetry.
        // Only advance the global frame epoch after acquisition succeeds so skipped
        // frames cannot skew profiler/secondary-buffer reuse or deletion scheduling.
        const auto acquireStart = std::chrono::high_resolution_clock::now();
        VkResult result = vkAcquireNextImageKHR(
            m_Device->GetLogicalDevice(),
            m_Swapchain.GetHandle(),
            UINT64_MAX,
            m_ImageAvailableSemaphores[m_CurrentFrame],
            VK_NULL_HANDLE,
            &m_ImageIndex
        );
        const auto acquireEnd = std::chrono::high_resolution_clock::now();
        const uint64_t acquireNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(acquireEnd - acquireStart).count());

        // Cache fence + acquire timings; present timing added in EndFrame.
        m_LastFenceWaitNs = fenceWaitNs;
        m_LastAcquireNs = acquireNs;
        Core::Telemetry::TelemetrySystem::Get().SetFramesInFlightCount(m_FramesInFlight);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_Swapchain.Recreate();
            m_IsFrameStarted = false;
            return;
        }
        if (result == VK_SUBOPTIMAL_KHR)
        {
            // Keep startup/resize behavior deterministic: rebuild on acquire-side
            // suboptimal and skip this frame so all framebuffer-sized resources
            // are recreated from a single coherent extent.
            m_Swapchain.Recreate();
            m_IsFrameStarted = false;
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            Core::Log::Error("Failed to acquire swapchain image!");
            m_IsFrameStarted = false;
            return;
        }

        // Now that this frame is definitely live, advance the global epoch for any
        // deferred work scheduled during command-buffer recording.
        m_Device->IncrementGlobalFrame();

        VK_CHECK_FATAL(vkResetFences(m_Device->GetLogicalDevice(), 1, &m_InFlightFences[m_CurrentFrame]));
        VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];
        VK_CHECK_FATAL(vkResetCommandBuffer(cmd, 0));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK_FATAL(vkBeginCommandBuffer(cmd, &beginInfo));

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

        VK_CHECK_FATAL(vkEndCommandBuffer(cmd));

        // Attach a timeline signal to this submit.
        const uint64_t signalValue = m_Device->SignalGraphicsTimeline();

        VkTimelineSemaphoreSubmitInfo timelineSubmit{};
        timelineSubmit.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;

        // We signal 2 semaphores (binary + timeline). The timeline submit info must provide a value
        // for *each* signaled semaphore. Values for binary semaphores are ignored by Vulkan.
        const uint64_t signalValues[] = { 0ull, signalValue };
        timelineSubmit.signalSemaphoreValueCount = 2;
        timelineSubmit.pSignalSemaphoreValues = signalValues;

        VkSemaphore timelineSem = m_Device->GetGraphicsTimelineSemaphore();
        VkSemaphore submitSignalSemaphores[] = { m_RenderFinishedSemaphores[m_CurrentFrame], timelineSem };

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = &timelineSubmit;

        VkSemaphore waitSemaphores[] = {m_ImageAvailableSemaphores[m_CurrentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        // Signal render-finished + timeline.
        submitInfo.signalSemaphoreCount = 2;
        submitInfo.pSignalSemaphores = submitSignalSemaphores;

        VK_CHECK_FATAL(m_Device->SubmitToGraphicsQueue(submitInfo, m_InFlightFences[m_CurrentFrame]));

        // --- Non-blocking resolve of an older frame (avoid stalls) ---
        // Cache the result for the orchestrator to consume per-FrameContext (B4.9).
        if (m_GpuProfiler)
        {
            const uint32_t framesInFlight = m_FramesInFlight;
            const uint32_t resolveFrame = (m_CurrentFrame + 1u) % framesInFlight;

            if (auto resolved = m_GpuProfiler->Resolve(resolveFrame); resolved.has_value())
                m_ResolvedGpuProfile = std::move(*resolved);
        }

        // 8. Present
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        // Present can only wait on *binary* semaphores.
        VkSemaphore presentWaitSemaphores[] = { m_RenderFinishedSemaphores[m_CurrentFrame] };
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = presentWaitSemaphores;

        VkSwapchainKHR swapchains[] = {m_Swapchain.GetHandle()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &m_ImageIndex;

        const auto presentStart = std::chrono::high_resolution_clock::now();
        VkResult presentResult = m_Device->Present(presentInfo);
        const auto presentEnd = std::chrono::high_resolution_clock::now();
        const uint64_t presentNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(presentEnd - presentStart).count());

        // Report all three present-path timings together.
        Core::Telemetry::TelemetrySystem::Get().SetPresentTimings(m_LastFenceWaitNs, m_LastAcquireNs, presentNs);

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
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
        // Use timeline-semaphore-based drain instead of vkDeviceWaitIdle.
        // This only waits for submitted graphics-queue work rather than stalling all queues,
        // so concurrent transfer-queue uploads can continue uninterrupted during resize.
        if (!m_Device->WaitForGraphicsIdle())
        {
            // Fallback: if timeline wait fails, drain everything to avoid UB.
            vkDeviceWaitIdle(m_Device->GetLogicalDevice());
        }
        m_Swapchain.Recreate();
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

    void SimpleRenderer::CopyPixel_R32_UINT_ToBuffer(VkImage srcImage,
                                                    uint32_t srcWidth,
                                                    uint32_t srcHeight,
                                                    uint32_t x,
                                                    uint32_t y,
                                                    VkBuffer dstBuffer)
    {
        if (!m_IsFrameStarted) return;
        if (srcImage == VK_NULL_HANDLE || dstBuffer == VK_NULL_HANDLE) return;
        if (srcWidth == 0 || srcHeight == 0) return;

        x = std::min(x, srcWidth - 1u);
        y = std::min(y, srcHeight - 1u);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;    // tightly packed
        region.bufferImageHeight = 0;  // tightly packed
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { static_cast<int32_t>(x), static_cast<int32_t>(y), 0 };
        region.imageExtent = { 1, 1, 1 };

        vkCmdCopyImageToBuffer(
            m_CommandBuffers[m_CurrentFrame],
            srcImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            dstBuffer,
            1,
            &region);
    }
}
