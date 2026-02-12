module;

#include <string>
#include <unordered_map>
#include <optional>

export module Graphics:ShaderRegistry;

import Core.Hash;

export namespace Graphics
{
    // Lightweight, data-driven shader path registry.
    // Contract: populated during engine init (single-threaded), read-only afterwards.
    class ShaderRegistry
    {
    public:
        void Register(Core::Hash::StringID name, const std::string& path)
        {
            m_Paths[name] = path;
        }

        [[nodiscard]] std::optional<std::string> Get(Core::Hash::StringID name) const
        {
            auto it = m_Paths.find(name);
            if (it != m_Paths.end())
                return it->second;
            return std::nullopt;
        }

        [[nodiscard]] bool Contains(Core::Hash::StringID name) const
        {
            return m_Paths.contains(name);
        }

    private:
        std::unordered_map<Core::Hash::StringID, std::string> m_Paths;
    };
}
