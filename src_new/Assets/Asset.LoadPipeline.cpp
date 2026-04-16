module;

#include <mutex>
#include <utility>

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

        if (auto state = SetStateChecked(registry, id, AssetState::Unloaded, AssetState::QueuedIO);
            !state.has_value())
        {
            return state;
        }

        {
            std::scoped_lock lock(m_Mutex);
            m_AssetsInFlight[id] = std::move(req);
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
            needsGpu = it->second.needsGpuUpload;
        }

        if (auto toCpu = SetStateChecked(registry, id, AssetState::QueuedIO, AssetState::LoadedCPU);
            !toCpu.has_value())
        {
            std::scoped_lock lock(m_Mutex);
            m_AssetsInFlight.erase(id);
            return toCpu;
        }

        if (needsGpu)
        {
            if (auto q = SetStateChecked(registry, id, AssetState::LoadedCPU, AssetState::QueuedGPU);
                !q.has_value())
            {
                (void)SetStateChecked(registry, id, AssetState::LoadedCPU, AssetState::Failed);
                if (eventBus != nullptr)
                {
                    eventBus->Publish(id, AssetEvent::Failed);
                }
                std::scoped_lock lock(m_Mutex);
                m_AssetsInFlight.erase(id);
                return q;
            }
            return Core::Ok();
        }

        if (auto ready = SetStateChecked(registry, id, AssetState::LoadedCPU, AssetState::Ready);
            !ready.has_value())
        {
            (void)SetStateChecked(registry, id, AssetState::LoadedCPU, AssetState::Failed);
            if (eventBus != nullptr)
            {
                eventBus->Publish(id, AssetEvent::Failed);
            }
            std::scoped_lock lock(m_Mutex);
            m_AssetsInFlight.erase(id);
            return ready;
        }

        if (eventBus != nullptr)
        {
            eventBus->Publish(id, AssetEvent::Ready);
        }

        std::scoped_lock lock(m_Mutex);
        m_AssetsInFlight.erase(id);
        return Core::Ok();
    }

    Core::Result AssetLoadPipeline::OnGpuUploaded(AssetId id)
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

        if (auto ready = SetStateChecked(registry, id, AssetState::QueuedGPU, AssetState::Ready);
            !ready.has_value())
        {
            (void)SetStateChecked(registry, id, AssetState::QueuedGPU, AssetState::Failed);
            if (eventBus != nullptr)
            {
                eventBus->Publish(id, AssetEvent::Failed);
            }
            std::scoped_lock lock(m_Mutex);
            m_AssetsInFlight.erase(id);
            return ready;
        }

        if (eventBus != nullptr)
        {
            eventBus->Publish(id, AssetEvent::Ready);
        }

        std::scoped_lock lock(m_Mutex);
        m_AssetsInFlight.erase(id);
        return Core::Ok();
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
                m_AssetsInFlight.erase(id);
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
                m_AssetsInFlight.erase(id);
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
        m_AssetsInFlight.erase(id);
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
}
