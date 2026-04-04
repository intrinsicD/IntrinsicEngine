module;

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>

#include "RHI.Vulkan.hpp"

module Graphics.ShaderHotReload;

import Core.Filesystem;
import Core.Logging;
import Graphics.PipelineLibrary;
import Graphics.ShaderRegistry;
import RHI.Device;
import RHI.Image;

namespace Graphics
{
    namespace
    {
        // Compile a single GLSL source file to SPIR-V.
        // Returns true on success.
        [[nodiscard]] bool CompileGlslToSpv(const std::string& glslPath,
                                            const std::string& spvPath,
                                            const std::string& includeDir)
        {
            // Ensure the output directory exists.
            {
                std::error_code ec;
                const auto outDir = std::filesystem::path(spvPath).parent_path();
                if (!outDir.empty())
                    std::filesystem::create_directories(outDir, ec);
            }

            std::string cmd = std::format(
                "glslc \"{}\" -I \"{}\" -o \"{}\" --target-env=vulkan1.3 2>&1",
                glslPath, includeDir, spvPath);

            int result = std::system(cmd.c_str());
            return result == 0;
        }

        // Check whether glslc is available on this system.
        [[nodiscard]] bool IsGlslcAvailable()
        {
            int result = std::system("glslc --version > /dev/null 2>&1");
            return result == 0;
        }
    }

    ShaderHotReloadService::ShaderHotReloadService(
        std::shared_ptr<RHI::VulkanDevice> device,
        PipelineLibrary& pipelineLibrary,
        const ShaderRegistry& shaderRegistry)
        : m_Device(std::move(device))
        , m_PipelineLibrary(pipelineLibrary)
        , m_ShaderRegistry(shaderRegistry)
    {
    }

    ShaderHotReloadService::~ShaderHotReloadService() = default;

    void ShaderHotReloadService::Start()
    {
        // Validate that glslc is available before registering watchers.
        if (!IsGlslcAvailable())
        {
            Core::Log::Warn("[ShaderHotReload] glslc not found on PATH. "
                            "Shader hot-reload is enabled but recompilation will not work. "
                            "Install the Vulkan SDK or ensure glslc is in your PATH.");
            return;
        }

        m_ShaderIncludeDir = Core::Filesystem::GetAssetPath("shaders");

        uint32_t watchCount = 0;

        m_ShaderRegistry.ForEachWithSource(
            [this, &watchCount](auto /*id*/, const std::string& spvRelPath, const std::string& sourcePath)
            {
                // Resolve the full SPV output path (same resolution the runtime uses).
                const std::string resolvedSpvPath = Core::Filesystem::GetShaderPath(spvRelPath);

                // Capture pointers to atomics for the watcher callback.
                // Safety: FileWatcher::Shutdown() joins the watcher thread before
                // the engine destroys this service (shutdown order in Engine::~Engine:
                // FileWatcher::Shutdown at line ~401, then m_RenderOrchestrator.reset()
                // at line ~423). So the atomics remain valid for the watcher's lifetime.
                std::atomic<bool>* pendingFlag = &m_ReloadPending;
                std::atomic<uint32_t>* reloadCounter = &m_ReloadCount;
                auto* lastChangeTime = &m_LastChangeTime;

                Core::Filesystem::FileWatcher::Watch(
                    sourcePath,
                    [sourcePath, spvPath = resolvedSpvPath,
                     includeDir = m_ShaderIncludeDir,
                     pendingFlag, reloadCounter, lastChangeTime](const std::string& /*changedPath*/)
                    {
                        Core::Log::Info("[ShaderHotReload] Recompiling: {}", sourcePath);

                        if (CompileGlslToSpv(sourcePath, spvPath, includeDir))
                        {
                            Core::Log::Info("[ShaderHotReload] Compilation succeeded: {}", sourcePath);
                            // Record the time of this change for debounce coalescing.
                            lastChangeTime->store(
                                std::chrono::steady_clock::now().time_since_epoch().count(),
                                std::memory_order_release);
                            pendingFlag->store(true, std::memory_order_release);
                            reloadCounter->fetch_add(1, std::memory_order_relaxed);
                        }
                        else
                        {
                            Core::Log::Warn("[ShaderHotReload] Compilation FAILED: {} — keeping previous pipeline.",
                                            sourcePath);
                        }
                    });

                ++watchCount;
            });

        Core::Log::Info("[ShaderHotReload] Watching {} shader source files.", watchCount);
    }

    void ShaderHotReloadService::PollAndReload(VkFormat swapchainFormat, VkFormat depthFormat)
    {
        if (!m_ReloadPending.load(std::memory_order_acquire))
            return;

        // Debounce: wait until no changes have occurred for kDebounceMs.
        // This coalesces rapid sequential changes (e.g., saving multiple
        // shaders or a shared include triggering many recompilations).
        constexpr int64_t kDebounceNs = 200'000'000; // 200ms
        const auto nowNs = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto lastNs = m_LastChangeTime.load(std::memory_order_acquire);
        if ((nowNs - lastNs) < kDebounceNs)
            return; // Not enough time since last compilation — wait for next frame.

        m_ReloadPending.store(false, std::memory_order_release);

        Core::Log::Info("[ShaderHotReload] Draining GPU and rebuilding pipelines...");

        // Wait for all GPU work to finish before destroying old pipelines.
        if (m_Device)
        {
            vkDeviceWaitIdle(m_Device->GetLogicalDevice());
        }

        m_PipelineLibrary.RebuildGraphicsPipelines(
            m_ShaderRegistry,
            swapchainFormat,
            depthFormat);

        Core::Log::Info("[ShaderHotReload] Pipeline rebuild complete (total reloads: {}).",
                        m_ReloadCount.load(std::memory_order_relaxed));
    }
}
