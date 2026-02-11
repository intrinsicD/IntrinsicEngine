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
import Runtime.GraphicsBackend;

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

        // Override to register additional systems into the FrameGraph each frame.
        // Called after the engine's core systems are registered but before Compile/Execute.
        // Client systems (e.g. AxisRotator, gameplay logic) should be registered here.
        virtual void OnRegisterSystems(Core::FrameGraph& graph, float deltaTime) {}
        entt::entity SpawnModel(Core::Assets::AssetHandle modelHandle,
                                Core::Assets::AssetHandle materialHandle,
                                glm::vec3 position,
                                glm::vec3 scale = glm::vec3(1.0f));

    protected:
        std::unique_ptr<Core::Windowing::Window> m_Window;

        // All Vulkan/GPU infrastructure: context, device, swapchain, descriptors, etc.
        std::unique_ptr<GraphicsBackend> m_GraphicsBackend;

        // Retained-mode GPU scene (persistent SSBOs managed by Graphics::RenderSystem).
        // Owned by Engine to allow SpawnModel/ECS to allocate slots and queue updates.
        std::unique_ptr<Graphics::GPUScene> m_GpuScene;

        Graphics::ShaderRegistry m_ShaderRegistry;
        std::unique_ptr<Graphics::PipelineLibrary> m_PipelineLibrary;

    public:
        // Public access so Sandbox can manipulate Scene/Assets
        ECS::Scene m_Scene;
        Core::Assets::AssetManager m_AssetManager;
        Core::Memory::LinearArena m_FrameArena; // 1 MB per frame
        Core::Memory::ScopeStack m_FrameScope; // per-frame scope allocator with destructors
        Core::FrameGraph m_FrameGraph;         // CPU-side system scheduling DAG (uses m_FrameScope)
        Graphics::GeometryPool m_GeometryStorage;
        std::unique_ptr<Graphics::RenderSystem> m_RenderSystem;
        std::unique_ptr<Graphics::MaterialSystem> m_MaterialSystem;
        // Engine-owned selection controller (Editor-like single selection).
        SelectionModule m_Selection;

        [[nodiscard]] SelectionModule& GetSelection() { return m_Selection; }
        [[nodiscard]] const SelectionModule& GetSelection() const { return m_Selection; }

        // Access to the GraphicsBackend subsystem.
        [[nodiscard]] GraphicsBackend& GetGraphicsBackend() const { return *m_GraphicsBackend; }

        // Helper to access the camera buffer (Temporary until we have a Camera Component)
        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_RenderSystem->GetGlobalUBO(); }

        // Convenience accessors that delegate to GraphicsBackend.
        [[nodiscard]] std::shared_ptr<RHI::VulkanDevice> GetDevice() const { return m_GraphicsBackend->GetDevice(); }
        [[nodiscard]] RHI::DescriptorAllocator& GetDescriptorPool() const { return m_GraphicsBackend->GetDescriptorPool(); }
        [[nodiscard]] RHI::DescriptorLayout& GetDescriptorLayout() const { return m_GraphicsBackend->GetDescriptorLayout(); }
        [[nodiscard]] RHI::VulkanSwapchain& GetSwapchain() const { return m_GraphicsBackend->GetSwapchain(); }
        [[nodiscard]] Graphics::GeometryPool& GetGeometryStorage() { return m_GeometryStorage; }

        void RegisterAssetLoad(Core::Assets::AssetHandle handle, RHI::TransferToken token);

        template <typename F>
        void RegisterAssetLoad(Core::Assets::AssetHandle handle, RHI::TransferToken token, F&& onComplete)
        {
            std::lock_guard lock(m_LoadMutex);
            PendingLoad l{};
            l.Handle = handle;
            l.Token = token;
            l.OnComplete = Core::Tasks::LocalTask(std::forward<F>(onComplete));
            m_PendingLoads.push_back(std::move(l));
        }

        template <typename F>
        void RunOnMainThread(F&& task)
        {
            std::lock_guard lock(m_MainThreadQueueMutex);
            m_MainThreadQueue.emplace_back(Core::Tasks::LocalTask(std::forward<F>(task)));
        }

    protected:
        // Keep-alive list for runtime-created materials is handle-based to avoid shared_ptr overhead.
        // The AssetManager owns the actual material payload.
        std::vector<Core::Assets::AssetHandle> m_LoadedMaterials;

        // Internal tracking struct (POD)
        struct PendingLoad
        {
            Core::Assets::AssetHandle Handle;
            RHI::TransferToken Token;
            Core::Tasks::LocalTask OnComplete{}; // optional main-thread completion work
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
