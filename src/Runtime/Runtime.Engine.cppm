module;
#include <memory>
#include <string>
#include <vector>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

#include "tiny_gltf.h"

export module Runtime.Engine;

import Core;
import RHI;
import Graphics;
import ECS;
import Runtime.SelectionModule;

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
        entt::entity SpawnModel(Core::Assets::AssetHandle modelHandle,
                                Core::Assets::AssetHandle materialHandle,
                                glm::vec3 position,
                                glm::vec3 scale = glm::vec3(1.0f));

    protected:
        std::unique_ptr<Core::Windowing::Window> m_Window;
        std::unique_ptr<RHI::VulkanContext> m_Context;
        std::shared_ptr<RHI::VulkanDevice> m_Device;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

        // Managers that depend on Device
        std::unique_ptr<RHI::VulkanSwapchain> m_Swapchain;
        std::unique_ptr<RHI::SimpleRenderer> m_Renderer;
        std::unique_ptr<RHI::TransferManager> m_TransferManager;

        std::unique_ptr<RHI::DescriptorLayout> m_DescriptorLayout;
        std::unique_ptr<RHI::DescriptorAllocator> m_DescriptorPool;
        std::unique_ptr<RHI::GraphicsPipeline> m_Pipeline;
        std::unique_ptr<RHI::GraphicsPipeline> m_PickPipeline;
        std::unique_ptr<RHI::BindlessDescriptorSystem> m_BindlessSystem;
        std::unique_ptr<RHI::TextureSystem> m_TextureSystem;

    public:
        // Protected access so Sandbox can manipulate Scene/Assets
        ECS::Scene m_Scene;
        Core::Assets::AssetManager m_AssetManager;
        Core::Memory::LinearArena m_FrameArena; // 1 MB per frame
        Core::Memory::ScopeStack m_FrameScope; // per-frame scope allocator with destructors
        Graphics::GeometryPool m_GeometryStorage;
        std::unique_ptr<Graphics::RenderSystem> m_RenderSystem;
        std::unique_ptr<Graphics::MaterialSystem> m_MaterialSystem;
        // Engine-owned selection controller (Editor-like single selection).
        SelectionModule m_Selection;

        [[nodiscard]] SelectionModule& GetSelection() { return m_Selection; }
        [[nodiscard]] const SelectionModule& GetSelection() const { return m_Selection; }

        // Helper to access the camera buffer (Temporary until we have a Camera Component)
        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_RenderSystem->GetGlobalUBO(); }

        // Needed for resource creation
        [[nodiscard]] std::shared_ptr<RHI::VulkanDevice> GetDevice() const { return m_Device; }
        [[nodiscard]] RHI::DescriptorAllocator& GetDescriptorPool() const { return *m_DescriptorPool; }
        [[nodiscard]] RHI::DescriptorLayout& GetDescriptorLayout() const { return *m_DescriptorLayout; }
        [[nodiscard]] RHI::VulkanSwapchain& GetSwapchain() const { return *m_Swapchain; }
        [[nodiscard]] Graphics::GeometryPool& GetGeometryStorage() { return m_GeometryStorage; }

        void RegisterAssetLoad(Core::Assets::AssetHandle handle, RHI::TransferToken token);

        template <typename F>
        void RunOnMainThread(F&& task)
        {
            std::lock_guard lock(m_MainThreadQueueMutex);
            m_MainThreadQueue.emplace_back(Core::Tasks::LocalTask(std::forward<F>(task)));
        }

    protected:
        std::shared_ptr<RHI::Texture> m_DefaultTexture;
        uint32_t m_DefaultTextureIndex = 0;

        // Keep-alive list for runtime-created materials is handle-based to avoid shared_ptr overhead.
        // The AssetManager owns the actual material payload.
        std::vector<Core::Assets::AssetHandle> m_LoadedMaterials;

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
        std::vector<Core::Tasks::LocalTask> m_MainThreadQueue;

        bool m_Running = true;
        bool m_FramebufferResized = false;

        void InitPipeline();
        void LoadDroppedAsset(const std::string& path);
        void ProcessUploads();
        void ProcessMainThreadQueue();
    };
}
