module;
#include <entt/entt.hpp>
#include <string>
#include <filesystem>
#include <memory>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <typeindex>

export module Core.Assets;

import Core.Logging;
import Core.Tasks; // Using your Async Task Graph

export namespace Core::Assets
{
    // --- Strong Handle Wrapper ---
    struct AssetHandle
    {
        entt::entity ID = entt::null;

        bool IsValid() const { return ID != entt::null; }
        bool operator==(const AssetHandle& other) const { return ID == other.ID; }

        // Allow hashing for maps
        struct Hash
        {
            std::size_t operator()(const AssetHandle& h) const { return (size_t)h.ID; }
        };
    };

    // --- Components ---

    enum class LoadState { Unloaded, Loading, Ready, Failed };

    struct AssetInfo
    {
        std::string Name;
        LoadState State = LoadState::Unloaded;
    };

    struct AssetSource
    {
        std::filesystem::path FilePath;
    };

    // The actual data container. 
    // We use shared_ptr so the asset can stay alive if the manager drops it (optional design choice),
    // or simply to handle polymorphic deletion easily.
    template <typename T>
    struct AssetPayload
    {
        std::shared_ptr<T> Resource;
    };

    // --- Asset Manager ---

    class AssetManager
    {
    public:
        AssetManager() = default;
        ~AssetManager() = default;

        // 1. Synchronous/Async Loader
        // Returns a handle immediately. The asset might not be ready yet.
        template <typename T, typename LoaderFunc>
        AssetHandle Load(const std::string& path, LoaderFunc&& loader)
        {
            std::unique_lock lock(m_Mutex);

            // 1. Check Cache
            size_t pathHash = std::hash<std::string>{}(path);
            if (m_Lookup.contains(pathHash))
            {
                return m_Lookup[pathHash];
            }

            // 2. Create Asset Entity
            auto entity = m_Registry.create();
            AssetHandle handle{entity};

            m_Registry.emplace<AssetInfo>(entity, path, LoadState::Loading);
            m_Registry.emplace<AssetSource>(entity, path);

            // Register in lookup
            m_Lookup[pathHash] = handle;

            // 3. Dispatch Async Task
            // We copy the loader and path into the lambda
            Tasks::Scheduler::Dispatch([this, entity, path, loader = std::forward<LoaderFunc>(loader)]()
            {
                // Execute user-provided loader (File IO / Parsing)
                // This runs on a worker thread
                std::shared_ptr<T> result = loader(path);

                // Critical Section: Write back to registry
                // Ideally, we might defer this to main thread to avoid locking registry,
                // but for now, we lock.
                {
                    std::unique_lock lock(m_Mutex);
                    if (m_Registry.valid(entity))
                    {
                        if (result)
                        {
                            m_Registry.emplace<AssetPayload<T>>(entity, result);
                            m_Registry.get<AssetInfo>(entity).State = LoadState::Ready;
                            Log::Info("Asset Loaded: {}", path);
                        }
                        else
                        {
                            m_Registry.get<AssetInfo>(entity).State = LoadState::Failed;
                            Log::Error("Asset Failed: {}", path);
                        }
                    }
                }
            });

            return handle;
        }

        // 2. Data Access
        // Returns nullptr if not ready or wrong type
        template <typename T>
        std::shared_ptr<T> Get(AssetHandle handle)
        {
            std::shared_lock lock(m_Mutex);
            if (!m_Registry.valid(handle.ID)) return nullptr;

            // Check if loaded
            const auto& info = m_Registry.get<AssetInfo>(handle.ID);
            if (info.State != LoadState::Ready) return nullptr;

            // Check if it has the specific payload
            if (auto* payload = m_Registry.try_get<AssetPayload<T>>(handle.ID))
            {
                return payload->Resource;
            }
            return nullptr;
        }

        // 3. Status Check
        LoadState GetState(AssetHandle handle)
        {
            std::shared_lock lock(m_Mutex);
            if (!m_Registry.valid(handle.ID)) return LoadState::Unloaded;
            return m_Registry.get<AssetInfo>(handle.ID).State;
        }

        void Clear()
        {
            std::unique_lock lock(m_Mutex);
            m_Registry.clear(); // Destroys all components -> release shared_ptrs -> free VMA memory
            m_Lookup.clear();
        }

    private:
        entt::registry m_Registry;
        std::unordered_map<size_t, AssetHandle> m_Lookup;

        // R/W Lock for thread safety
        std::shared_mutex m_Mutex;
    };
}
