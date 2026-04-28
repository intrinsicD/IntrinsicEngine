module;

#include <mutex>
#include <chrono>
#include <algorithm>

module Extrinsic.Asset.LoadPipeline;

import Extrinsic.Core.Tasks;

namespace Extrinsic::Assets
{
    namespace
    {
        // Call SetState on the registry while NOT holding the pipeline's mutex.
        // The pipeline mutex only protects m_AssetsInFlight + binding pointers;
        // Registry has its own lock. Keeping the order strict avoids any cross-
        // lock ordering concern.
        Core::Result SetStateChecked(AssetRegistry* registry, AssetId id, AssetState from, AssetState to)
        {
            return registry->SetState(id, from, to);
        }

    }

    void AssetLoadPipeline::AppendStageStamp(InFlightEntry& entry, Stage stage)
    {
        entry.stages.push_back(StageStamp{
            .stage = stage,
            .timestamp = std::chrono::steady_clock::now(),
        });
    }

    void AssetLoadPipeline::ArchiveTrailUnlocked(const AssetId id)
    {
        const auto it = m_AssetsInFlight.find(id);
        if (it == m_AssetsInFlight.end())
        {
            return;
        }
        m_CompletedStageTrails[id] = it->second.stages;
        m_CompletedTrailOrder.push_back(id);
        m_AssetsInFlight.erase(it);

        while (m_CompletedTrailOrder.size() > kCompletedTrailCapacity)
        {
            const auto oldest = m_CompletedTrailOrder.front();
            m_CompletedTrailOrder.pop_front();
            m_CompletedStageTrails.erase(oldest);
        }

        for (auto fenceIt = m_FenceWaiters.begin(); fenceIt != m_FenceWaiters.end(); )
        {
            auto& ids = fenceIt->second;
            ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
            if (ids.empty())
                fenceIt = m_FenceWaiters.erase(fenceIt);
            else
                ++fenceIt;
        }
    }

    void AssetLoadPipeline::BindRegistry(AssetRegistry* registry)
    {
        std::scoped_lock lock(m_Mutex);
        m_Registry = registry;
    }

    void AssetLoadPipeline::BindEventBus(AssetEventBus* eventBus)
    {
        std::scoped_lock lock(m_Mutex);
        m_EventBus = eventBus;
    }

    Core::Result AssetLoadPipeline::EnqueueIO(LoadRequest req)
    {
        const AssetId id = req.id;
        AssetRegistry* registry = nullptr;
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Registry == nullptr)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }
            registry = m_Registry;
        }

        if (auto state = SetStateChecked(registry, id, AssetState::Unloaded, AssetState::QueuedIO); !state.
            has_value())
        {
            return state;
        }

        {
            std::scoped_lock lock(m_Mutex);
            auto& entry = m_AssetsInFlight[id];
            entry.request = std::move(req);
            entry.stages.clear();
            entry.decodeDone = false;
            entry.uploadDone = false;
            entry.finalized = false;
            AppendStageStamp(entry, Stage::AssetIO);
        }

        if (Core::Tasks::Scheduler::IsInitialized())
        {
            Core::Tasks::Scheduler::Dispatch([this, id]
            {
                (void)OnCpuDecoded(id);
            });
            return Core::Ok();
        }

        return OnCpuDecoded(id);
    }

    Core::Result AssetLoadPipeline::OnCpuDecoded(AssetId id)
    {
        AssetRegistry* registry = nullptr;
        AssetEventBus* eventBus = nullptr;
        bool needsGpu = false;

        {
            std::scoped_lock lock(m_Mutex);
            if (m_Registry == nullptr)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }
            const auto it = m_AssetsInFlight.find(id);
            if (it == m_AssetsInFlight.end())
            {
                return Core::Err(Core::ErrorCode::ResourceNotFound);
            }

            registry = m_Registry;
            eventBus = m_EventBus;
            needsGpu = it->second.request.needsGpuUpload;
            AppendStageStamp(it->second, Stage::AssetDecode);
            it->second.decodeDone = true;
        }

        if (auto toCpu = SetStateChecked(registry, id, AssetState::QueuedIO, AssetState::LoadedCPU); !toCpu.has_value())
        {
            std::scoped_lock lock(m_Mutex);
            ArchiveTrailUnlocked(id);
            return toCpu;
        }

        if (needsGpu)
        {
            if (auto q = SetStateChecked(registry, id, AssetState::LoadedCPU, AssetState::QueuedGPU); !q.has_value())
            {
                SetStateChecked(registry, id, AssetState::LoadedCPU, AssetState::Failed);
                if (eventBus != nullptr)
                {
                    eventBus->Publish(id, AssetEvent::Failed);
                }
                std::scoped_lock lock(m_Mutex);
                ArchiveTrailUnlocked(id);
                return q;
            }
            return Core::Ok();
        }

        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_AssetsInFlight.find(id);
            if (it == m_AssetsInFlight.end() || !it->second.decodeDone)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }
            AppendStageStamp(it->second, Stage::Finalize);
            it->second.finalized = true;
        }

        if (auto ready = SetStateChecked(registry, id, AssetState::LoadedCPU, AssetState::Ready); !ready.has_value())
        {
            SetStateChecked(registry, id, AssetState::LoadedCPU, AssetState::Failed);
            if (eventBus != nullptr)
            {
                eventBus->Publish(id, AssetEvent::Failed);
            }
            std::scoped_lock lock(m_Mutex);
            ArchiveTrailUnlocked(id);
            return ready;
        }

        if (eventBus != nullptr)
        {
            eventBus->Publish(id, AssetEvent::Ready);
        }

        std::scoped_lock lock(m_Mutex);
        ArchiveTrailUnlocked(id);
        return Core::Ok();
    }

    Core::Result AssetLoadPipeline::OnGpuUploaded(AssetId id)
    {
        AssetRegistry* registry = nullptr;
        AssetEventBus* eventBus = nullptr;
        AssetState stateBefore = AssetState::Unloaded;

        {
            std::scoped_lock lock(m_Mutex);
            if (m_Registry == nullptr)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }
            registry = m_Registry;
            eventBus = m_EventBus;
        }
        {
            const auto current = registry->GetState(id);
            if (!current.has_value())
            {
                return Core::Err(current.error());
            }
            stateBefore = *current;
            if (stateBefore != AssetState::QueuedGPU)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }
        }

        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_AssetsInFlight.find(id);
            if (it == m_AssetsInFlight.end())
            {
                return Core::Err(Core::ErrorCode::ResourceNotFound);
            }
            AppendStageStamp(it->second, Stage::AssetUpload);
            it->second.uploadDone = true;
            if (!it->second.decodeDone)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }
            AppendStageStamp(it->second, Stage::Finalize);
            it->second.finalized = true;
        }

        if (auto ready = SetStateChecked(registry, id, AssetState::QueuedGPU, AssetState::Ready); !ready.has_value())
        {
            SetStateChecked(registry, id, AssetState::QueuedGPU, AssetState::Failed);
            if (eventBus != nullptr)
            {
                eventBus->Publish(id, AssetEvent::Failed);
            }

            std::scoped_lock lock(m_Mutex);
            ArchiveTrailUnlocked(id);
            return ready;
        }

        if (eventBus != nullptr)
        {
            eventBus->Publish(id, AssetEvent::Ready);
        }

        std::scoped_lock lock(m_Mutex);
        ArchiveTrailUnlocked(id);
        return Core::Ok();
    }

    Core::Result AssetLoadPipeline::ArmGpuFence(const AssetId id, const uint64_t fenceValue)
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_AssetsInFlight.find(id);
        if (it == m_AssetsInFlight.end())
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }
        if (!it->second.decodeDone)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        m_FenceWaiters[fenceValue].push_back(id);
        return Core::Ok();
    }

    uint32_t AssetLoadPipeline::CompleteGpuFence(const uint64_t fenceValue)
    {
        std::vector<AssetId> ready{};
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_FenceWaiters.find(fenceValue);
            if (it == m_FenceWaiters.end())
            {
                return 0;
            }
            ready = std::move(it->second);
            m_FenceWaiters.erase(it);
        }

        uint32_t completed = 0;
        for (const auto id : ready)
        {
            if (OnGpuUploaded(id).has_value())
            {
                ++completed;
            }
        }
        return completed;
    }

    Core::Result AssetLoadPipeline::MarkFailed(AssetId id)
    {
        AssetRegistry* registry = nullptr;
        AssetEventBus* eventBus = nullptr;
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Registry == nullptr)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }
            registry = m_Registry;
            eventBus = m_EventBus;
        }

        // Retry-loop the compare-and-swap: the state may change between
        // our read and our write; tolerate benign races by re-reading.
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto meta = registry->GetMeta(id);
            if (!meta.has_value())
            {
                return Core::Err(meta.error());
            }
            if (meta->state == AssetState::Failed)
            {
                std::scoped_lock lock(m_Mutex);
                ArchiveTrailUnlocked(id);
                return Core::Ok();
            }
            auto r = SetStateChecked(registry, id, meta->state, AssetState::Failed);
            if (r.has_value())
            {
                if (eventBus != nullptr)
                {
                    eventBus->Publish(id, AssetEvent::Failed);
                }
                std::scoped_lock lock(m_Mutex);
                ArchiveTrailUnlocked(id);
                return Core::Ok();
            }
            if (r.error() != Core::ErrorCode::InvalidState)
            {
                return r;
            }
            // InvalidState = someone moved the state under us - retry.
        }
        return Core::Err(Core::ErrorCode::ResourceBusy);
    }

    void AssetLoadPipeline::Cancel(AssetId id)
    {
        std::scoped_lock lock(m_Mutex);
        ArchiveTrailUnlocked(id);
    }

    std::size_t AssetLoadPipeline::InFlightCount() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_AssetsInFlight.size();
    }

    bool AssetLoadPipeline::IsInFlight(AssetId id) const
    {
        std::scoped_lock lock(m_Mutex);
        return m_AssetsInFlight.find(id) != m_AssetsInFlight.end();
    }

    Core::Expected<std::vector<AssetLoadPipeline::StageStamp>> AssetLoadPipeline::GetStageTrail(AssetId id) const
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_AssetsInFlight.find(id);
        if (it != m_AssetsInFlight.end())
        {
            return it->second.stages;
        }

        const auto completedIt = m_CompletedStageTrails.find(id);
        if (completedIt == m_CompletedStageTrails.end())
        {
            return Core::Err<std::vector<StageStamp>>(Core::ErrorCode::ResourceNotFound);
        }
        return completedIt->second;
    }
}
