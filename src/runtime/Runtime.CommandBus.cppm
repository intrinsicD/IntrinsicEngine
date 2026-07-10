module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

export module Extrinsic.Runtime.CommandBus;

import Extrinsic.Core.FrameGraph;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldRegistry;

// ============================================================
// ARCH-007 — Kernel command bus with a single pre-sim drain point.
//
// ADR-0024 D5: commands are plain-data payloads with correlation
// IDs. Enqueue is thread-safe and allowed from any thread or
// phase; execution happens main-thread-only at the single drain
// point in `Engine::RunFrame()` between platform input and the
// fixed-step simulation. Everything the simulation and renderer
// observe in a frame is post-command, pre-tick: one mutation
// window, deterministic, replayable.
//
// Fail-closed (ADR-0024 D5): draining a command type with no
// registered handler is a loud diagnostic, never a silent drop.
//
// Commands enqueued *during* a drain (by handlers) are deferred
// to the next frame's drain — mirroring the queued-events
// next-pump rule (D7) so cascades stay bounded and ordered.
//
// D13: handlers receive a `CommandContext` carrying narrow
// capabilities, never `Engine&`. The context grows additional
// members (events, jobs, worlds) as ARCH-008..ARCH-010 land.
//
// The repository builds with -fno-rtti and -fno-exceptions: type
// identity uses the same compile-time FNV-1a type tokens the
// FrameGraph uses (`Core::TypeToken<T>()`, no RTTI), diagnostics
// names come from the compiler signature at compile time, and
// handlers/hooks must not throw (any throw terminates the
// process by construction — there is no exception path to guard).
//
// Layering: kernel substrate per ADR-0024 D9 — no domain nouns.
// ============================================================

namespace Extrinsic::Runtime
{
    export class CommandBus;

    // Compile-time command-type identity (no RTTI): the FrameGraph's
    // FNV-1a token of the compiler type signature.
    export using CommandTypeKey = std::size_t;

    // Compile-time, allocation-free diagnostics name for a command
    // type. The returned view points at the compiler's function
    // signature literal (static storage duration) and contains the
    // type name; precise formatting is compiler-specific and only
    // used for logs.
    export template <typename TCommand>
    [[nodiscard]] consteval std::string_view CommandTypeNameOf() noexcept
    {
#if defined(__clang__) || defined(__GNUC__)
        return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
        return __FUNCSIG__;
#else
        return "CommandTypeNameOf<unknown>";
#endif
    }

    export struct CommandCorrelationId
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(CommandCorrelationId,
                                             CommandCorrelationId) noexcept = default;
    };

    export enum class CommandStatus : std::uint8_t
    {
        Completed,
        Failed,
    };

    export struct CommandOutcome
    {
        CommandStatus Status{CommandStatus::Completed};
        std::string   Error{};

        [[nodiscard]] static CommandOutcome Ok() noexcept { return {}; }
        [[nodiscard]] static CommandOutcome Fail(std::string error)
        {
            return {CommandStatus::Failed, std::move(error)};
        }
    };

    // Narrow-capability context (ADR-0024 D13). Deliberately no
    // `Engine&`: a handler that needs engine-wide behavior asks for
    // it via a command (e.g. `QuitRequested`), not a god-handle.
    export struct CommandContext
    {
        ECS::Scene::Registry& ActiveWorld;
        CommandBus&           Commands;
        KernelEventBus*       Events{};
        JobService*           Jobs{};
        WorldRegistry*        Worlds{};
        CommandCorrelationId  Correlation{};
    };

    export struct CommandDrainServices
    {
        KernelEventBus* Events{};
        JobService* Jobs{};
        WorldRegistry* Worlds{};
    };

    // Opaque, copyable, re-enqueueable command payload. Produced by
    // `CommandEnvelope::Make<T>` (the same erasure `Enqueue<T>` uses)
    // and consumed by `CommandBus::Enqueue(CommandEnvelope)` — the
    // undo path re-enqueues the inverse envelope recorded by a
    // handler. Payloads are immutable once wrapped.
    export class CommandEnvelope
    {
    public:
        CommandEnvelope() = default;

        template <typename TCommand>
        [[nodiscard]] static CommandEnvelope Make(TCommand payload)
        {
            return CommandEnvelope(Core::TypeToken<TCommand>(),
                                   std::make_shared<const TCommand>(std::move(payload)),
                                   CommandTypeNameOf<TCommand>());
        }

        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_Payload); }
        [[nodiscard]] std::string_view TypeName() const noexcept { return m_TypeName; }

    private:
        friend class CommandBus;

        CommandEnvelope(CommandTypeKey type,
                        std::shared_ptr<const void> payload,
                        std::string_view typeName)
            : m_Type(type), m_Payload(std::move(payload)), m_TypeName(typeName)
        {
        }

        CommandTypeKey              m_Type{0};
        std::shared_ptr<const void> m_Payload{};
        std::string_view            m_TypeName{};
    };

    // Post-execution history record (ADR-0024 D5 undo seam). The bus
    // notifies the installed hook after a *successful* execution that
    // recorded an inverse payload; which commands are undoable is
    // module policy, not kernel policy. Undo = re-enqueue `Inverse`.
    export struct CommandHistoryRecord
    {
        std::string_view     CommandTypeName{};
        CommandCorrelationId Correlation{};
        CommandEnvelope      Inverse{};
    };

    export using CommandHistoryHook = std::function<void(const CommandHistoryRecord&)>;

    // Cumulative drain diagnostics; intended for tests and telemetry.
    export struct CommandBusStats
    {
        std::uint64_t Executed{0};
        std::uint64_t Failed{0};
        std::uint64_t MissingHandler{0};
        std::uint64_t Drains{0};
        std::uint64_t LastDrainCount{0};
        std::uint64_t Discarded{0};
    };

    // Built-in kernel command (ADR-0024 D13): the sanctioned way for
    // module/UI code to stop the engine. The Engine registers the
    // handler during `Initialize()`; nothing else calls shutdown
    // entry points directly.
    export struct QuitRequested
    {
    };

    export class CommandBus
    {
    public:
        CommandBus() = default;
        CommandBus(const CommandBus&)            = delete;
        CommandBus& operator=(const CommandBus&) = delete;

        // Main-thread only, before the first drain that should see the
        // handler. Exactly one handler per command type (commands are
        // intents with one receiver — ADR-0024 D5/D6); re-registration
        // replaces, which tests use to stub behavior.
        template <typename TCommand>
        void RegisterHandler(
            std::function<CommandOutcome(CommandContext&, const TCommand&)> handler)
        {
            RegisterHandlerErased(
                Core::TypeToken<TCommand>(),
                CommandTypeNameOf<TCommand>(),
                [h = std::move(handler)](CommandContext& ctx,
                                         const void*     payload) -> CommandOutcome
                { return h(ctx, *static_cast<const TCommand*>(payload)); });
        }

        // Thread-safe; any thread, any phase. The payload is copied
        // into the envelope at enqueue time — never capture references
        // into UI state (ADR-0024 D5).
        template <typename TCommand>
        CommandCorrelationId Enqueue(TCommand payload)
        {
            return Enqueue(CommandEnvelope::Make<TCommand>(std::move(payload)));
        }

        // Re-enqueue path (undo, replay). Thread-safe.
        CommandCorrelationId Enqueue(CommandEnvelope envelope);

        // Executes every command enqueued before this call, in enqueue
        // order, on the calling thread. Called exactly once per frame
        // by `Engine::RunFrame()` between platform input and the
        // fixed-step simulation; commands enqueued by handlers during
        // the drain run at the NEXT drain. Reentrant drains are a
        // programming error and are refused with a diagnostic.
        void Drain(ECS::Scene::Registry& activeWorld);
        void Drain(ECS::Scene::Registry& activeWorld,
                   CommandDrainServices services);

        // During handler execution only: record the inverse payload
        // that undoes the currently executing command. Forwarded to
        // the history hook after successful execution.
        void RecordInverse(CommandEnvelope inverse);

        // Install the post-execution history hook (e.g. the editor
        // command history). One hook; replaces any previous hook.
        // Hooks must not throw (the codebase builds with
        // -fno-exceptions; a throw is a process-terminating defect).
        void SetHistoryHook(CommandHistoryHook hook);

        // Drop every command still queued without executing it and
        // return the number dropped. Called by `Engine::Shutdown()` so
        // commands enqueued after the final frame's drain (e.g. from
        // the variable tick just before exit) cannot replay into a
        // freshly re-initialized scene on the documented
        // Shutdown() + Initialize() reuse path. Thread-safe; logs when
        // it drops a non-empty queue.
        std::size_t DiscardPending();

        [[nodiscard]] CommandBusStats Stats() const;

    private:
        using ErasedHandler =
            std::function<CommandOutcome(CommandContext&, const void*)>;

        struct HandlerRecord
        {
            ErasedHandler    Handler{};
            std::string_view TypeName{};
        };

        struct PendingCommand
        {
            CommandEnvelope      Envelope{};
            CommandCorrelationId Correlation{};
        };

        void RegisterHandlerErased(CommandTypeKey   type,
                                   std::string_view typeName,
                                   ErasedHandler    handler);

        // Pending queue: mutex-guarded multi-producer queue swapped
        // whole at drain time (single consumer). Handlers/registry are
        // main-thread-only and need no lock beyond the queue's.
        mutable std::mutex          m_QueueMutex;
        std::vector<PendingCommand> m_Pending;
        std::uint64_t               m_NextCorrelation{1};

        std::unordered_map<CommandTypeKey, HandlerRecord> m_Handlers;
        CommandHistoryHook                                m_HistoryHook{};

        bool            m_Draining{false};
        CommandEnvelope m_RecordedInverse{};
        CommandBusStats m_Stats{};
    };
}
