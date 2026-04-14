module;

#include <memory>
#include <cassert>
#include <mutex>
#include <filesystem>
#include <expected>
#include <entt/entity/registry.hpp>

module Extrinsic.Asset.Manager;

import Extrinsic.Core.Error;

namespace Extrinsic::Assets
{
    void AssetManager::BeginReadPhase() const
    {
#ifndef NDEBUG
        m_DebugReadPhase.fetch_add(1, std::memory_order_relaxed);
#endif
    }

    void AssetManager::EndReadPhase() const
    {
#ifndef NDEBUG
        const int prev = m_DebugReadPhase.fetch_sub(1, std::memory_order_relaxed);
        assert(prev > 0 && "AssetManager::EndReadPhase without matching BeginReadPhase.");
#endif
    }

    void AssetManager::Update()
    {
        std::vector<AssetHandle> events;
        {
            std::lock_guard lock(m_EventQueueMutex);
            if (m_ReadyQueue.empty()) return;
            events.swap(m_ReadyQueue);
        }

        for (const auto& handle : events)
        {
            // 1. Process One-Shot Listeners
            std::vector<AssetCallback> runOneShots;
            {
                std::unique_lock lock(m_Mutex);
                auto it = m_OneShotListeners.find(handle);
                if (it != m_OneShotListeners.end())
                {
                    runOneShots = std::move(it->second);
                    m_OneShotListeners.erase(it);
                }
            }
            // Execute OUTSIDE lock
            for (const auto& cb : runOneShots) cb(handle);

            // 2. Process Persistent Listeners
            std::vector<AssetCallback> runPersistent;
            {
                std::shared_lock lock(m_Mutex);

                // Check if any listeners exist for this asset
                if (m_PersistentListeners.contains(handle))
                {
                    const auto& listeners = m_PersistentListeners.at(handle);
                    runPersistent.reserve(listeners.size());

                    // Copy valid callbacks only
                    for(const auto& [id, cb] : listeners) {
                        if(cb) runPersistent.push_back(cb);
                    }
                }
            }
            // Execute OUTSIDE lock (Safe to call Load() recursively now)
            for (const auto& cb : runPersistent) cb(handle);
        }
    }

    Core::Expected<AssetHandle> AssetManager::CreateAsset(AssetMetaData metaData, std::function<void(std::string_view)> &&loader)
    {

        using LoaderFunc = std::function<AssetHandle(std::string_view)>;
        if (!metaData.ReloadAction)
        {
            return Core::Err<AssetHandle>(Core::ErrorCode::AssetLoaderMissing);
        }

        if (metaData.FilePath.empty())
        {
            return Core::Err<AssetHandle>(Core::ErrorCode::InvalidPath);
        }

        std::string key = std::filesystem::absolute(metaData.FilePath).string();

        Core::Hash::StringID id(key); // Hashes here
        std::unique_lock lock(m_Mutex);

        if (m_Lookup.contains(id)) return m_Lookup[id];

        AssetHandle handle = m_Registry.create();

        m_Registry.emplace<AssetMetaData>(handle, std::move(metaData));
        m_Lookup[id] = handle;

        auto sharedLoader = std::make_shared<std::decay_t<LoaderFunc>>(std::forward<LoaderFunc>(loader));

        auto reloadAction = std::make_shared<std::function<void()>>(
            [this, handle, sharedLoader]() mutable
            {
                this->Reload(handle, *sharedLoader);
            });

        lock.unlock(); // Reload locks internally
        Reload(handle, *sharedLoader);
        return Core::Ok(handle);
    }

    void AssetManager::Reload(AssetHandle handle, std::function<void(std::string_view)> &&loader)
    {
        std::string path;
        {
            std::unique_lock lock(m_Mutex);
            if (m_Registry.valid(handle))
            {
                auto &metaData = m_Registry.get<AssetMetaData>(handle);
                metaData.State = LoadState::Loading;
                path = metaData.FilePath;
            }
        }

        if (!path.empty())
        {
            auto result = loader(path);
            //TODO... figure out how to reload properly
        }
    }

    bool AssetManager::DestroyAsset(AssetHandle handle)
    {
        std::unique_lock lock(m_Mutex);
        if (!m_Registry.valid(handle))
        {
            return false;
        }

        for (auto it = m_Lookup.begin(); it != m_Lookup.end(); ++it)
        {
            if (it->second == handle)
            {
                m_Lookup.erase(it);
                break;
            }
        }

        m_OneShotListeners.erase(handle);
        m_PersistentListeners.erase(handle);

        for (auto it = m_ReadyQueue.begin(); it != m_ReadyQueue.end();)
        {
            if (*it == handle)
            {
                it = m_ReadyQueue.erase(it);
            }
            else
            {
                ++it;
            }
        }

        m_Registry.destroy(handle);
        return true;
    }

    void AssetManager::FinalizeLoad(AssetHandle handle)
    {
        std::unique_lock lock(m_Mutex);
        if (m_Registry.valid(handle))
        {
            auto& metaData = m_Registry.get<AssetMetaData>(handle);
            if (metaData.State == LoadState::Processing)
            {
                metaData.State = LoadState::Ready;
                EnqueueReady(handle);
            }
        }
    }

    Core::Expected<const AssetMetaData&> AssetManager::GetAssetMetaData(AssetHandle handle) const
    {
        return m_Registry.get<AssetMetaData>(handle);
    }

    [[nodiscard]] LoadState AssetManager::GetState(AssetHandle handle) const
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle)) return LoadState::Unloaded;
        return m_Registry.get<AssetMetaData>(handle).State;
    }

    static std::atomic<uint32_t> s_ListenerIdCounter{1};

    AssetManager::ListenerHandle AssetManager::Listen(AssetHandle handle, AssetCallback callback)
    {
        AssetCallback fireCallback{};
        ListenerHandle out{0};
        {
            std::unique_lock lock(m_Mutex);
            if (!m_Registry.valid(handle))
                return {0};

            const uint32_t id = s_ListenerIdCounter++;
            out = {id};

            // Keep the persistent listener registered, then snapshot for immediate fire if ready.
            m_PersistentListeners[handle][id] = callback;

            if (m_Registry.get<AssetMetaData>(handle).State == LoadState::Ready)
                fireCallback = std::move(callback);
        }

        if (fireCallback)
            fireCallback(handle);

        return out;
    }

    void AssetManager::Unlisten(AssetHandle handle, ListenerHandle listenerID)
    {
        std::unique_lock lock(m_Mutex);
        if (m_PersistentListeners.contains(handle))
        {
            m_PersistentListeners[handle].erase(listenerID);
        }
    }

    void AssetManager::ListenOnce(AssetHandle handle, AssetCallback callback)
    {
        AssetCallback fireCallback{};
        {
            std::unique_lock lock(m_Mutex);
            if (!m_Registry.valid(handle))
                return;

            // If already ready, fire immediately outside the lock.
            if (m_Registry.get<AssetMetaData>(handle).State == LoadState::Ready)
            {
                fireCallback = std::move(callback);
            }
            else
            {
                // Register for future
                m_OneShotListeners[handle].push_back(std::move(callback));
            }
        }

        if (fireCallback)
            fireCallback(handle);
    }

    void AssetManager::Clear()
    {
        // 1. Snapshot entities to destroy and clear auxiliary maps under lock.
        std::vector<entt::entity> toDestroy;
        {
            std::unique_lock lock(m_Mutex);
            m_Lookup.clear();
            m_OneShotListeners.clear();
            m_PersistentListeners.clear();

            auto view = m_Registry.view<AssetMetaData>();

            toDestroy.reserve(view.storage()->size());

            for (auto entity : view)
                toDestroy.push_back(entity);
        }

        // 2. Destroy entities one-by-one WITHOUT holding the main mutex.
        // Destructors (e.g. ~Material -> MaterialRegistry::Destroy -> AssetManager::Unlisten)
        // can safely re-acquire m_Mutex because we released it above.
        for (auto entity : toDestroy)
        {
            if (m_Registry.valid(entity))
                m_Registry.destroy(entity);
        }

        // 3. Final registry cleanup (should be empty; ensures no orphaned entities remain).
        m_Registry.clear();

        // 4. Drain event queue
        {
            std::lock_guard qLock(m_EventQueueMutex);
            m_ReadyQueue.clear();
        }
    }

    void AssetManager::EnqueueReady(AssetHandle handle)
    {
        std::lock_guard lock(m_EventQueueMutex);
        m_ReadyQueue.push_back(handle);
    }
}
