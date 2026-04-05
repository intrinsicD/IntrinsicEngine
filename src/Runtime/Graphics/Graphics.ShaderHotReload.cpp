module;

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "RHI.Vulkan.hpp"

module Graphics.ShaderHotReload;

import Core.Filesystem;
import Core.Logging;
import Core.Process;
import Graphics.PipelineLibrary;
import Graphics.ShaderRegistry;
import RHI.Device;
import RHI.Image;

namespace Graphics
{
    namespace
    {
        // Compile timeout: 30 seconds should be more than enough for any single shader.
        constexpr auto kCompileTimeout = std::chrono::milliseconds(30'000);

        // Compile a single GLSL source file to SPIR-V using structured process spawn.
        // Returns true on success. Logs compiler output on failure.
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

            Core::Process::ProcessConfig config;
            config.Executable = "glslc";
            config.Args = {glslPath, "-I", includeDir, "-o", spvPath, "--target-env=vulkan1.3"};
            config.Timeout = kCompileTimeout;
            config.CaptureOutput = true;

            const auto result = Core::Process::Run(config);

            if (result.SpawnFailed)
            {
                Core::Log::Error("[ShaderHotReload] Failed to spawn glslc: {}", result.StdErr);
                return false;
            }

            if (result.TimedOut)
            {
                Core::Log::Error("[ShaderHotReload] glslc timed out after {}s compiling: {}",
                                 kCompileTimeout.count() / 1000, glslPath);
                return false;
            }

            if (result.ExitCode != 0)
            {
                // Surface compiler diagnostics.
                if (!result.StdErr.empty())
                    Core::Log::Error("[ShaderHotReload] glslc stderr:\n{}", result.StdErr);
                if (!result.StdOut.empty())
                    Core::Log::Warn("[ShaderHotReload] glslc stdout:\n{}", result.StdOut);
                return false;
            }

            return true;
        }

        // Check whether glslc is available on this system.
        [[nodiscard]] bool IsGlslcAvailable()
        {
            return Core::Process::IsExecutableAvailable("glslc");
        }

        // Scan a GLSL source file for #include directives.
        // Returns the list of included filenames (e.g. "surface_color_resolve.glsl").
        // Uses manual parsing (no std::regex) because the project builds with -fno-exceptions.
        [[nodiscard]] std::vector<std::string> ScanIncludes(const std::string& sourcePath)
        {
            std::vector<std::string> includes;
            std::ifstream file(sourcePath);
            if (!file.is_open())
                return includes;

            std::string line;
            while (std::getline(file, line))
            {
                // Skip leading whitespace.
                size_t pos = 0;
                while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                    ++pos;

                // Check for '#'.
                if (pos >= line.size() || line[pos] != '#')
                    continue;
                ++pos;

                // Skip whitespace between '#' and 'include'.
                while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                    ++pos;

                // Check for 'include'.
                constexpr std::string_view kInclude = "include";
                if (pos + kInclude.size() > line.size())
                    continue;
                if (line.compare(pos, kInclude.size(), kInclude) != 0)
                    continue;
                pos += kInclude.size();

                // Skip whitespace before the quoted filename.
                while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                    ++pos;

                // Expect opening quote.
                if (pos >= line.size() || line[pos] != '"')
                    continue;
                ++pos;

                // Find closing quote.
                const size_t closeQuote = line.find('"', pos);
                if (closeQuote == std::string::npos || closeQuote == pos)
                    continue;

                includes.push_back(line.substr(pos, closeQuote - pos));
            }

            return includes;
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

        // Phase 1: Register watchers for direct shader source files and build include dependency map.
        m_ShaderRegistry.ForEachWithSource(
            [this, &watchCount](auto /*id*/, const std::string& spvRelPath, const std::string& sourcePath)
            {
                const std::string resolvedSpvPath = Core::Filesystem::GetShaderPath(spvRelPath);

                // Scan for include dependencies.
                auto includes = ScanIncludes(sourcePath);
                if (!includes.empty())
                {
                    std::lock_guard lock(m_DepsMutex);
                    for (const auto& inc : includes)
                    {
                        // Resolve include path relative to the shader include directory.
                        const std::string incPath = (std::filesystem::path(m_ShaderIncludeDir) / inc).string();
                        m_IncludeDeps[incPath].push_back(ShaderEntry{sourcePath, resolvedSpvPath});
                    }
                }

                // Capture pointers to atomics for the watcher callback.
                // Safety: FileWatcher::Shutdown() joins the watcher thread before
                // Engine destroys this service (see Engine::~Engine shutdown order).
                auto* compileGen = &m_CompileGeneration;
                std::atomic<uint32_t>* reloadCounter = &m_ReloadCount;
                auto* lastChangeTime = &m_LastChangeTime;

                Core::Filesystem::FileWatcher::Watch(
                    sourcePath,
                    [sourcePath, spvPath = resolvedSpvPath,
                     includeDir = m_ShaderIncludeDir,
                     compileGen, reloadCounter, lastChangeTime](const std::string& /*changedPath*/)
                    {
                        Core::Log::Info("[ShaderHotReload] Recompiling: {}", sourcePath);

                        if (CompileGlslToSpv(sourcePath, spvPath, includeDir))
                        {
                            Core::Log::Info("[ShaderHotReload] Compilation succeeded: {}", sourcePath);
                            lastChangeTime->store(
                                std::chrono::steady_clock::now().time_since_epoch().count(),
                                std::memory_order_release);
                            compileGen->fetch_add(1, std::memory_order_release);
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

        // Phase 2: Register watchers for include files so changes cascade to all dependents.
        {
            std::lock_guard lock(m_DepsMutex);
            for (const auto& [includePath, dependents] : m_IncludeDeps)
            {
                if (!std::filesystem::exists(includePath))
                {
                    Core::Log::Warn("[ShaderHotReload] Include file not found: {}", includePath);
                    continue;
                }

                // Capture what we need for the watcher callback.
                auto* compileGen = &m_CompileGeneration;
                std::atomic<uint32_t>* reloadCounter = &m_ReloadCount;
                auto* lastChangeTime = &m_LastChangeTime;
                const std::string includeDir = m_ShaderIncludeDir;

                // Copy the dependent entries for the lambda (small, bounded list).
                std::vector<ShaderEntry> deps = dependents;

                Core::Filesystem::FileWatcher::Watch(
                    includePath,
                    [deps = std::move(deps), includeDir, includePath,
                     compileGen, reloadCounter, lastChangeTime](const std::string& /*changedPath*/)
                    {
                        Core::Log::Info("[ShaderHotReload] Include file changed: {} — recompiling {} dependent shader(s).",
                                        includePath, deps.size());

                        bool anySuccess = false;
                        for (const auto& entry : deps)
                        {
                            Core::Log::Info("[ShaderHotReload] Recompiling dependent: {}", entry.SourcePath);
                            if (CompileGlslToSpv(entry.SourcePath, entry.SpvPath, includeDir))
                            {
                                Core::Log::Info("[ShaderHotReload] Compilation succeeded: {}", entry.SourcePath);
                                anySuccess = true;
                                reloadCounter->fetch_add(1, std::memory_order_relaxed);
                            }
                            else
                            {
                                Core::Log::Warn("[ShaderHotReload] Compilation FAILED: {} — keeping previous pipeline.",
                                                entry.SourcePath);
                            }
                        }

                        if (anySuccess)
                        {
                            lastChangeTime->store(
                                std::chrono::steady_clock::now().time_since_epoch().count(),
                                std::memory_order_release);
                            compileGen->fetch_add(1, std::memory_order_release);
                        }
                    });

                ++watchCount;
                Core::Log::Info("[ShaderHotReload] Watching include file: {} ({} dependents)",
                                includePath, dependents.size());
            }
        }

        Core::Log::Info("[ShaderHotReload] Watching {} shader source and include files.", watchCount);
    }

    void ShaderHotReloadService::PollAndReload(VkFormat swapchainFormat, VkFormat depthFormat)
    {
        const uint32_t currentGen = m_CompileGeneration.load(std::memory_order_acquire);
        if (currentGen == m_ProcessedGeneration)
            return; // No new compilations since last rebuild.

        // Debounce: wait until no changes have occurred for kDebounceNs.
        // This coalesces rapid sequential changes (e.g., saving multiple
        // shaders or a shared include triggering many recompilations).
        constexpr int64_t kDebounceNs = 200'000'000; // 200ms
        const auto nowNs = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto lastNs = m_LastChangeTime.load(std::memory_order_acquire);
        if ((nowNs - lastNs) < kDebounceNs)
            return; // Not enough time since last compilation — wait for next frame.

        // Max rebuild frequency cap: prevent excessive GPU drains under rapid save bursts.
        constexpr int64_t kMinRebuildIntervalNs = 500'000'000; // 500ms
        if (m_LastRebuildTime > 0 && (nowNs - m_LastRebuildTime) < kMinRebuildIntervalNs)
            return;

        // Record that we've processed up to this generation. This avoids the
        // lost-update race: if a watcher thread increments m_CompileGeneration
        // between our check and this line, we'll catch it on the next poll.
        m_ProcessedGeneration = currentGen;
        m_LastRebuildTime = nowNs;

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
