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
#include <expected>

export module Core:Assets;

import :Error;
import :Logging;
import :Filesystem;
import :Tasks; // Using your Async Task Graph
import :Hash;

using namespace Core::Hash;

export namespace Core::Assets
{
    // --- Strong Handle Wrapper ---
    struct AssetHandle
    {
        entt::entity ID = entt::null;

        [[nodiscard]] bool IsValid() const { return ID != entt::null; }
        bool operator==(const AssetHandle& other) const { return ID == other.ID; }

        // Allow hashing for maps
        struct Hash
        {
            size_t operator()(const AssetHandle& h) const { return (size_t)h.ID; }
        };
    };

    // --- Components ---

    enum class LoadState
    {
        Unloaded,
        Loading,
        Processing,
        Ready,
        Failed
    };

    struct AssetInfo
    {
        std::string Name;
        std::string Type;
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

    struct AssetReloader
    {
        std::function<void()> ReloadAction;
    };

    // --- Asset Manager ---

    class AssetManager
    {
    public:
        using AssetCallback = std::function<void(AssetHandle)>;

        AssetManager() = default;

        // Call this once per frame on the Main Thread
        void Update();

        // 1. Load: Returns handle immediately. Starts async task.
        template <typename T, typename LoaderFunc>
        AssetHandle Load(const std::filesystem::path& path, LoaderFunc&& loader);

        // 2. Persistent Listener (Updates every reload)
        void Listen(AssetHandle handle, AssetCallback callback);

        // 3. Request Notify: Register a callback for when the asset is Ready.
        // If already ready, callback fires immediately (synchronously).
        void RequestNotify(AssetHandle handle, AssetCallback callback);

        void FinalizeLoad(AssetHandle handle);

        void MoveToProcessing(AssetHandle handle);

        // 4. Get Resource - Returns Expected with proper error codes
        // Use Get() for shared ownership, GetRaw() for temporary access
        template <typename T>
        [[nodiscard]] Expected<std::shared_ptr<T>> Get(AssetHandle handle);

        template <typename T>
        [[nodiscard]] Expected<T*> GetRaw(AssetHandle handle);

        // OPTIMIZATION: Lightweight accessor for hot loops.
        // Returns nullptr if not loaded/ready, invalid handle, or wrong type.
        // Intentionally does not return error codes to minimize overhead.
        template <typename T>
        [[nodiscard]] T* TryGet(AssetHandle handle);

        [[nodiscard]] LoadState GetState(AssetHandle handle);

        void Clear();
        void AssetsUiPanel();

        template <typename T>
        AssetHandle Create(const std::string& name, std::shared_ptr<T> resource);

    private:
        entt::registry m_Registry;
        std::unordered_map<StringID, AssetHandle> m_Lookup;

        // Separate map for Persistent Listeners
        std::unordered_map<AssetHandle, std::vector<AssetCallback>, AssetHandle::Hash> m_PersistentListeners;
        // Map for One-Shot Listeners
        std::unordered_map<AssetHandle, std::vector<AssetCallback>, AssetHandle::Hash> m_OneShotListeners;

        std::vector<AssetHandle> m_ReadyQueue; // Queue of assets loaded since last frame
        std::shared_mutex m_Mutex;
        std::mutex m_EventQueueMutex;

        void EnqueueReadyEvent(AssetHandle handle);

        // Internal reload helper
        template <typename T, typename LoaderFunc>
        void Reload(AssetHandle handle, LoaderFunc&& loader);
    };

    template <typename T, typename LoaderFunc>
    AssetHandle AssetManager::Load(const std::filesystem::path& path, LoaderFunc&& loader)
    {
        std::string key = std::filesystem::absolute(path).string();

        StringID id(key); // Hashes here
        std::unique_lock lock(m_Mutex);

        if (m_Lookup.contains(id)) return m_Lookup[id];

        auto entity = m_Registry.create();
        AssetHandle handle{entity};

        m_Registry.emplace<AssetInfo>(entity, key, "Unknown", LoadState::Loading);
        m_Registry.emplace<AssetSource>(entity, key);
        m_Lookup[id] = handle;

        m_Registry.emplace<AssetReloader>(entity, [this, handle, loader]() mutable
        {
            this->Reload<T>(handle, loader);
        });

        // WATCHER REGISTRATION
        // We capture 'loader' by value (copy) for the lambda.
        // NOTE: 'loader' usually captures pointers/refs, ensure they are safe or copyable!
        Filesystem::FileWatcher::Watch(key, [this, handle, loader](const std::string&)
        {
            this->Reload<T>(handle, loader);
        });

        // Trigger initial load via Reload logic to avoid duplication
        lock.unlock(); // Reload locks internally
        Reload<T>(handle, std::forward<LoaderFunc>(loader));

        return handle;
    }

    template <typename T, typename LoaderFunc>
    void AssetManager::Reload(AssetHandle handle, LoaderFunc&& loader)
    {
        // 1. Mark Loading (Thread Safe)
        {
            std::unique_lock lock(m_Mutex);
            if (m_Registry.valid(handle.ID))
            {
                m_Registry.get<AssetInfo>(handle.ID).State = LoadState::Loading;
            }
        }

        // 2. Get Path
        std::string path;
        {
            std::shared_lock lock(m_Mutex);
            if (!m_Registry.valid(handle.ID)) return;
            path = m_Registry.get<AssetSource>(handle.ID).FilePath.string();
        }

        // 3. Async Task
        Tasks::Scheduler::Dispatch([this, handle, path, loader]()
        {
            auto result = loader(path, handle);

            std::unique_lock lock(m_Mutex);
            if (m_Registry.valid(handle.ID))
            {
                if (result)
                {
                    // Replace Payload
                    m_Registry.emplace_or_replace<AssetPayload<T>>(handle.ID, result);
                    auto& info = m_Registry.get<AssetInfo>(handle.ID);
                    if (info.State != LoadState::Processing)
                    {
                        info.State = LoadState::Ready;
                        EnqueueReadyEvent(handle);
                        Log::Info("Asset Loaded: {}", path);
                    }
                    else
                    {
                        Log::Info("Asset Loaded (CPU) -> Waiting for Processing: {}", path);
                    }
                }
                else
                {
                    m_Registry.get<AssetInfo>(handle.ID).State = LoadState::Failed;
                    Log::Error("Asset Load Failed: {}", path);
                }
            }
        });
    }

    // 2. Data Access - Returns Expected with proper error information
    template <typename T>
    Expected<std::shared_ptr<T>> AssetManager::Get(AssetHandle handle)
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle.ID))
            return std::unexpected(ErrorCode::ResourceNotFound);

        // Check if loaded
        const auto& info = m_Registry.get<AssetInfo>(handle.ID);
        if (info.State != LoadState::Ready)
        {
            if (info.State == LoadState::Failed)
                return std::unexpected(ErrorCode::AssetLoadFailed);
            return std::unexpected(ErrorCode::AssetNotLoaded);
        }

        // Check if it has the specific payload
        if (auto* payload = m_Registry.try_get<AssetPayload<T>>(handle.ID))
        {
            return payload->Resource;
        }
        return std::unexpected(ErrorCode::AssetTypeMismatch);
    }

    template <typename T>
    Expected<T*> AssetManager::GetRaw(AssetHandle handle)
    {
        // Use shared_lock for reader concurrency
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle.ID))
            return std::unexpected(ErrorCode::ResourceNotFound);

        const auto& info = m_Registry.get<AssetInfo>(handle.ID);
        if (info.State != LoadState::Ready)
        {
            if (info.State == LoadState::Failed)
                return std::unexpected(ErrorCode::AssetLoadFailed);
            return std::unexpected(ErrorCode::AssetNotLoaded);
        }

        if (auto* payload = m_Registry.try_get<AssetPayload<T>>(handle.ID))
        {
            return payload->Resource.get();
        }
        return std::unexpected(ErrorCode::AssetTypeMismatch);
    }

    template <typename T>
    T* AssetManager::TryGet(AssetHandle handle)
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle.ID)) return nullptr;

        const auto& info = m_Registry.get<AssetInfo>(handle.ID);
        if (info.State != LoadState::Ready) return nullptr;

        if (auto* payload = m_Registry.try_get<AssetPayload<T>>(handle.ID))
        {
            return payload->Resource.get();
        }
        return nullptr;
    }

    template <typename T>
    AssetHandle AssetManager::Create(const std::string& name, std::shared_ptr<T> resource)
    {
        std::unique_lock lock(m_Mutex);

        auto entity = m_Registry.create();
        AssetHandle handle{entity};

        m_Registry.emplace<AssetInfo>(entity, name, "Runtime", LoadState::Ready);
        m_Registry.emplace<AssetPayload<T>>(entity, resource);

        // Allow lookup by name
        m_Lookup[StringID(name)] = handle;

        return handle;
    }
}
