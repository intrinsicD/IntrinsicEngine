module;

#include <exception>
#include <mutex>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

module Extrinsic.Runtime.CommandBus;

import Extrinsic.Core.Logging;

namespace Extrinsic::Runtime
{
    CommandCorrelationId CommandBus::Enqueue(CommandEnvelope envelope)
    {
        if (!envelope.IsValid())
        {
            Core::Log::Error(
                "[CommandBus] Rejected enqueue of an empty CommandEnvelope.");
            return {};
        }

        std::lock_guard lock(m_QueueMutex);
        const CommandCorrelationId correlation{m_NextCorrelation++};
        m_Pending.push_back(PendingCommand{std::move(envelope), correlation});
        return correlation;
    }

    void CommandBus::RegisterHandlerErased(std::type_index type,
                                           std::string     typeName,
                                           ErasedHandler   handler)
    {
        m_Handlers[type] = HandlerRecord{std::move(handler), std::move(typeName)};
    }

    void CommandBus::RecordInverse(CommandEnvelope inverse)
    {
        if (!m_Draining)
        {
            Core::Log::Error(
                "[CommandBus] RecordInverse called outside of a drain; ignored.");
            return;
        }
        m_RecordedInverse = std::move(inverse);
    }

    void CommandBus::SetHistoryHook(CommandHistoryHook hook)
    {
        m_HistoryHook = std::move(hook);
    }

    CommandBusStats CommandBus::Stats() const
    {
        return m_Stats;
    }

    void CommandBus::Drain(ECS::Scene::Registry& activeWorld)
    {
        if (m_Draining)
        {
            Core::Log::Error(
                "[CommandBus] Reentrant Drain() refused; commands remain queued "
                "for the next frame's drain point.");
            return;
        }

        // Swap out exactly the batch that existed at the drain point.
        // Handler-enqueued follow-ups land in the (now empty) live
        // queue and execute at the next frame's drain (ADR-0024 D5).
        std::vector<PendingCommand> batch;
        {
            std::lock_guard lock(m_QueueMutex);
            batch.swap(m_Pending);
        }

        m_Draining = true;
        m_Stats.Drains += 1;
        m_Stats.LastDrainCount = static_cast<std::uint64_t>(batch.size());

        for (PendingCommand& pending : batch)
        {
            const auto handlerIt = m_Handlers.find(pending.Envelope.m_Type);
            if (handlerIt == m_Handlers.end())
            {
                // Fail-closed (ADR-0024 D5): a command nobody handles is
                // a defect at the composition root, not a no-op.
                m_Stats.MissingHandler += 1;
                Core::Log::Error(
                    "[CommandBus] No handler registered for command type '{}' "
                    "(correlation {}); command dropped loudly.",
                    pending.Envelope.TypeName(),
                    pending.Correlation.Value);
                continue;
            }

            // Element references stay valid under unordered_map rehash,
            // so a handler registering new handlers mid-drain cannot
            // invalidate this record (replacing the *executing* handler
            // mid-call is documented as unsupported).
            const HandlerRecord& record = handlerIt->second;

            CommandContext context{activeWorld, *this, pending.Correlation};
            m_RecordedInverse = CommandEnvelope{};

            CommandOutcome outcome{};
            try
            {
                outcome = record.Handler(context,
                                         pending.Envelope.m_Payload.get());
            }
            catch (const std::exception& ex)
            {
                outcome = CommandOutcome::Fail(ex.what());
            }
            catch (...)
            {
                outcome = CommandOutcome::Fail("unknown exception");
            }

            if (outcome.Status == CommandStatus::Failed)
            {
                m_Stats.Failed += 1;
                Core::Log::Error(
                    "[CommandBus] Command '{}' (correlation {}) failed: {}",
                    record.TypeName,
                    pending.Correlation.Value,
                    outcome.Error);
                continue;
            }

            m_Stats.Executed += 1;
            if (m_HistoryHook && m_RecordedInverse.IsValid())
            {
                m_HistoryHook(CommandHistoryRecord{
                    record.TypeName,
                    pending.Correlation,
                    std::move(m_RecordedInverse)});
                m_RecordedInverse = CommandEnvelope{};
            }
        }

        m_Draining = false;
    }
}
