module;
#include <vector>
#include <memory>
#include "RHI.Vulkan.hpp"

export module Graphics:Presentation;

import RHI;
import Core;

export namespace Graphics
{
    // ---------------------------------------------------------------------
    // Presentation System
    // ---------------------------------------------------------------------
    // Manages the Swapchain, Depth Buffers, and Frame Synchronization.
    // It isolates the Window System Integration (WSI) from the rendering logic.
    class PresentationSystem
    {
    public:
        PresentationSystem(std::shared_ptr<RHI::VulkanDevice> device,
                           RHI::VulkanSwapchain& swapchain,
                           RHI::SimpleRenderer& renderer);
        ~PresentationSystem();

        // -----------------------------------------------------------------
        // Frame Lifecycle
        // -----------------------------------------------------------------
        // Returns true if a frame was successfully started.
        // If false (e.g., window minimized), the renderer should skip this frame.
        [[nodiscard]] bool BeginFrame();

        // Submits the command buffer and presents the image.
        void EndFrame();

        // -----------------------------------------------------------------
        // Resources (Valid only between BeginFrame and EndFrame)
        // -----------------------------------------------------------------
        [[nodiscard]] uint32_t GetFrameIndex() const; // 0..N-1 (Frame in Flight)
        [[nodiscard]] uint32_t GetImageIndex() const; // 0..M-1 (Swapchain Image)
        
        [[nodiscard]] VkImage GetBackbuffer() const;
        [[nodiscard]] VkImageView GetBackbufferView() const;
        [[nodiscard]] VkFormat GetBackbufferFormat() const;
        [[nodiscard]] VkExtent2D GetResolution() const;

        // Lazily creates depth buffer if needed (handles resize automatically).
        [[nodiscard]] RHI::VulkanImage& GetDepthBuffer(); 

        [[nodiscard]] VkCommandBuffer GetCommandBuffer() const;

        // Called when window resize is detected.
        void OnResize();

    private:
        std::shared_ptr<RHI::VulkanDevice> m_Device;
        RHI::VulkanSwapchain& m_Swapchain;
        RHI::SimpleRenderer& m_Renderer;

        // Per-frame depth buffers (match swapchain count/resolution).
        std::vector<std::unique_ptr<RHI::VulkanImage>> m_DepthImages;
    };
}
