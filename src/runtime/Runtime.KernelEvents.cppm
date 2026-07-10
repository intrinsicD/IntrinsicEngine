module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

export module Extrinsic.Runtime.KernelEvents;

import Extrinsic.Core.FrameGraph;

// ============================================================
// ARCH-008 — Queued-only kernel event bus with two pump points.
//
// ADR-0024 D7: kernel events are always queued. Publish is
// thread-safe and allowed from workers through an inbox; listeners
// run only on the thread that calls Pump(). Engine owns exactly two
// Pump() calls per frame: post-command-drain and post-simulation.
//
// Events published during a Pump() are deferred to the next pump by
// construction: Pump() swaps out the batch that existed at entry and
// never recursively drains the live inbox.
//
// This is the runtime-kernel bus. It intentionally does not rename
// or absorb the asset-layer `Extrinsic.Asset.EventBus`.
// ============================================================

namespace Extrinsic::Runtime
{
    export using KernelEventTypeKey = std::size_t;

    export template <typename TEvent>
    [[nodiscard]] consteval std::string_view KernelEventTypeNameOf() noexcept
    {
#if defined(__clang__) || defined(__GNUC__)
        return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
        return __FUNCSIG__;
#else
        return "KernelEventTypeNameOf<unknown>";
#endif
    }

    export struct KernelEventSubscription
    {
        std::uint64_t      Value{0};
        KernelEventTypeKey EventType{0};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Value != 0 && EventType != 0;
        }
        [[nodiscard]] friend bool operator==(KernelEventSubscription,
                                             KernelEventSubscription) noexcept = default;
    };

    export class KernelEventEnvelope
    {
    public:
        KernelEventEnvelope() = default;

        template <typename TEvent>
        [[nodiscard]] static KernelEventEnvelope Make(TEvent payload)
        {
            return KernelEventEnvelope(Core::TypeToken<TEvent>(),
                                       std::make_shared<const TEvent>(std::move(payload)),
                                       KernelEventTypeNameOf<TEvent>());
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return static_cast<bool>(m_Payload);
        }
        [[nodiscard]] std::string_view TypeName() const noexcept
        {
            return m_TypeName;
        }

    private:
        friend class KernelEventBus;

        KernelEventEnvelope(KernelEventTypeKey     type,
                            std::shared_ptr<const void> payload,
                            std::string_view      typeName)
            : m_Type(type), m_Payload(std::move(payload)), m_TypeName(typeName)
        {
        }

        KernelEventTypeKey        m_Type{0};
        std::shared_ptr<const void> m_Payload{};
        std::string_view          m_TypeName{};
    };

    export struct KernelEventBusStats
    {
        std::uint64_t PublishedEvents{0};
        std::uint64_t Pumps{0};
        std::uint64_t LastPumpEvents{0};
        std::uint64_t DeliveredEvents{0};
        std::uint64_t LastPumpDeliveredEvents{0};
        std::uint64_t ListenerInvocations{0};
        std::uint64_t LastPumpListenerInvocations{0};
    };

    export class KernelEventBus
    {
    public:
        KernelEventBus() = default;
        KernelEventBus(const KernelEventBus&)            = delete;
        KernelEventBus& operator=(const KernelEventBus&) = delete;

        template <typename TEvent>
        [[nodiscard]] KernelEventSubscription Subscribe(
            std::function<void(const TEvent&)> listener)
        {
            if (!listener)
                return {};

            return SubscribeErased(
                Core::TypeToken<TEvent>(),
                KernelEventTypeNameOf<TEvent>(),
                [h = std::move(listener)](const void* payload)
                { h(*static_cast<const TEvent*>(payload)); });
        }

        void Unsubscribe(KernelEventSubscription subscription);

        template <typename TEvent>
        void Publish(TEvent payload)
        {
            Publish(KernelEventEnvelope::Make<TEvent>(std::move(payload)));
        }

        void Publish(KernelEventEnvelope envelope);

        // Main-thread pump. Delivers only the batch present at pump entry.
        // Publishes from listeners or worker threads land in the next pump.
        [[nodiscard]] std::uint64_t Pump();

        [[nodiscard]] KernelEventBusStats Stats() const;

    private:
        using ErasedListener = std::function<void(const void*)>;

        struct SubscriberRecord
        {
            KernelEventSubscription Handle{};
            ErasedListener          Listener{};
            std::string_view        TypeName{};
        };

        struct ListenerSnapshot
        {
            KernelEventSubscription Handle{};
            ErasedListener          Listener{};
        };

        [[nodiscard]] KernelEventSubscription SubscribeErased(
            KernelEventTypeKey type,
            std::string_view typeName,
            ErasedListener listener);
        [[nodiscard]] std::vector<ListenerSnapshot> SnapshotListeners(
            KernelEventTypeKey type) const;
        [[nodiscard]] bool IsSubscribed(KernelEventSubscription subscription) const;

        mutable std::mutex m_InboxMutex;
        std::vector<KernelEventEnvelope> m_Inbox;

        mutable std::mutex m_SubscriptionMutex;
        std::unordered_map<KernelEventTypeKey, std::vector<SubscriberRecord>>
            m_Subscribers;
        std::uint64_t m_NextSubscription{1};

        mutable std::mutex m_StatsMutex;
        KernelEventBusStats m_Stats{};

        bool m_Pumping{false};
    };
}
