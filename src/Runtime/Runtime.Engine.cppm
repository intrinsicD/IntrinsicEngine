module;
#include <memory>
#include <string>
#include <RHI/RHI.Vulkan.hpp>

export module Runtime.Engine;

import Core.Window;
import Core.Memory;
import Runtime.RHI.Context;
import Runtime.RHI.Device;
import Runtime.RHI.Swapchain;
import Runtime.RHI.Renderer;
import Runtime.RHI.Pipeline;
import Runtime.RHI.Descriptors;
import Runtime.RHI.Buffer;
import Runtime.Graphics.RenderSystem;
import Runtime.ECS.Scene;

export namespace Runtime
{
    struct EngineConfig
    {
        std::string AppName = "Intrinsic App";
        int Width = 1600;
        int Height = 900;
    };

    class Engine
    {
    public:
        explicit Engine(const EngineConfig& config);
        virtual ~Engine();

        void Run();

        // To be implemented by the Client (Sandbox)
        virtual void OnStart() = 0;
        virtual void OnUpdate(float deltaTime) = 0;
        virtual void OnRender() = 0; // Optional custom rendering hook

    protected:
        // Protected access so Sandbox can manipulate Scene/Assets
        ECS::Scene m_Scene;
        Core::Memory::LinearArena m_FrameArena; // 1 MB per frame
        std::unique_ptr<Graphics::RenderSystem> m_RenderSystem;
        std::unique_ptr<RHI::VulkanDevice> m_Device;

        // Helper to access the camera buffer (Temporary until we have a Camera Component)
        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_RenderSystem->GetGlobalUBO(); }

        // Needed for resource creation
        [[nodiscard]] RHI::VulkanDevice& GetDevice() const { return *m_Device; }
        [[nodiscard]] RHI::DescriptorPool& GetDescriptorPool() const { return *m_DescriptorPool; }
        [[nodiscard]] RHI::DescriptorLayout& GetDescriptorLayout() const { return *m_DescriptorLayout; }
        [[nodiscard]] RHI::VulkanSwapchain& GetSwapchain() const { return *m_Swapchain; }

    protected:
        std::unique_ptr<Core::Windowing::Window> m_Window;
        std::unique_ptr<RHI::VulkanContext> m_Context;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

        std::unique_ptr<RHI::VulkanSwapchain> m_Swapchain;
        std::unique_ptr<RHI::SimpleRenderer> m_Renderer;
        std::unique_ptr<RHI::DescriptorLayout> m_DescriptorLayout;
        std::unique_ptr<RHI::DescriptorPool> m_DescriptorPool;
        std::unique_ptr<RHI::GraphicsPipeline> m_Pipeline;

        bool m_Running = true;
        bool m_FramebufferResized = false;

        void InitVulkan();
        void InitPipeline();
        void LoadDroppedAsset(const std::string& path);
    };
}
