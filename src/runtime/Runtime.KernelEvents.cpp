module;

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

module Extrinsic.Runtime.KernelEvents;

import Extrinsic.Core.Logging;

namespace Extrinsic::Runtime
{
    EventBus::~EventBus()
    {
        for (auto& [_, subscription] : m_Subscriptions)
        {
            subscription->Disconnect(m_Dispatcher);
        }
    }

    void EventBus::EnqueuePending(PendingEvent pending)
    {
        if (!pending.Enqueue || !pending.Update)
        {
            Core::Log::Error("[EventBus] Rejected enqueue of an empty event.");
            return;
        }

        {
            std::lock_guard lock(m_InboxMutex);
            m_Inbox.push_back(std::move(pending));
        }

        {
            std::lock_guard lock(m_StatsMutex);
            m_Stats.TotalPublished += 1;
        }
    }

    void EventBus::RecordDelivery() noexcept
    {
        m_DeliveriesThisPump += 1;
    }

    void EventBus::Unsubscribe(EventSubscriptionHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        const auto it = m_Subscriptions.find(handle.Value);
        if (it == m_Subscriptions.end())
        {
            return;
        }
        if (!it->second->Active)
        {
            return;
        }

        it->second->Active = false;
        {
            std::lock_guard lock(m_StatsMutex);
            m_Stats.Unsubscribed += 1;
        }

        if (m_Pumping)
        {
            m_PendingUnsubscriptions.push_back(handle);
            return;
        }

        it->second->Disconnect(m_Dispatcher);
        m_Subscriptions.erase(it);
        UpdateActiveSubscriptionCount();
    }

    void EventBus::CleanupPendingUnsubscriptions()
    {
        for (const EventSubscriptionHandle handle : m_PendingUnsubscriptions)
        {
            const auto it = m_Subscriptions.find(handle.Value);
            if (it == m_Subscriptions.end())
            {
                continue;
            }
            it->second->Disconnect(m_Dispatcher);
            m_Subscriptions.erase(it);
        }
        m_PendingUnsubscriptions.clear();
    }

    void EventBus::UpdateActiveSubscriptionCount()
    {
        std::lock_guard lock(m_StatsMutex);
        m_Stats.ActiveSubscriptions =
            static_cast<std::uint64_t>(m_Subscriptions.size());
    }

    void EventBus::Pump()
    {
        if (m_Pumping)
        {
            Core::Log::Error(
                "[EventBus] Reentrant Pump() refused; queued events remain "
                "deferred for the next pump point.");
            return;
        }

        std::vector<PendingEvent> batch;
        {
            std::lock_guard lock(m_InboxMutex);
            batch.swap(m_Inbox);
        }

        struct PumpScope final
        {
            bool& Flag;
            explicit PumpScope(bool& flag) : Flag(flag) { Flag = true; }
            ~PumpScope() { Flag = false; }
        } pumpScope{m_Pumping};

        m_DeliveriesThisPump = 0;
        for (PendingEvent& pending : batch)
        {
            pending.Enqueue(m_Dispatcher);
            pending.Update(m_Dispatcher);
        }
        const std::uint64_t delivered = m_DeliveriesThisPump;
        CleanupPendingUnsubscriptions();

        {
            std::lock_guard lock(m_StatsMutex);
            m_Stats.PumpCount += 1;
            m_Stats.LastPumpPublished =
                static_cast<std::uint64_t>(batch.size());
            m_Stats.LastPumpDelivered = delivered;
            m_Stats.TotalDelivered += delivered;
            m_Stats.ActiveSubscriptions =
                static_cast<std::uint64_t>(m_Subscriptions.size());
        }
    }

    EventBusStats EventBus::Stats() const
    {
        std::lock_guard lock(m_StatsMutex);
        return m_Stats;
    }

    std::size_t EventBus::PendingCount() const
    {
        std::lock_guard lock(m_InboxMutex);
        return m_Inbox.size();
    }
}
