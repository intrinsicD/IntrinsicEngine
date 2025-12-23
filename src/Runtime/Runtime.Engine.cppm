module;
#include <memory>
#include <string>
#include <vector>
#include <functional>

#include <RHI/RHI.Vulkan.hpp>

#include "tiny_gltf.h"

export module Runtime.Engine;

import Core.Window;
import Core.Memory;
import Core.Assets;
import Runtime.RHI.Bindless;
import Runtime.RHI.Context;
import Runtime.RHI.Device;
import Runtime.RHI.Swapchain;
import Runtime.RHI.Renderer;
import Runtime.RHI.Pipeline;
import Runtime.RHI.Descriptors;
import Runtime.RHI.Buffer;
import Runtime.RHI.Texture;
import Runtime.RHI.Transfer;
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
        Graphics::GeometryStorage m_GeometryStorage;
        std::unique_ptr<Graphics::RenderSystem> m_RenderSystem;

        // Helper to access the camera buffer (Temporary until we have a Camera Component)
        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_RenderSystem->GetGlobalUBO(); }

        // Needed for resource creation
        [[nodiscard]] std::shared_ptr<RHI::VulkanDevice> GetDevice() const { return m_Device; }
        [[nodiscard]] RHI::DescriptorPool& GetDescriptorPool() const { return *m_DescriptorPool; }
        [[nodiscard]] RHI::DescriptorLayout& GetDescriptorLayout() const { return *m_DescriptorLayout; }
        [[nodiscard]] RHI::VulkanSwapchain& GetSwapchain() const { return *m_Swapchain; }
        [[nodiscard]] Graphics::GeometryStorage& GetGeometryStorage() { return m_GeometryStorage; }

        void RegisterAssetLoad(Core::Assets::AssetHandle handle, RHI::TransferToken token);
        void RunOnMainThread(std::function<void()> task);
    protected:
        std::unique_ptr<Core::Windowing::Window> m_Window;
        std::unique_ptr<RHI::VulkanContext> m_Context;
        std::shared_ptr<RHI::VulkanDevice> m_Device;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

        std::unique_ptr<RHI::VulkanSwapchain> m_Swapchain;
        std::unique_ptr<RHI::SimpleRenderer> m_Renderer;
        std::unique_ptr<RHI::DescriptorLayout> m_DescriptorLayout;
        std::unique_ptr<RHI::DescriptorPool> m_DescriptorPool;
        std::unique_ptr<RHI::GraphicsPipeline> m_Pipeline;
        std::unique_ptr<RHI::BindlessDescriptorSystem> m_BindlessSystem;

        std::shared_ptr<RHI::Texture> m_DefaultTexture;
        std::vector<std::shared_ptr<Graphics::Material>> m_LoadedMaterials;
        std::unique_ptr<RHI::TransferManager> m_TransferManager;

        // Internal tracking struct (POD)
        struct PendingLoad
        {
            Core::Assets::AssetHandle Handle;
            RHI::TransferToken Token;
        };

        // Protected by mutex because Loaders call RegisterAssetLoad from worker threads
        std::mutex m_LoadMutex;
        std::vector<PendingLoad> m_PendingLoads;

        std::mutex m_MainThreadQueueMutex;
        std::vector<std::function<void()>> m_MainThreadQueue;
        void ProcessMainThreadQueue();

        bool m_Running = true;
        bool m_FramebufferResized = false;

        void InitPipeline();
        void LoadDroppedAsset(const std::string& path);
        void ProcessUploads();
    };
}
