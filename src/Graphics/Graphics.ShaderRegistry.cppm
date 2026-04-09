module;

#include <string>
#include <unordered_map>
#include <optional>
#include <vector>
#include <functional>

export module Graphics.ShaderRegistry;

import Core.Hash;

export namespace Graphics
{
    // Lightweight, data-driven shader path registry.
    // Contract: populated during engine init (single-threaded), read-only afterwards.
    //
    // Tracks both the resolved SPV path and the optional GLSL source path
    // for each shader.  The source path is used by shader hot-reload to
    // watch for edits and re-invoke the compiler.
    class ShaderRegistry
    {
    public:
        void Register(Core::Hash::StringID name, const std::string& spvRelativePath)
        {
            m_Paths[name] = spvRelativePath;
        }

        // Register a shader with both its SPV relative path and GLSL source path.
        void RegisterWithSource(Core::Hash::StringID name,
                                const std::string& spvRelativePath,
                                const std::string& glslSourcePath)
        {
            m_Paths[name] = spvRelativePath;
            m_SourcePaths[name] = glslSourcePath;
        }

        [[nodiscard]] std::optional<std::string> Get(Core::Hash::StringID name) const
        {
            auto it = m_Paths.find(name);
            if (it != m_Paths.end())
                return it->second;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> GetSourcePath(Core::Hash::StringID name) const
        {
            auto it = m_SourcePaths.find(name);
            if (it != m_SourcePaths.end())
                return it->second;
            return std::nullopt;
        }

        [[nodiscard]] bool Contains(Core::Hash::StringID name) const
        {
            return m_Paths.contains(name);
        }

        // Iterate all registered shader entries.
        // fn(StringID name, const std::string& spvPath)
        template<typename Fn>
        void ForEach(Fn&& fn) const
        {
            for (const auto& [id, path] : m_Paths)
                fn(id, path);
        }

        // Iterate entries that have source paths registered.
        // fn(StringID name, const std::string& spvPath, const std::string& sourcePath)
        template<typename Fn>
        void ForEachWithSource(Fn&& fn) const
        {
            for (const auto& [id, sourcePath] : m_SourcePaths)
            {
                auto it = m_Paths.find(id);
                if (it != m_Paths.end())
                    fn(id, it->second, sourcePath);
            }
        }

    private:
        std::unordered_map<Core::Hash::StringID, std::string> m_Paths;
        std::unordered_map<Core::Hash::StringID, std::string> m_SourcePaths;
    };
}
