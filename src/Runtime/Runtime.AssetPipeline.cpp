module;
#include <mutex>
#include <algorithm>
#include <memory>
#include <vector>

module Runtime.AssetPipeline;

import Core.Logging;
import Core.Tasks;
import Core.Assets;
import RHI.Transfer;

namespace Runtime
{
    struct AssetPipeline::Impl
    {
        struct PendingLoad
        {
            Core::Assets::AssetHandle Handle;
            RHI::TransferToken Token;
            Core::Tasks::LocalTask OnComplete{};
        };

        explicit Impl(RHI::TransferManager& transferManager)
            : TransferManager(transferManager)
        {
        }

        Core::Assets::AssetManager AssetManager;
        std::mutex LoadMutex;
        std::vector<PendingLoad> PendingLoads;
        std::mutex MainThreadQueueMutex;
        std::vector<Core::Tasks::LocalTask> MainThreadQueue;
        std::vector<Core::Assets::AssetHandle> LoadedMaterials;
        RHI::TransferManager& TransferManager;
    };

    AssetPipeline::AssetPipeline(RHI::TransferManager& transferManager)
        : m_Impl(std::make_unique<Impl>(transferManager))
    {
        Core::Log::Info("AssetPipeline: Initialized.");
    }

    AssetPipeline::~AssetPipeline()
    {
        Core::Log::Info("AssetPipeline: Shutdown.");
    }

    Core::Assets::AssetManager& AssetPipeline::GetAssetManager()
    {
        return m_Impl->AssetManager;
    }

    const Core::Assets::AssetManager& AssetPipeline::GetAssetManager() const
    {
        return m_Impl->AssetManager;
    }

    void AssetPipeline::RegisterAssetLoad(Core::Assets::AssetHandle handle, RHI::TransferToken token)
    {
        RegisterAssetLoadWithCallback(handle, token, {});
    }

    void AssetPipeline::RegisterAssetLoadWithCallback(Core::Assets::AssetHandle handle,
                                                      RHI::TransferToken token,
                                                      Core::Tasks::LocalTask onComplete)
    {
        std::lock_guard lock(m_Impl->LoadMutex);
        m_Impl->PendingLoads.push_back({handle, token, std::move(onComplete)});
    }

    void AssetPipeline::EnqueueMainThreadTask(Core::Tasks::LocalTask task)
    {
        std::lock_guard lock(m_Impl->MainThreadQueueMutex);
        m_Impl->MainThreadQueue.emplace_back(std::move(task));
    }

    void AssetPipeline::ProcessUploads()
    {
        // 1. Cleanup staging memory
        m_Impl->TransferManager.GarbageCollect();

        // 2. Check for completions
        std::lock_guard lock(m_Impl->LoadMutex);
        if (m_Impl->PendingLoads.empty()) return;

        // Use erase-remove idiom to process finished loads
        auto it = std::remove_if(m_Impl->PendingLoads.begin(), m_Impl->PendingLoads.end(),
                                 [&](Impl::PendingLoad& load)
                                 {
                                     if (m_Impl->TransferManager.IsCompleted(load.Token))
                                     {
                                         if (load.OnComplete.Valid()) load.OnComplete();

                                         // Signal Core that the "External Processing" is done
                                         m_Impl->AssetManager.FinalizeLoad(load.Handle);
                                         return true; // Remove from list
                                     }
                                     return false; // Keep waiting
                                 });

        m_Impl->PendingLoads.erase(it, m_Impl->PendingLoads.end());
    }

    void AssetPipeline::ProcessMainThreadQueue()
    {
        std::vector<Core::Tasks::LocalTask> tasks;
        {
            std::lock_guard lock(m_Impl->MainThreadQueueMutex);
            if (m_Impl->MainThreadQueue.empty()) return;
            tasks.swap(m_Impl->MainThreadQueue);
        }

        for (auto& task : tasks)
        {
            task();
        }
    }

    void AssetPipeline::TrackMaterial(Core::Assets::AssetHandle handle)
    {
        m_Impl->LoadedMaterials.push_back(handle);
    }

    const std::vector<Core::Assets::AssetHandle>& AssetPipeline::GetLoadedMaterials() const
    {
        return m_Impl->LoadedMaterials;
    }

    void AssetPipeline::ClearLoadedMaterials()
    {
        m_Impl->LoadedMaterials.clear();
    }
}
