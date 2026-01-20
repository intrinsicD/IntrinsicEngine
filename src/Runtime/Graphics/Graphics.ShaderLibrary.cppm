// Graphics.ShaderLibrary.cppm
module;
#include <string>
#include <unordered_map>
#include <functional>
#include <filesystem>
#include <mutex>
#include <vector>

export module Graphics:ShaderLibrary;

import RHI;
import Core;

export namespace Graphics
{
    class ShaderLibrary
    {
    public:
        // Callback signature: (ShaderName)
        using OnReloadCallback = std::function<void(const std::string&)>;

        ShaderLibrary(RHI::VulkanDevice& device);

        // Registers a shader file to be watched.
        // name: "TriangleVert"
        // srcPath: "assets/shaders/triangle.vert"
        // stage: Vertex/Fragment
        void Register(const std::string& name, const std::string& srcPath, RHI::ShaderStage stage);

        // Returns the current valid shader module.
        // Returns nullptr if not loaded.
        [[nodiscard]] RHI::ShaderModule* GetModule(const std::string& name);

        // Register a callback to be fired when a specific shader is reloaded.
        // Use this in RenderSystem to trigger Pipeline Rebuilds.
        void Listen(const std::string& name, OnReloadCallback callback);

        // Call this at the start of the frame (Main Thread) to process background compilations.
        void Update();

    private:
        struct ShaderEntry
        {
            std::string SourcePath;
            std::string SpvPath;
            RHI::ShaderStage Stage;
            std::unique_ptr<RHI::ShaderModule> CurrentModule;
            std::vector<OnReloadCallback> Callbacks;
        };

        void OnFileChanged(const std::string& path);

        RHI::VulkanDevice& m_Device;
        std::unordered_map<std::string, ShaderEntry> m_Shaders; // Key = Name
        std::unordered_map<std::string, std::string> m_PathToName; // Key = FilePath

        // Thread safety for the FileWatcher callback
        std::mutex m_QueueMutex;
        std::vector<std::string> m_DirtyShaders; // List of names needing main-thread update
    };
}