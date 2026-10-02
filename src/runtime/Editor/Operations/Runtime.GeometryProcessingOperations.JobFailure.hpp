// Terminal failure status and diagnostics shared by queued editor methods, and
// the one setup/completion contract every queued editor job uses: duplicate-output
// refusal, revalidation of abandoned runs and deliver-once completion.
// Include after EditorCommon, Core.Error, JobService and EditorJobProjection
// imports; queued-job users also include `Runtime.EditorProcessingAccess.hpp`.
// The including unit supplies functional, memory, optional, string and string_view.
#pragma once

extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail::MeshSupport
{
    struct UnpublishedEditorJobFailure
    {
        EditorCommandStatus Status{EditorCommandStatus::GeometryProcessingFailed};
        Core::ErrorCode Error{Core::ErrorCode::Unknown};
        std::string Message{};
    };

    // Worker detail is copied only for Current; stale/cancelled reasons stand alone.
    // A non-empty `staleReason` replaces the generic text of StaleGeneration for
    // gates that know precisely what changed.
    [[nodiscard]] UnpublishedEditorJobFailure BuildUnpublishedEditorJobFailure(
        JobApplyValidation validation,
        std::string_view label,
        std::string_view detail = {},
        std::string_view staleReason = {});

    // Duplicate guard of a queued editor job. When `identity` already has an
    // active job, returns the refusal ("<label> already has an active <state>
    // job (job i:g).") that the caller answers with `EditorCommandStatus::Pending`
    // without registering its callback: the active job keeps delivering to its
    // own caller, and the agent lane reports such a call as `result_unavailable`.
    [[nodiscard]] std::optional<std::string> ActiveOutputJobRefusal(
        const EditorProcessingContext& context,
        const EditorJobIdentity& identity,
        std::string_view label);

    // Revalidation of one stage of a queued job on the main thread. An
    // abandoned run (cancelled, or an earlier stage finalized) answers
    // Cancelled; otherwise the caller's typed input check decides.
    [[nodiscard]] JobApplyValidation ValidateQueuedJob(
        bool abandoned, bool inputsCurrent) noexcept;

    // "<label> was cancelled or its source became stale; nothing was applied."
    [[nodiscard]] std::string QueuedJobUnpublishedMessage(std::string_view label);
    // "<label> job submission was rejected." (or "... rejected (<stage>).")
    [[nodiscard]] std::string QueuedJobRejectedMessage(
        std::string_view label, std::string_view stage = {});

    // Deliver-once completion of a queued editor job. Copies share one state,
    // so every stage of a multi-stage run captures the same delivery. The
    // callback is guarded against a detached attachment and fires at most once
    // per delivery: from `Publish` or a `Finalize*`, whichever comes first, and
    // never after `Rejected`. Main thread only, like the JobService callbacks that call it.
    template <class Result>
    class QueuedJobDelivery
    {
    public:
        QueuedJobDelivery(const EditorProcessingContext& context,
                          std::function<void(Result)> onComplete,
                          Result pending,
                          std::string label)
            : m_State(std::make_shared<State>(State{
                  .Sink = GuardEditorProcessingResult(context, std::move(onComplete)),
                  .Pending = std::move(pending),
                  .Label = std::move(label)}))
        {
            m_State->Pending.Status = EditorCommandStatus::Pending;
        }

        // The immediate answer of a queued submission.
        [[nodiscard]] const Result& Pending() const noexcept { return m_State->Pending; }

        // `PublishCompletion`: delivers the published result; its success is the
        // job's publication verdict (a failed one still runs `Finalize`, which
        // then delivers nothing more).
        bool Publish(Result result) const
        {
            const bool succeeded = result.Succeeded();
            Deliver(std::move(result));
            return succeeded;
        }

        // `FinalizeUnpublishedOnMainThread`. A failure the run recorded (a stage
        // that refused to continue) is delivered as it is; otherwise the pending
        // snapshot ends as StaleEntity with the shared wording.
        void Finalize(const std::optional<Result>& failure = std::nullopt) const
        {
            if (failure)
                Deliver(*failure);
            else
                FinalizeFrom(m_State->Pending);
        }
        // Finalize for jobs whose worker writes its result in place: a worker
        // that failed (`GeometryProcessingFailed`) is reported as it is.
        void FinalizeAfterWorker(const Result& worker) const
        {
            if (worker.Status == EditorCommandStatus::GeometryProcessingFailed)
                Deliver(worker);
            else
                FinalizeFrom(m_State->Pending);
        }
        // As `Finalize`, from the caller's latest progress instead of the snapshot.
        void FinalizeFrom(Result latest) const
        {
            latest.Status = EditorCommandStatus::StaleEntity;
            latest.Message = QueuedJobUnpublishedMessage(m_State->Label);
            Deliver(std::move(latest));
        }

        // A rejected submission. The failure is the immediate answer only (the
        // caller already reports it, as for every other immediate failure); the
        // delivery closes so an earlier stage's finalizer delivers nothing.
        [[nodiscard]] Result Rejected(std::string_view stage = {}) const
        {
            m_State->Delivered = true;
            auto result = m_State->Pending;
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Message = QueuedJobRejectedMessage(m_State->Label, stage);
            return result;
        }

    private:
        struct State
        {
            std::function<void(Result)> Sink{};
            Result Pending{};
            std::string Label{};
            bool Delivered{false};
        };

        void Deliver(Result result) const
        {
            if (m_State->Delivered)
                return;
            m_State->Delivered = true;
            if (m_State->Sink)
                m_State->Sink(std::move(result));
        }

        std::shared_ptr<State> m_State;
    };
}
}
