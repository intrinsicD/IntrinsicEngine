module;

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "RHI.Vulkan.hpp"

export module Graphics.ShaderHotReload;

import Core.FeatureRegistry;
import Graphics.PipelineLibrary;
import Graphics.ShaderRegistry;
import RHI.Device;

export namespace Graphics
{
    // Shader hot-reload service.
    //
    // Watches GLSL source files registered in the ShaderRegistry, recompiles
    // changed shaders to SPIR-V via glslc, and signals for pipeline rebuild.
    //
    // Thread model:
    //   - FileWatcher callbacks run on the watcher thread. They only set an
    //     atomic flag + invoke the compiler (a child process, thread-safe).
    //   - PollAndReload() must be called from the main thread (maintenance lane).
    //     It checks the flag, drains GPU, and rebuilds pipelines.
    //
    // Lifecycle:
    //   - Constructed during engine init.
    //   - Start() registers file watches (call after ShaderRegistry is populated).
    //   - PollAndReload() called once per frame from BookkeepHotReloads().
    //   - Destructor is safe (FileWatcher::Shutdown handles thread join).
    class ShaderHotReloadService
    {
    public:
        ShaderHotReloadService(std::shared_ptr<RHI::VulkanDevice> device,
                               PipelineLibrary& pipelineLibrary,
                               const ShaderRegistry& shaderRegistry);
        ~ShaderHotReloadService();

        // Non-copyable, non-movable.
        ShaderHotReloadService(const ShaderHotReloadService&) = delete;
        ShaderHotReloadService& operator=(const ShaderHotReloadService&) = delete;

        // Register FileWatcher entries for all shaders with source paths.
        // Also resolves the shader include directory for glslc -I flag.
        void Start();

        // Called from the main thread maintenance lane.
        // If any shaders were recompiled since the last call, drains
        // the GPU and rebuilds all graphics pipelines.
        void PollAndReload(VkFormat swapchainFormat, VkFormat depthFormat);

        // Number of successful recompilations since Start().
        [[nodiscard]] uint32_t ReloadCount() const { return m_ReloadCount.load(std::memory_order_relaxed); }

        // True if a reload is pending (shader recompiled, pipelines not yet rebuilt).
        [[nodiscard]] bool ReloadPending() const { return m_ReloadPending.load(std::memory_order_relaxed); }

    private:
        std::shared_ptr<RHI::VulkanDevice> m_Device;
        PipelineLibrary& m_PipelineLibrary;
        const ShaderRegistry& m_ShaderRegistry;

        // Resolved once in Start(): the assets/shaders/ directory for glslc -I.
        std::string m_ShaderIncludeDir;

        std::atomic<bool> m_ReloadPending{false};
        std::atomic<uint32_t> m_ReloadCount{0};
        // Steady-clock nanosecond timestamp of the last successful compilation.
        // Used for debounce coalescing in PollAndReload().
        std::atomic<int64_t> m_LastChangeTime{0};
    };
}
