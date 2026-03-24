module;
#include <memory>
#include <string>
#include <cstdint>

#include "RHI.Vulkan.hpp"

export module Runtime.GraphicsBackend;

import Core.Window;
import Core.Logging;
import RHI.Bindless;
import RHI.Context;
#ifdef INTRINSIC_HAS_CUDA
import RHI.CudaDevice;
#endif
import RHI.Descriptors;
import RHI.Device;
import RHI.Renderer;
import RHI.Swapchain;
import RHI.Texture;
import RHI.TextureSystem;
import RHI.Transfer;

export namespace Runtime
{
    struct GraphicsBackendConfig
    {
        std::string AppName = "Intrinsic App";
        bool EnableValidation = true;
    };

    // Owns the Vulkan context, device, swapchain, presentation, transfer,
    // descriptor plumbing, bindless system, texture system, and the engine
    // default texture.  Encapsulates construction & destruction order so the
    // Engine class no longer needs to manage individual GPU resource lifetimes.
    class GraphicsBackend
    {
    public:
        // Construct the full GPU stack.  Requires a valid, already-created window.
        GraphicsBackend(Core::Windowing::Window& window, const GraphicsBackendConfig& config);
        ~GraphicsBackend();

        // Non-copyable, non-movable (owns Vulkan resources).
        GraphicsBackend(const GraphicsBackend&) = delete;
        GraphicsBackend& operator=(const GraphicsBackend&) = delete;
        GraphicsBackend(GraphicsBackend&&) = delete;
        GraphicsBackend& operator=(GraphicsBackend&&) = delete;

        // --- Accessors (non-owning references) ---
        [[nodiscard]] RHI::VulkanContext& GetContext() const { return *m_Context; }
        // Borrowed device access for runtime hot paths.
        [[nodiscard]] RHI::VulkanDevice& GetDevice() const { return *m_Device; }
        // Explicit owning access for APIs that still require shared ownership.
        [[nodiscard]] const std::shared_ptr<RHI::VulkanDevice>& GetDeviceShared() const { return m_Device; }
        [[nodiscard]] RHI::VulkanSwapchain& GetSwapchain() const { return *m_Swapchain; }
        [[nodiscard]] RHI::SimpleRenderer& GetRenderer() const { return *m_Renderer; }
        [[nodiscard]] RHI::TransferManager& GetTransferManager() const { return *m_TransferManager; }
        [[nodiscard]] RHI::DescriptorLayout& GetDescriptorLayout() const { return *m_DescriptorLayout; }
        [[nodiscard]] RHI::DescriptorAllocator& GetDescriptorPool() const { return *m_DescriptorPool; }
        [[nodiscard]] RHI::BindlessDescriptorSystem& GetBindlessSystem() const { return *m_BindlessSystem; }
        [[nodiscard]] RHI::TextureSystem& GetTextureSystem() const { return *m_TextureSystem; }
        [[nodiscard]] uint32_t GetDefaultTextureIndex() const { return m_DefaultTextureIndex; }
#ifdef INTRINSIC_HAS_CUDA
        [[nodiscard]] RHI::CudaDevice* GetCudaDevice() const { return m_CudaDevice.get(); }
#endif

        // --- Per-frame maintenance ---
        void OnResize();
        void CollectGpuDeferredDestructions();
        void GarbageCollectTransfers();
        void ProcessTextureDeletions();

        // --- Shutdown helpers ---
        void WaitIdle();
        void FlushDeletionQueues();

        // Clear texture pool (call during teardown after all textures are released).
        void ClearTextureSystem();

    private:
        // Vulkan instance & debug layers.
        std::unique_ptr<RHI::VulkanContext> m_Context;

        // Logical device + frame-in-flight tracking.
        std::shared_ptr<RHI::VulkanDevice> m_Device;

        // Window surface for presentation.
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

        // Swapchain & presentation.
        std::unique_ptr<RHI::VulkanSwapchain> m_Swapchain;
        std::unique_ptr<RHI::SimpleRenderer> m_Renderer;

        // Async GPU transfer manager (staging belt + timeline semaphores).
        std::unique_ptr<RHI::TransferManager> m_TransferManager;

        // Descriptor plumbing.
        std::unique_ptr<RHI::DescriptorLayout> m_DescriptorLayout;
        std::unique_ptr<RHI::DescriptorAllocator> m_DescriptorPool;

        // Bindless descriptor indexing.
        std::unique_ptr<RHI::BindlessDescriptorSystem> m_BindlessSystem;

        // Texture pooling + bindless slot management.
        std::unique_ptr<RHI::TextureSystem> m_TextureSystem;

        // Engine-owned default 1x1 white texture (bindless slot 0).
        std::shared_ptr<RHI::Texture> m_DefaultTexture;
        uint32_t m_DefaultTextureIndex = 0;

#ifdef INTRINSIC_HAS_CUDA
        std::unique_ptr<RHI::CudaDevice> m_CudaDevice;
#endif

        // Back-reference to the window (needed for surface destruction).
        Core::Windowing::Window* m_Window = nullptr;

        void CreateDefaultTexture();
    };
}
