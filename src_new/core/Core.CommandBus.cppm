module;

#include <unordered_map>
#include <string>
#include <functional>
#include <memory>

export module Core.CommandBus;

import Core.TypeToken;

namespace Extrinsic::Core
{
    export class CommandBus;

    export using CommandTypeKey = std::size_t;

    export enum class CommandStatus : std::uint8_t
    {
        Completed,
        Failed,
    };

    export struct CommandOutcome
    {
        CommandStatus Status{CommandStatus::Completed};
        std::string Error{};

        [[nodiscard]] static CommandOutcome Ok() noexcept { return {}; }

        [[nodiscard]] static CommandOutcome Fail(std::string error)
        {
            return {CommandStatus::Failed, std::move(error)};
        }
    };

    export class CommandEnvelope
    {
    public:
        CommandEnvelope() = default;

        template <typename TCommandContext, typename TCommand>
        [[nodiscard]] static CommandEnvelope Make(TCommandContext ctx, TCommand payload)
        {
            return CommandEnvelope(Core::TypeToken<TCommand>(),
                                   std::make_shared<void>(std::move(ctx)),
                                   std::make_shared<const TCommand>(std::move(payload)),
                                   Core::TypeNameOf<TCommand>());
        }

        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_Payload) && static_cast<bool>(m_Context); }
        [[nodiscard]] std::string_view TypeName() const noexcept { return m_TypeName; }

    private:
        friend class CommandBus;

        CommandEnvelope(CommandTypeKey type,
                        std::shared_ptr<void> ctx,
                        std::shared_ptr<const void> payload,
                        std::string_view typeName)
            : m_Type(type), m_Context(std::move(ctx)), m_Payload(std::move(payload)), m_TypeName(typeName)
        {
        }

        CommandTypeKey m_Type{0};
        std::shared_ptr<void> m_Context{};
        std::shared_ptr<const void> m_Payload{};
        std::string_view m_TypeName{};
    };


    export struct CommandCorrelationId
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(CommandCorrelationId,
                                             CommandCorrelationId) noexcept = default;
    };

    export struct CommandBusStats
    {
        std::uint64_t Executed{0};
        std::uint64_t Failed{0};
        std::uint64_t MissingHandler{0};
        std::uint64_t Drains{0};
        std::uint64_t LastDrainCount{0};
        std::uint64_t Discarded{0};
    };

    export class CommandBus
    {
    public:
        CommandBus() = default;
        CommandBus(const CommandBus&) = delete;
        CommandBus& operator=(const CommandBus&) = delete;

        template <typename TCommandContext, typename TCommand>
        void RegisterHandler(std::function<CommandOutcome(TCommandContext&, const TCommand&)> handler)
        {
            RegisterHandlerErased(
                Core::TypeToken<TCommand>(),
                Core::TypeNameOf<TCommand>(),
                [h = std::move(handler)](void* ctx, const void* handler) -> CommandOutcome
                {
                    return h(static_cast<TCommandContext*>(ctx), *static_cast<const TCommand*>(handler));
                });
        }

        template <typename TCommandContext, typename TCommand>
        CommandCorrelationId Enqueue(TCommandContext ctx, TCommand payload)
        {
            return Enqueue(CommandEnvelope::Make<TCommandContext, TCommand>(std::move(ctx), std::move(payload)));
        }

        CommandCorrelationId Enqueue(CommandEnvelope envelope);

        void Drain();

        std::size_t DiscardPending();

        [[nodiscard]] CommandBusStats Stats() const;

    private:
        using ErasedHandler = std::function<CommandOutcome(void*, const void*)>;

        struct HandlerRecord
        {
            ErasedHandler Handler{};
            std::string_view TypeName{};
        };

        struct PendingCommand
        {
            CommandEnvelope Envelope{};
            CommandCorrelationId Correlation{};
        };

        void RegisterHandlerErased(CommandTypeKey   type,
                           std::string_view typeName,
                           ErasedHandler    handler);

        mutable std::mutex m_QueueMutex;
        std::vector<PendingCommand> m_Pending;
        std::uint64_t m_NextCorrelation{1};

        std::unordered_map<CommandTypeKey, HandlerRecord> m_Handlers;

        bool m_Draining{false};
        CommandBusStats m_Stats{};
    };
}
