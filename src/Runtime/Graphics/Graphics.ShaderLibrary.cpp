module;
#include <filesystem>
#include <vector>
#include <format>
#include <mutex>
#include <algorithm>
#include "RHI.Vulkan.hpp"

module Graphics:ShaderLibrary.Impl;
import :ShaderLibrary;
import Core;

// Include the compile helper from step 1 or implement here
namespace Graphics::Internal {
    bool CompileShader(const std::filesystem::path& src, const std::filesystem::path& dst) {
        std::string cmd = std::format("glslc \"{}\" -o \"{}\"", src.string(), dst.string());
        return std::system(cmd.c_str()) == 0;
    }
}

namespace Graphics
{
    ShaderLibrary::ShaderLibrary(RHI::VulkanDevice& device)
        : m_Device(device)
    {
    }

    void ShaderLibrary::Register(const std::string& name, const std::string& srcPath, RHI::ShaderStage stage)
    {
        std::filesystem::path fsSrc = Core::Filesystem::GetAssetPath(srcPath);
        std::filesystem::path fsSpv = Core::Filesystem::GetShaderPath(srcPath + ".spv"); // Convention: .vert -> .vert.spv

        // Initial Load
        auto module = std::make_unique<RHI::ShaderModule>(m_Device, fsSpv.string(), stage);
        
        ShaderEntry entry;
        entry.SourcePath = fsSrc.string();
        entry.SpvPath = fsSpv.string();
        entry.Stage = stage;
        entry.CurrentModule = std::move(module);

        m_Shaders[name] = std::move(entry);
        m_PathToName[fsSrc.string()] = name;

        // Register with existing global FileWatcher
        Core::Filesystem::FileWatcher::Watch(fsSrc.string(), [this](const std::string& path) {
            this->OnFileChanged(path);
        });
    }

    RHI::ShaderModule* ShaderLibrary::GetModule(const std::string& name)
    {
        if(m_Shaders.contains(name)) return m_Shaders[name].CurrentModule.get();
        return nullptr;
    }

    void ShaderLibrary::Listen(const std::string& name, OnReloadCallback callback)
    {
        if(m_Shaders.contains(name)) m_Shaders[name].Callbacks.push_back(std::move(callback));
    }

    void ShaderLibrary::OnFileChanged(const std::string& path)
    {
        // This runs on the FileWatcher thread!
        
        // 1. Identify Shader
        std::string name;
        {
            // Simple lookup, no mutex needed if m_PathToName is stable after init
            // (Assuming Register() happens before loop). 
            // If Register() happens dynamically, lock m_Shaders access.
            if (!m_PathToName.contains(path)) return;
            name = m_PathToName[path];
        }

        const auto& entry = m_Shaders[name];

        // 2. Compile (Blocking operation on worker thread - this is good)
        Core::Log::Info("[HotReload] Compiling {}...", name);
        bool success = Internal::CompileShader(entry.SourcePath, entry.SpvPath);

        if (success)
        {
            // 3. Queue Main Thread Update
            std::lock_guard lock(m_QueueMutex);
            m_DirtyShaders.push_back(name);
            Core::Log::Info("[HotReload] Compiled {}. Queued for update.", name);
        }
        else
        {
            Core::Log::Error("[HotReload] Compilation failed for {}. Keeping old shader.", name);
        }
    }

    void ShaderLibrary::Update()
    {
        // Run on Main Thread (e.g. Engine::OnUpdate or RenderSystem::OnUpdate)
        
        std::vector<std::string> dirty;
        {
            std::lock_guard lock(m_QueueMutex);
            if (m_DirtyShaders.empty()) return;
            dirty = std::move(m_DirtyShaders);
        }

        // Deduplicate
        std::sort(dirty.begin(), dirty.end());
        dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());

        for (const auto& name : dirty)
        {
            auto& entry = m_Shaders[name];

            // 1. Reload from disk (SPV)
            auto newModule = std::make_unique<RHI::ShaderModule>(m_Device, entry.SpvPath, entry.Stage);
            
            // Check if valid (file I/O might still fail)
            if (newModule->GetHandle() != VK_NULL_HANDLE)
            {
                // 2. Swap (Old one dies here, triggers SafeDestroy in RHI::ShaderModule destructor)
                entry.CurrentModule = std::move(newModule);

                // 3. Notify Listeners (The RenderSystem)
                for (const auto& cb : entry.Callbacks) cb(name);
                
                Core::Log::Info("[HotReload] Hot-swapped shader: {}", name);
            }
        }
    }
}