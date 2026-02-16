module;
#include <memory>
#include <string>
#include <vector>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

#include "tiny_gltf.h"

export module Runtime.Engine;

import Core.Window;
import Core.FrameGraph;
import Core.Assets;
import Core.Memory;
import Core.FeatureRegistry;
import Core.IOBackend;
import RHI;
import Graphics;
import ECS;
import Runtime.SelectionModule;
import Runtime.GraphicsBackend;
import Runtime.AssetPipeline;
import Runtime.SceneManager;
import Runtime.RenderOrchestrator;

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

        // Optional fixed-step hook (default no-op). Called 0..N times per render frame.
        // Intended for deterministic simulation / physics.
        virtual void OnFixedUpdate([[maybe_unused]] float fixedDeltaTime) {}

        virtual void OnRender() = 0; // Optional custom rendering hook

        // Override to register additional systems into the FrameGraph each frame.
        // Called after the engine's core systems are registered but before Compile/Execute.
        // Client systems (e.g. AxisRotator, gameplay logic) should be registered here.
        virtual void OnRegisterSystems([[maybe_unused]] Core::FrameGraph& graph, [[maybe_unused]] float deltaTime) {}

        // Override to register fixed-step systems (e.g. physics) into the FrameGraph.
        // Called 0..N times per render frame, once per fixed tick.
        virtual void OnRegisterFixedSystems([[maybe_unused]] Core::FrameGraph& graph, [[maybe_unused]] float fixedDeltaTime) {}

        entt::entity SpawnModel(Core::Assets::AssetHandle modelHandle,
                                Core::Assets::AssetHandle materialHandle,
                                glm::vec3 position,
                                glm::vec3 scale = glm::vec3(1.0f));

    protected:
        // Central feature catalog: render features, systems, panels, geometry operators.
        Core::FeatureRegistry m_FeatureRegistry;

        std::unique_ptr<Core::Windowing::Window> m_Window;

        // All Vulkan/GPU infrastructure: context, device, swapchain, descriptors, etc.
        std::unique_ptr<GraphicsBackend> m_GraphicsBackend;

        // Asset management: AssetManager, pending transfers, main-thread queue, material tracking.
        std::unique_ptr<AssetPipeline> m_AssetPipeline;

        // ECS scene, entity lifecycle, and EnTT GPU-reclaim hooks.
        std::unique_ptr<SceneManager> m_SceneManager;

        // Render subsystem: ShaderRegistry, PipelineLibrary, GPUScene, RenderSystem,
        // MaterialSystem, per-frame arena/scope/FrameGraph, GeometryPool.
        std::unique_ptr<RenderOrchestrator> m_RenderOrchestrator;

        // I/O backend (Phase 0: loose files via std::ifstream)
        std::unique_ptr<Core::IO::IIOBackend> m_IOBackend;

        // Format loader/exporter registry (populated at startup with built-in loaders)
        Graphics::IORegistry m_IORegistry;

    public:
        // Engine-owned selection controller (Editor-like single selection).
        SelectionModule m_Selection;

        [[nodiscard]] SelectionModule& GetSelection() { return m_Selection; }
        [[nodiscard]] const SelectionModule& GetSelection() const { return m_Selection; }

        // Access to the central feature registry.
        [[nodiscard]] Core::FeatureRegistry& GetFeatureRegistry() { return m_FeatureRegistry; }
        [[nodiscard]] const Core::FeatureRegistry& GetFeatureRegistry() const { return m_FeatureRegistry; }

        // Access to the GraphicsBackend subsystem.
        [[nodiscard]] GraphicsBackend& GetGraphicsBackend() const { return *m_GraphicsBackend; }

        // Access to the AssetPipeline subsystem.
        [[nodiscard]] AssetPipeline& GetAssetPipeline() { return *m_AssetPipeline; }
        [[nodiscard]] const AssetPipeline& GetAssetPipeline() const { return *m_AssetPipeline; }

        // Access to the SceneManager subsystem.
        [[nodiscard]] SceneManager& GetSceneManager() { return *m_SceneManager; }
        [[nodiscard]] const SceneManager& GetSceneManager() const { return *m_SceneManager; }

        // Access to the RenderOrchestrator subsystem.
        [[nodiscard]] RenderOrchestrator& GetRenderOrchestrator() { return *m_RenderOrchestrator; }
        [[nodiscard]] const RenderOrchestrator& GetRenderOrchestrator() const { return *m_RenderOrchestrator; }

        // Convenience: direct access to the ECS scene (delegates to SceneManager).
        [[nodiscard]] ECS::Scene& GetScene() { return m_SceneManager->GetScene(); }
        [[nodiscard]] const ECS::Scene& GetScene() const { return m_SceneManager->GetScene(); }

        // Convenience accessor: delegates to AssetPipeline.
        [[nodiscard]] Core::Assets::AssetManager& GetAssetManager() { return m_AssetPipeline->GetAssetManager(); }
        [[nodiscard]] const Core::Assets::AssetManager& GetAssetManager() const { return m_AssetPipeline->GetAssetManager(); }

        // Helper to access the camera buffer (Temporary until we have a Camera Component)
        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_RenderOrchestrator->GetRenderSystem().GetGlobalUBO(); }

        // Convenience accessors that delegate to GraphicsBackend.
        [[nodiscard]] std::shared_ptr<RHI::VulkanDevice> GetDevice() const { return m_GraphicsBackend->GetDevice(); }
        [[nodiscard]] RHI::DescriptorAllocator& GetDescriptorPool() const { return m_GraphicsBackend->GetDescriptorPool(); }
        [[nodiscard]] RHI::DescriptorLayout& GetDescriptorLayout() const { return m_GraphicsBackend->GetDescriptorLayout(); }
        [[nodiscard]] RHI::VulkanSwapchain& GetSwapchain() const { return m_GraphicsBackend->GetSwapchain(); }
        [[nodiscard]] Graphics::GeometryPool& GetGeometryStorage() { return m_RenderOrchestrator->GetGeometryStorage(); }

        // Access to the I/O subsystem.
        [[nodiscard]] Core::IO::IIOBackend& GetIOBackend() { return *m_IOBackend; }
        [[nodiscard]] const Graphics::IORegistry& GetIORegistry() const { return m_IORegistry; }
        [[nodiscard]] Graphics::IORegistry& GetIORegistry() { return m_IORegistry; }

        // Convenience methods that delegate to AssetPipeline.
        void RegisterAssetLoad(Core::Assets::AssetHandle handle, RHI::TransferToken token)
        {
            m_AssetPipeline->RegisterAssetLoad(handle, token);
        }

        template <typename F>
        void RegisterAssetLoad(Core::Assets::AssetHandle handle, RHI::TransferToken token, F&& onComplete)
        {
            m_AssetPipeline->RegisterAssetLoad(handle, token, std::forward<F>(onComplete));
        }

        template <typename F>
        void RunOnMainThread(F&& task)
        {
            m_AssetPipeline->RunOnMainThread(std::forward<F>(task));
        }

    protected:
        bool m_Running = true;
        bool m_FramebufferResized = false;

        void LoadDroppedAsset(const std::string& path);

    private:
        // Populate the FeatureRegistry with all core engine features
        // (render passes, ECS systems). Called once at the end of the constructor.
        void RegisterCoreFeatures();
    };
}
