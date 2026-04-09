module;

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    // Include-file dependency tracking:
    //   On Start(), scans registered GLSL source files for #include directives,
    //   builds a reverse dependency map (include → dependents), and watches
    //   include files. When an include changes, all dependent shaders are
    //   recompiled.
    //
    // Burst coalescing:
    //   Under rapid file-save bursts, the debounce window (200ms) coalesces
    //   multiple recompilations into a single pipeline rebuild. Additionally,
    //   a max rebuild frequency cap (500ms) prevents excessive GPU drains.
    //
    // Thread model:
    //   - FileWatcher callbacks run on the watcher thread. They only queue
    //     recompilation work and set an atomic flag.
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
        // Scans source files for #include directives and watches include files.
        void Start();

        // Called from the main thread maintenance lane.
        // If any shaders were recompiled since the last call, drains
        // the GPU and rebuilds all graphics pipelines.
        void PollAndReload(VkFormat swapchainFormat, VkFormat depthFormat);

        // Number of successful recompilations since Start().
        [[nodiscard]] uint32_t ReloadCount() const { return m_ReloadCount.load(std::memory_order_relaxed); }

        // True if a reload is pending (shader recompiled, pipelines not yet rebuilt).
        [[nodiscard]] bool ReloadPending() const
        {
            return m_CompileGeneration.load(std::memory_order_relaxed) != m_ProcessedGeneration;
        }

    private:
        std::shared_ptr<RHI::VulkanDevice> m_Device;
        PipelineLibrary& m_PipelineLibrary;
        const ShaderRegistry& m_ShaderRegistry;

        // Resolved once in Start(): the assets/shaders/ directory for glslc -I.
        std::string m_ShaderIncludeDir;

        // Compilation generation counter. Incremented by watcher thread on each
        // successful compilation. PollAndReload compares against m_ProcessedGeneration
        // to detect pending work. Using a counter instead of a boolean flag prevents
        // the lost-update race where PollAndReload's store(false) could overwrite
        // a concurrent watcher store(true).
        std::atomic<uint32_t> m_CompileGeneration{0};
        uint32_t m_ProcessedGeneration{0};
        std::atomic<uint32_t> m_ReloadCount{0};
        // Steady-clock nanosecond timestamp of the last successful compilation.
        // Used for debounce coalescing in PollAndReload().
        std::atomic<int64_t> m_LastChangeTime{0};
        // Steady-clock nanosecond timestamp of the last pipeline rebuild.
        // Used for max rebuild frequency cap.
        int64_t m_LastRebuildTime{0};

        // Include dependency tracking.
        // Maps: include file path → set of (source GLSL path, SPV output path) pairs.
        struct ShaderEntry
        {
            std::string SourcePath;
            std::string SpvPath;
        };
        // Protected by m_DepsMutex (written once in Start(), read from watcher thread).
        std::mutex m_DepsMutex;
        std::unordered_map<std::string, std::vector<ShaderEntry>> m_IncludeDeps;
    };
}
