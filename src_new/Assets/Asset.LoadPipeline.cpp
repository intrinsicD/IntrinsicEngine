module;

#include <mutex>

module Extrinsic.Asset.LoadPipeline;

import Extrinsic.Core.Tasks;

namespace Extrinsic::Assets
{
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

    Core::Result AssetLoadPipeline::EnqueueIO(const LoadRequest& req)
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Registry == nullptr)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }

            if (auto state = m_Registry->SetState(req.id, AssetState::Unloaded, AssetState::QueuedIO); !state.
                has_value())
            {
                return state;
            }

            m_AssetsInFlight[req.id] = req;
        }

        if (Core::Tasks::Scheduler::IsInitialized())
        {
            Core::Tasks::Scheduler::Dispatch([this, id = req.id]
            {
                (void)OnCpuDecoded(id);
            });
            //TODO: Should we log here that we dispatch it or log the fallback use?
            return Core::Ok();
        }

        return OnCpuDecoded(req.id);
    }

    Core::Result AssetLoadPipeline::OnCpuDecoded(AssetId id)
    {
        std::scoped_lock lock(m_Mutex);
        if (m_Registry == nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState); //TODO: What about these ErrorCodes? Should we make them more explicit to exactly know whats wrong? like: Core::ErrorCode::AssetRegistryNotBound?
        }

        const auto it = m_AssetsInFlight.find(id);
        if (it == m_AssetsInFlight.end())
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        if (auto toCpu = m_Registry->SetState(id, AssetState::QueuedIO, AssetState::LoadedCPU); !toCpu.has_value())
        {
            return toCpu;
        }

        if (it->second.needsGpuUpload)
        {
            if (auto q = m_Registry->SetState(id, AssetState::LoadedCPU, AssetState::QueuedGPU); !q.has_value())
            {
                (void)m_Registry->SetState(id, AssetState::LoadedCPU, AssetState::Failed);
                if (m_EventBus != nullptr)
                {
                    m_EventBus->Publish(id, AssetEvent::Failed);
                }
                return q;
            }
            return Core::Ok();
        }

        if (auto ready = m_Registry->SetState(id, AssetState::LoadedCPU, AssetState::Ready); !ready.has_value())
        {
            (void)m_Registry->SetState(id, AssetState::LoadedCPU, AssetState::Failed);
            if (m_EventBus != nullptr)
            {
                m_EventBus->Publish(id, AssetEvent::Failed);
            }
            return ready;
        }

        if (m_EventBus != nullptr)
        {
            m_EventBus->Publish(id, AssetEvent::Ready);
        }

        m_AssetsInFlight.erase(it);
        return Core::Ok();
    }

    Core::Result AssetLoadPipeline::OnGpuUploaded(AssetId id)
    {
        std::scoped_lock lock(m_Mutex);
        if (m_Registry == nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        if (auto ready = m_Registry->SetState(id, AssetState::QueuedGPU, AssetState::Ready); !ready.has_value())
        {
            (void)m_Registry->SetState(id, AssetState::QueuedGPU, AssetState::Failed);
            if (m_EventBus != nullptr)
            {
                m_EventBus->Publish(id, AssetEvent::Failed);
            }
            return ready;
        }

        if (m_EventBus != nullptr)
        {
            m_EventBus->Publish(id, AssetEvent::Ready);
        }

        m_AssetsInFlight.erase(id);
        return Core::Ok();
    }
}
