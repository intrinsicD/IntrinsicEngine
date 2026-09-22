module;

#include <mutex>
#include <chrono>
#include <algorithm>
#include <utility>

module Extrinsic.Asset.LoadPipeline;

import Extrinsic.Core.Tasks;

namespace Extrinsic::Assets
{
    struct AssetLoadPipeline::AsyncState
    {
        std::mutex Mutex{};
        AssetLoadPipeline* Owner{nullptr};
    };

    namespace
    {
        // Pipeline transitions hold m_Mutex before registry/event-queue locks.
        // Neither callee invokes listeners; event flushing stays outside this lock.
        Core::Result SetStateChecked(AssetRegistry* registry, AssetId id, AssetState from, AssetState to)
        {
            return registry->SetState(id, from, to);
        }

    }

    AssetLoadPipeline::AssetLoadPipeline(AssetLoadPipelineTestHooks testHooks)
        : m_TestHooks(std::move(testHooks)), m_AsyncState(std::make_shared<AsyncState>())
    {
        m_AsyncState->Owner = this;
    }

    AssetLoadPipeline::~AssetLoadPipeline()
    {
        Shutdown();
    }

    void AssetLoadPipeline::Shutdown()
    {
        const std::shared_ptr<AsyncState> asyncState = m_AsyncState;
        if (asyncState)
        {
            std::scoped_lock asyncLock(asyncState->Mutex);
            if (asyncState->Owner == this)
                asyncState->Owner = nullptr;
        }
        std::scoped_lock lock(m_Mutex);
        m_Accepting = false;
        m_Registry = nullptr;
        m_EventBus = nullptr;
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
        const bool publishQueuedEvent = req.publishQueuedEvent;
        const AssetEvent queuedEvent = req.queuedEvent;
        {
            std::scoped_lock lock(m_Mutex);
            if (!m_Accepting || m_Registry == nullptr)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }
            if (auto state = SetStateChecked(m_Registry, id, AssetState::Unloaded, AssetState::QueuedIO);
                !state.has_value())
            {
                return state;
            }

            auto& entry = m_AssetsInFlight[id];
            entry.request = std::move(req);
            entry.stages.clear();
            entry.decodeDone = false;
            entry.uploadDone = false;
            entry.finalized = false;
            AppendStageStamp(entry, Stage::AssetIO);
            if (publishQueuedEvent && m_EventBus != nullptr)
                m_EventBus->Publish(id, queuedEvent);
        }

        if (Core::Tasks::Scheduler::IsInitialized())
        {
            const std::shared_ptr<AsyncState> asyncState =
                m_AsyncState;
            Core::Tasks::Scheduler::Dispatch(
                [asyncState, id]
            {
                std::scoped_lock lock(asyncState->Mutex);
                if (asyncState->Owner != nullptr)
                    (void)asyncState->Owner->OnCpuDecoded(id);
            });
            return Core::Ok();
        }

        return OnCpuDecoded(id);
    }

    Core::Result AssetLoadPipeline::OnCpuDecoded(AssetId id)
    {
        std::scoped_lock lock(m_Mutex);
        return OnCpuDecodedUnlocked(id);
    }

    Core::Result AssetLoadPipeline::CompleteCpuLoad(AssetId id)
    {
        // Joining the transition also joins event publication and archival.
        // Ready alone is insufficient while another completion still owns the lock.
        std::scoped_lock lock(m_Mutex);
        if (m_Registry == nullptr)
            return Core::Err(Core::ErrorCode::InvalidState);
        if (m_AssetsInFlight.contains(id))
        {
            if (auto result = OnCpuDecodedUnlocked(id); !result.has_value())
                return result;
        }
        const auto state = m_Registry->GetState(id);
        if (!state.has_value())
            return Core::Err(state.error());
        return *state == AssetState::Ready
            ? Core::Ok() : Core::Err(Core::ErrorCode::InvalidState);
    }

    Core::Result AssetLoadPipeline::OnCpuDecodedUnlocked(AssetId id)
    {
        AssetRegistry* registry = nullptr;
        AssetEventBus* eventBus = nullptr;
        bool needsGpu = false;

        {
            if (m_Registry == nullptr)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }
            const auto it = m_AssetsInFlight.find(id);
            if (it == m_AssetsInFlight.end())
            {
                return Core::Err(Core::ErrorCode::ResourceNotFound);
            }
            if (it->second.decodeDone)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }

            registry = m_Registry;
            eventBus = m_EventBus;
            needsGpu = it->second.request.needsGpuUpload;
            AppendStageStamp(it->second, Stage::AssetDecode);
            it->second.decodeDone = true;
        }

        if (m_TestHooks.AfterCpuDecodeClaim)
            m_TestHooks.AfterCpuDecodeClaim(id);

        if (auto toCpu = SetStateChecked(registry, id, AssetState::QueuedIO, AssetState::LoadedCPU); !toCpu.has_value())
        {
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
                ArchiveTrailUnlocked(id);
                return q;
            }
            return Core::Ok();
        }

        {
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
            ArchiveTrailUnlocked(id);
            return ready;
        }

        if (m_TestHooks.BeforeCpuReadyEvent)
            m_TestHooks.BeforeCpuReadyEvent(id);

        if (eventBus != nullptr)
        {
            eventBus->Publish(id, AssetEvent::Ready);
        }

        ArchiveTrailUnlocked(id);
        return Core::Ok();
    }

    Core::Result AssetLoadPipeline::OnGpuUploaded(AssetId id)
    {
        std::scoped_lock lock(m_Mutex);
        AssetRegistry* registry = nullptr;
        AssetEventBus* eventBus = nullptr;
        AssetState stateBefore = AssetState::Unloaded;

        {
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

            ArchiveTrailUnlocked(id);
            return ready;
        }

        if (eventBus != nullptr)
        {
            eventBus->Publish(id, AssetEvent::Ready);
        }

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
        std::scoped_lock lock(m_Mutex);
        AssetRegistry* registry = nullptr;
        AssetEventBus* eventBus = nullptr;
        {
            if (m_Registry == nullptr)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }
            registry = m_Registry;
            eventBus = m_EventBus;
        }

        // State can change between observation and compare-and-set; retry only
        // InvalidState results that represent benign contention.
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto meta = registry->GetMeta(id);
            if (!meta.has_value())
            {
                return Core::Err(meta.error());
            }
            if (meta->state == AssetState::Failed)
            {
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
                ArchiveTrailUnlocked(id);
                return Core::Ok();
            }
            if (r.error() != Core::ErrorCode::InvalidState)
            {
                return r;
            }
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
