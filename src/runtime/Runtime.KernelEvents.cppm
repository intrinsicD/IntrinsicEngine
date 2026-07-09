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

#include <entt/signal/dispatcher.hpp>

export module Extrinsic.Runtime.KernelEvents;

import Extrinsic.Core.FrameGraph;

// ============================================================
// ARCH-008 - queued-only runtime kernel event bus.
//
// ADR-0024 D7: synchronous event dispatch is not part of the
// kernel API. Publishers enqueue typed events from any thread;
// the Engine pumps the bus at the two documented main-thread
// points. Events published while a pump is delivering are held
// for the next pump, bounding cascades and keeping teardown
// two-phase by construction.
//
// This wrapper owns the entt::dispatcher instance. Runtime code
// never reaches dispatcher::trigger through this API.
// ============================================================

namespace Extrinsic::Runtime
{
    export class EventBus;

    export using EventTypeKey = std::size_t;

    export template <typename TEvent>
    [[nodiscard]] consteval std::string_view EventTypeNameOf() noexcept
    {
#if defined(__clang__) || defined(__GNUC__)
        return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
        return __FUNCSIG__;
#else
        return "EventTypeNameOf<unknown>";
#endif
    }

    export struct EventSubscriptionHandle
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(EventSubscriptionHandle,
                                             EventSubscriptionHandle) noexcept = default;
    };

    export struct EventBusStats
    {
        std::uint64_t PumpCount{0};
        std::uint64_t LastPumpPublished{0};
        std::uint64_t LastPumpDelivered{0};
        std::uint64_t TotalPublished{0};
        std::uint64_t TotalDelivered{0};
        std::uint64_t ActiveSubscriptions{0};
        std::uint64_t Unsubscribed{0};
    };

    export class EventBus
    {
    public:
        EventBus() = default;
        ~EventBus();

        EventBus(const EventBus&)            = delete;
        EventBus& operator=(const EventBus&) = delete;

        template <typename TEvent>
        [[nodiscard]] EventSubscriptionHandle Subscribe(
            std::function<void(const TEvent&)> callback)
        {
            if (!callback)
            {
                return {};
            }

            const EventSubscriptionHandle handle{m_NextSubscription++};
            auto subscription = std::make_unique<TypedSubscription<TEvent>>(
                *this,
                handle,
                EventTypeNameOf<TEvent>(),
                std::move(callback));

            auto& typed = *subscription;
            m_Dispatcher.sink<TEvent>().template connect<
                &TypedSubscription<TEvent>::OnEvent>(typed);
            m_Subscriptions.emplace(handle.Value, std::move(subscription));
            UpdateActiveSubscriptionCount();
            return handle;
        }

        void Unsubscribe(EventSubscriptionHandle handle);

        template <typename TEvent>
        void Publish(TEvent event)
        {
            auto payload = std::make_shared<const TEvent>(std::move(event));
            EnqueuePending(PendingEvent{
                Core::TypeToken<TEvent>(),
                EventTypeNameOf<TEvent>(),
                [payload = std::move(payload)](entt::dispatcher& dispatcher)
                {
                    dispatcher.enqueue<TEvent>(*payload);
                },
                [](entt::dispatcher& dispatcher)
                {
                    dispatcher.update<TEvent>();
                }});
        }

        // Main-thread pump. Merges the thread-safe inbox into the
        // dispatcher, delivers that bounded batch, and leaves events
        // published by listeners for the next Pump() call.
        void Pump();

        [[nodiscard]] EventBusStats Stats() const;
        [[nodiscard]] std::size_t PendingCount() const;

    private:
        struct PendingEvent
        {
            EventTypeKey Type{0};
            std::string_view TypeName{};
            std::function<void(entt::dispatcher&)> Enqueue{};
            std::function<void(entt::dispatcher&)> Update{};
        };

        struct SubscriptionBase
        {
            EventSubscriptionHandle Handle{};
            EventTypeKey Type{0};
            std::string_view TypeName{};
            bool Active{true};

            virtual ~SubscriptionBase() = default;
            virtual void Disconnect(entt::dispatcher& dispatcher) = 0;
        };

        template <typename TEvent>
        struct TypedSubscription final : SubscriptionBase
        {
            EventBus& Owner;
            std::function<void(const TEvent&)> Callback;

            TypedSubscription(EventBus& owner,
                              EventSubscriptionHandle handle,
                              std::string_view typeName,
                              std::function<void(const TEvent&)> callback)
                : Owner(owner), Callback(std::move(callback))
            {
                Handle = handle;
                Type = Core::TypeToken<TEvent>();
                TypeName = typeName;
            }

            void OnEvent(const TEvent& event)
            {
                if (!Active)
                {
                    return;
                }

                Owner.RecordDelivery();
                Callback(event);
            }

            void Disconnect(entt::dispatcher& dispatcher) override
            {
                dispatcher.sink<TEvent>().template disconnect<
                    &TypedSubscription<TEvent>::OnEvent>(*this);
            }
        };

        void EnqueuePending(PendingEvent pending);
        void RecordDelivery() noexcept;
        void CleanupPendingUnsubscriptions();
        void UpdateActiveSubscriptionCount();

        entt::dispatcher m_Dispatcher{};

        mutable std::mutex m_InboxMutex{};
        std::vector<PendingEvent> m_Inbox{};

        mutable std::mutex m_StatsMutex{};
        EventBusStats m_Stats{};

        std::uint64_t m_NextSubscription{1};
        std::unordered_map<std::uint64_t, std::unique_ptr<SubscriptionBase>>
            m_Subscriptions{};
        std::vector<EventSubscriptionHandle> m_PendingUnsubscriptions{};

        bool m_Pumping{false};
        std::uint64_t m_DeliveriesThisPump{0};
    };
}
