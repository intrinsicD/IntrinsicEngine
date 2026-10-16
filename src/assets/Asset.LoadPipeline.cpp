module;

#include <memory>

#include <mutex>
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

    // Pipeline transitions lock before registry/event-queue operations; neither
    // invokes listeners here. Service transactions acquire their lock first.
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
            if (auto state = m_Registry->SetState(id, AssetState::Unloaded, AssetState::QueuedIO);
                !state.has_value())
            {
                return state;
            }

            m_AssetsInFlight.insert(id);
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
        // Joining the transition also joins event publication and in-flight removal.
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
        if (m_Registry == nullptr)
            return Core::Err(Core::ErrorCode::InvalidState);
        if (!m_AssetsInFlight.contains(id))
            return Core::Err(Core::ErrorCode::ResourceNotFound);

        if (m_TestHooks.AfterCpuDecodeClaim)
            m_TestHooks.AfterCpuDecodeClaim(id);
        if (auto toCpu = m_Registry->SetState(id, AssetState::QueuedIO, AssetState::LoadedCPU);
            !toCpu.has_value())
        {
            m_AssetsInFlight.erase(id);
            return toCpu;
        }
        if (auto ready = m_Registry->SetState(id, AssetState::LoadedCPU, AssetState::Ready);
            !ready.has_value())
        {
            (void)m_Registry->SetState(id, AssetState::LoadedCPU, AssetState::Failed);
            if (m_EventBus != nullptr)
                m_EventBus->Publish(id, AssetEvent::Failed);
            m_AssetsInFlight.erase(id);
            return ready;
        }
        if (m_TestHooks.BeforeCpuReadyEvent)
            m_TestHooks.BeforeCpuReadyEvent(id);
        if (m_EventBus != nullptr)
            m_EventBus->Publish(id, AssetEvent::Ready);
        m_AssetsInFlight.erase(id);
        return Core::Ok();
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
        return m_AssetsInFlight.contains(id);
    }

}
