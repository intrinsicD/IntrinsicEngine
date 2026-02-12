module;
#include <mutex>
#include <algorithm>
#include <vector>
#include "RHI.Vulkan.hpp"

module Runtime.AssetPipeline;

import Core;
import RHI;

namespace Runtime
{
    AssetPipeline::AssetPipeline(RHI::TransferManager& transferManager)
        : m_TransferManager(transferManager)
    {
        Core::Log::Info("AssetPipeline: Initialized.");
    }

    AssetPipeline::~AssetPipeline()
    {
        Core::Log::Info("AssetPipeline: Shutdown.");
    }

    void AssetPipeline::RegisterAssetLoad(Core::Assets::AssetHandle handle, RHI::TransferToken token)
    {
        std::lock_guard lock(m_LoadMutex);
        m_PendingLoads.push_back({handle, token, {}});
    }

    void AssetPipeline::ProcessUploads()
    {
        // 1. Cleanup staging memory
        m_TransferManager.GarbageCollect();

        // 2. Check for completions
        std::lock_guard lock(m_LoadMutex);
        if (m_PendingLoads.empty()) return;

        // Use erase-remove idiom to process finished loads
        auto it = std::remove_if(m_PendingLoads.begin(), m_PendingLoads.end(),
                                 [&](PendingLoad& load)
                                 {
                                     if (m_TransferManager.IsCompleted(load.Token))
                                     {
                                         if (load.OnComplete.Valid()) load.OnComplete();

                                         // Signal Core that the "External Processing" is done
                                         m_AssetManager.FinalizeLoad(load.Handle);
                                         return true; // Remove from list
                                     }
                                     return false; // Keep waiting
                                 });

        m_PendingLoads.erase(it, m_PendingLoads.end());
    }

    void AssetPipeline::ProcessMainThreadQueue()
    {
        std::vector<Core::Tasks::LocalTask> tasks;
        {
            std::lock_guard lock(m_MainThreadQueueMutex);
            if (m_MainThreadQueue.empty()) return;
            tasks.swap(m_MainThreadQueue);
        }

        for (auto& task : tasks)
        {
            task();
        }
    }

    void AssetPipeline::TrackMaterial(Core::Assets::AssetHandle handle)
    {
        m_LoadedMaterials.push_back(handle);
    }
}
