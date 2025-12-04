module;
#include <memory>
#include <string>
#include <vector>
#include <RHI/RHI.Vulkan.hpp>

#include "tiny_gltf.h"

export module Runtime.Engine;

import Core.Window;
import Core.Memory;
import Core.Assets;
import Runtime.RHI.Context;
import Runtime.RHI.Device;
import Runtime.RHI.Swapchain;
import Runtime.RHI.Renderer;
import Runtime.RHI.Pipeline;
import Runtime.RHI.Descriptors;
import Runtime.RHI.Buffer;
import Runtime.RHI.Texture;
import Runtime.Graphics.RenderSystem;
import Runtime.Graphics.Material;
import Runtime.Graphics.Geometry;
import Runtime.ECS.Scene;

export namespace Runtime
{
    struct EngineConfig
    {
        std::string AppName = "Intrinsic App";
        int Width = 1600;
        int Height = 900;
        size_t FrameArenaSize = 1024 * 1024; // Configurable frame arena (default: 1 MB)
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

    //protected:
        // Protected access so Sandbox can manipulate Scene/Assets
        ECS::Scene m_Scene;
        Core::Assets::AssetManager m_AssetManager;
        Core::Memory::LinearArena m_FrameArena; // 1 MB per frame
        std::unique_ptr<Graphics::RenderSystem> m_RenderSystem;
        std::shared_ptr<RHI::VulkanDevice> m_Device;

        std::shared_ptr<RHI::Texture> m_DefaultTexture;

        // Helper to access the camera buffer (Temporary until we have a Camera Component)
        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_RenderSystem->GetGlobalUBO(); }

        // Needed for resource creation
        [[nodiscard]] std::shared_ptr<RHI::VulkanDevice> GetDevice() const { return m_Device; }
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

        std::vector<std::shared_ptr<Graphics::Material>> m_LoadedMaterials;
        std::vector<std::shared_ptr<Graphics::GeometryGpuData>> m_LoadedGeometries;

        bool m_Running = true;
        bool m_FramebufferResized = false;

        void InitPipeline();
        void LoadDroppedAsset(const std::string& path);
    };
}
