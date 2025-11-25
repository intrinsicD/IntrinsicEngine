module;
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <algorithm>

module Core.Assets;

namespace Core::Assets
{
    void AssetManager::EnqueueReadyEvent(AssetHandle handle)
    {
        std::lock_guard lock(m_EventQueueMutex);
        m_ReadyQueue.push_back(handle);
    }

    void AssetManager::Update()
    {
        // 1. Swap queue to avoid locking the event queue mutex during processing
        std::vector<AssetHandle> events;
        {
            std::lock_guard lock(m_EventQueueMutex);
            if (m_ReadyQueue.empty()) return;
            events.swap(m_ReadyQueue);
        }

        // 2. Process Events
        for (const auto& handle : events)
        {
            std::vector<AssetCallback> callbacksToRun;

            // Critical Section: Extract listeners for this handle
            {
                std::unique_lock lock(m_Mutex);
                auto it = m_Listeners.find(handle);
                if (it != m_Listeners.end())
                {
                    // Move callbacks out of the map so we can run them without the lock
                    callbacksToRun = std::move(it->second);
                    m_Listeners.erase(it);
                }
            } // <--- Lock is released here

            // 3. Execute Callbacks (Lock Free)
            // Now calls to AssetManager::Get() inside these callbacks will succeed
            for (const auto& callback : callbacksToRun)
            {
                callback(handle);
            }
        }
    }

    void AssetManager::RequestNotify(AssetHandle handle, AssetCallback callback)
    {
        std::unique_lock lock(m_Mutex);

        if (!m_Registry.valid(handle.ID)) return;

        // If already ready, fire immediately
        if (m_Registry.get<AssetInfo>(handle.ID).State == LoadState::Ready)
        {
            lock.unlock(); // Unlock before callback to prevent deadlocks
            callback(handle);
            return;
        }

        // Register for future
        m_Listeners[handle].push_back(callback);
    }

    LoadState AssetManager::GetState(AssetHandle handle)
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle.ID)) return LoadState::Unloaded;
        return m_Registry.get<AssetInfo>(handle.ID).State;
    }

    void AssetManager::Clear()
    {
        std::unique_lock lock(m_Mutex);
        m_Registry.clear();
        m_Lookup.clear();
        m_Listeners.clear();
        {
            std::lock_guard qLock(m_EventQueueMutex);
            m_ReadyQueue.clear();
        }
    }
}