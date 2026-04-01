module;
#include <memory>
#include <string>
#include <cstdint>

#include "RHI.Vulkan.hpp"

export module Runtime.GraphicsBackend;

import Core.Window;
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
import RHI.TextureManager;
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
        [[nodiscard]] RHI::VulkanContext& GetContext() const;
        // Borrowed device access for runtime hot paths.
        [[nodiscard]] RHI::VulkanDevice& GetDevice() const;
        // Explicit owning access for APIs that still require shared ownership.
        [[nodiscard]] const std::shared_ptr<RHI::VulkanDevice>& GetDeviceShared() const;
        [[nodiscard]] RHI::VulkanSwapchain& GetSwapchain() const;
        [[nodiscard]] RHI::SimpleRenderer& GetRenderer() const;
        [[nodiscard]] RHI::TransferManager& GetTransferManager() const;
        [[nodiscard]] RHI::DescriptorLayout& GetDescriptorLayout() const;
        [[nodiscard]] RHI::DescriptorAllocator& GetDescriptorPool() const;
        [[nodiscard]] RHI::BindlessDescriptorSystem& GetBindlessSystem() const;
        [[nodiscard]] RHI::TextureManager& GetTextureManager() const;
        [[nodiscard]] uint32_t GetDefaultTextureIndex() const;
#ifdef INTRINSIC_HAS_CUDA
        [[nodiscard]] RHI::CudaDevice* GetCudaDevice() const;
#endif

        // --- Present policy ---
        void SetPresentPolicy(RHI::PresentPolicy policy);
        [[nodiscard]] RHI::PresentPolicy GetPresentPolicy() const;

        // --- Per-frame maintenance ---
        void OnResize();
        void CollectGpuDeferredDestructions();
        void GarbageCollectTransfers();
        void ProcessTextureDeletions();

        // --- Shutdown helpers ---
        void WaitIdle();
        void FlushDeletionQueues();

        // Clear texture pool (call during teardown after all textures are released).
        void ClearTextureManager();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
