module;

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Runtime.KernelEvents;

import Extrinsic.Core.Logging;

namespace Extrinsic::Runtime
{
    KernelEventSubscription KernelEventBus::SubscribeErased(
        const KernelEventTypeKey type,
        const std::string_view typeName,
        ErasedListener listener)
    {
        std::lock_guard lock(m_SubscriptionMutex);
        KernelEventSubscription handle{
            .Value = m_NextSubscription++,
            .EventType = type,
        };
        m_Subscribers[type].push_back(SubscriberRecord{
            .Handle = handle,
            .Listener = std::move(listener),
            .TypeName = typeName,
        });
        return handle;
    }

    void KernelEventBus::Unsubscribe(const KernelEventSubscription subscription)
    {
        if (!subscription.IsValid())
            return;

        std::lock_guard lock(m_SubscriptionMutex);
        auto it = m_Subscribers.find(subscription.EventType);
        if (it == m_Subscribers.end())
            return;

        std::vector<SubscriberRecord>& records = it->second;
        records.erase(std::remove_if(records.begin(),
                                     records.end(),
                                     [subscription](const SubscriberRecord& record)
                                     { return record.Handle == subscription; }),
                      records.end());
        if (records.empty())
            m_Subscribers.erase(it);
    }

    void KernelEventBus::Publish(KernelEventEnvelope envelope)
    {
        if (!envelope.IsValid())
        {
            Core::Log::Error(
                "[KernelEventBus] Rejected publish of an empty event envelope.");
            return;
        }

        {
            std::lock_guard lock(m_InboxMutex);
            m_Inbox.push_back(std::move(envelope));
        }
        {
            std::lock_guard lock(m_StatsMutex);
            m_Stats.PublishedEvents += 1;
        }
    }

    std::vector<KernelEventBus::ListenerSnapshot>
        KernelEventBus::SnapshotListeners(const KernelEventTypeKey type) const
    {
        std::vector<ListenerSnapshot> listeners;
        std::lock_guard lock(m_SubscriptionMutex);
        const auto it = m_Subscribers.find(type);
        if (it == m_Subscribers.end())
            return listeners;

        listeners.reserve(it->second.size());
        for (const SubscriberRecord& record : it->second)
        {
            listeners.push_back(ListenerSnapshot{
                .Handle = record.Handle,
                .Listener = record.Listener,
            });
        }
        return listeners;
    }

    bool KernelEventBus::IsSubscribed(
        const KernelEventSubscription subscription) const
    {
        if (!subscription.IsValid())
            return false;

        std::lock_guard lock(m_SubscriptionMutex);
        const auto it = m_Subscribers.find(subscription.EventType);
        if (it == m_Subscribers.end())
            return false;

        return std::any_of(it->second.begin(),
                           it->second.end(),
                           [subscription](const SubscriberRecord& record)
                           { return record.Handle == subscription; });
    }

    std::uint64_t KernelEventBus::Pump()
    {
        if (m_Pumping)
        {
            Core::Log::Error(
                "[KernelEventBus] Reentrant Pump() refused; queued events remain "
                "for the next pump point.");
            return 0;
        }

        std::vector<KernelEventEnvelope> batch;
        {
            std::lock_guard lock(m_InboxMutex);
            batch.swap(m_Inbox);
        }

        struct PumpingScope final
        {
            bool& Flag;
            explicit PumpingScope(bool& flag) : Flag(flag) { Flag = true; }
            ~PumpingScope() { Flag = false; }
        } pumpingScope{m_Pumping};

        {
            std::lock_guard lock(m_StatsMutex);
            m_Stats.Pumps += 1;
            m_Stats.LastPumpEvents = static_cast<std::uint64_t>(batch.size());
            m_Stats.LastPumpDeliveredEvents = 0;
            m_Stats.LastPumpListenerInvocations = 0;
        }

        std::uint64_t deliveredEvents = 0;
        std::uint64_t listenerInvocations = 0;
        for (const KernelEventEnvelope& pending : batch)
        {
            const std::vector<ListenerSnapshot> listeners =
                SnapshotListeners(pending.m_Type);
            bool eventDelivered = false;
            for (const ListenerSnapshot& listener : listeners)
            {
                if (!IsSubscribed(listener.Handle))
                    continue;

                listener.Listener(pending.m_Payload.get());
                eventDelivered = true;
                listenerInvocations += 1;
            }

            if (eventDelivered)
                deliveredEvents += 1;
        }

        {
            std::lock_guard lock(m_StatsMutex);
            m_Stats.DeliveredEvents += deliveredEvents;
            m_Stats.LastPumpDeliveredEvents = deliveredEvents;
            m_Stats.ListenerInvocations += listenerInvocations;
            m_Stats.LastPumpListenerInvocations = listenerInvocations;
        }
        return deliveredEvents;
    }

    KernelEventBusStats KernelEventBus::Stats() const
    {
        std::lock_guard lock(m_StatsMutex);
        return m_Stats;
    }
}
