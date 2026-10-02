// Internal resolution of the shared command handle; expires before borrowing live services.
// Also the shared result guard that reports editor job outcomes (RUNTIME-317). The including
// unit supplies concepts, functional, optional, string, string_view and type_traits.
#pragma once
extern "C++"
{
namespace Extrinsic::Runtime
{
    struct EditorProcessingCommandsAccess
    {
        [[nodiscard]] static const EditorProcessingContext& Resolve(const EditorProcessingCommands&) noexcept;
    };

    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorProcessingConfig(
        const EditorProcessingCommands&, const Core::Config::EngineConfigSectionValidationResult&,
        const std::string& sourceId, const std::function<void(Core::Config::EngineConfig&)>& update);

    namespace EditorJobOutcomeDetail
    {
        template <typename Field>
        [[nodiscard]] std::optional<EditorJobDomain> DomainOf(const Field& backend)
        {
            if constexpr (std::is_convertible_v<const Field&, std::string_view>)
                return EditorJobDomainOfBackend(std::string_view{backend});
            else if constexpr (requires { std::string_view{ToString(backend)}; })
                return EditorJobDomainOfBackend(std::string_view{ToString(backend)});
            else
                return std::nullopt;
        }
    }

    // The outcome a terminal result of any editor operation reports for its job's run, read
    // from the shared result vocabulary. Only a run that produced its result (Applied or
    // NoChange) resolved to a backend: the one it actually ran (`ActualBackend`, a name or an
    // enum with `ToString`; else `BackendId` or `Backend`), with the first non-empty of its
    // fallback/backend diagnostics, else its message, as diagnostic. A failed, cancelled, stale
    // or pending result resolves to nothing (its backend fields are planned or defaults) and
    // its message is the diagnostic.
    template <typename Result>
    [[nodiscard]] EditorJobOutcome EditorJobOutcomeOf(const Result& result)
    {
        namespace D = EditorJobOutcomeDetail;
        EditorJobOutcome outcome{};
        bool ran = true;
        if constexpr (requires { result.Status; })
        {
            using Status = std::remove_cvref_t<decltype(result.Status)>; // EditorCommandStatus
            ran = result.Status == Status::Applied || result.Status == Status::NoChange;
        }
        const auto note = [&outcome](const std::string& text) {
            if (outcome.Diagnostic.empty()) outcome.Diagnostic = text;
        };
        if (ran)
        {
            if constexpr (requires { result.ActualBackend; }) outcome.ResolvedDomain = D::DomainOf(result.ActualBackend);
            if constexpr (requires { result.BackendId; })
                if (!outcome.ResolvedDomain) outcome.ResolvedDomain = D::DomainOf(result.BackendId);
            if constexpr (requires { result.Backend; })
                if (!outcome.ResolvedDomain) outcome.ResolvedDomain = D::DomainOf(result.Backend);
            if constexpr (requires { { result.BackendFallbackReason } -> std::convertible_to<const std::string&>; }) note(result.BackendFallbackReason);
            if constexpr (requires { { result.FallbackReason } -> std::convertible_to<const std::string&>; }) note(result.FallbackReason);
            if constexpr (requires { { result.BackendDiagnostic } -> std::convertible_to<const std::string&>; }) note(result.BackendDiagnostic);
            if constexpr (requires { { result.GpuDiagnostic } -> std::convertible_to<const std::string&>; }) note(result.GpuDiagnostic);
            if constexpr (requires { { result.Diagnostic } -> std::convertible_to<const std::string&>; }) note(result.Diagnostic);
        }
        if constexpr (requires { { result.Message } -> std::convertible_to<const std::string&>; }) note(result.Message);
        return outcome;
    }

    // The one wrapper of an editor operation's result callback: drops results of a detached
    // attachment and, for a result delivered from an editor job's completion callback, reports
    // its outcome for that job's run (after the sink, so a run the sink starts in turn cannot
    // overwrite it). Bound even without a sink while the job surface takes outcomes.
    template <typename Result>
    std::function<void(Result)> GuardEditorProcessingResult(
        const EditorProcessingContext& context, std::function<void(Result)> sink)
    {
        auto report = context.JobCommands.ReportOutcome;
        auto completing = context.JobCommands.CompletingJob;
        if (!sink && !(report && completing)) return {};
        return [active = context.AttachmentActive, sink = std::move(sink), report = std::move(report),
                completing = std::move(completing)](Result result)
        {
            if (active && !active()) return;
            if (!(report && completing))
            {
                sink(std::move(result));
                return;
            }
            const auto job = completing();
            std::optional<EditorJobOutcome> outcome{};
            if (job.IsValid()) outcome = EditorJobOutcomeOf(result);
            if (sink) sink(std::move(result));
            if (outcome) report(job, std::move(*outcome));
        };
    }
}
}
