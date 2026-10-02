// Shared helpers of the editor-backed agent operations (Editor.cpp, Operations.cpp): JSON
// argument access, outcome builders, schema fragments, readiness and asynchronous
// completion. Private to the Extrinsic.Runtime.AgentOperations implementation units: include it
// after the module declaration and the imports, with a global module fragment that provides
// <algorithm>, <cstdint>, <functional>, <memory>, <optional>, <string>, <string_view>, <utility>, <vector>
// and <nlohmann/json.hpp>.
#pragma once
extern "C++"
{
namespace Extrinsic::Runtime::AgentDetail
{
    using Json = nlohmann::json;

    inline std::string Dump(const Json& value) { return value.dump(-1, ' ', false, Json::error_handler_t::replace); }
    inline AgentOperationOutcome Ok(const Json& value) { return {.IsError = false, .Text = Dump(value)}; }
    inline AgentOperationOutcome Fail(std::string message) { return {.IsError = true, .Text = std::move(message)}; }

    inline std::optional<Json> ParseObject(std::string_view text)
    {
        Json value = Json::parse(text, nullptr, false);
        if (value.is_discarded() || !value.is_object()) return std::nullopt;
        return value;
    }
    inline std::optional<std::uint32_t> UInt(const Json& object, const char* key)
    {
        const auto it = object.find(key);
        if (it == object.end() || !it->is_number_integer()) return std::nullopt;
        const auto value = it->get<std::int64_t>();
        if (value < 0 || value > std::int64_t(UINT32_MAX)) return std::nullopt;
        return static_cast<std::uint32_t>(value);
    }
    inline std::optional<std::string> String(const Json& object, const char* key)
    {
        const auto it = object.find(key);
        if (it == object.end() || !it->is_string()) return std::nullopt;
        return it->get<std::string>();
    }
    // A config payload may be passed as a JSON object or as JSON text.
    inline std::optional<std::string> Payload(const Json& object)
    {
        const auto it = object.find("payload");
        if (it == object.end()) return std::nullopt;
        if (it->is_object()) return Dump(*it);
        if (it->is_string()) return it->get<std::string>();
        return std::nullopt;
    }
    inline Json ParsedOrString(const std::string& text)
    {
        Json value = Json::parse(text, nullptr, false);
        return value.is_discarded() ? Json(text) : value;
    }
    inline Json Diagnostics(const std::vector<Core::Config::EngineConfigDiagnostic>& diagnostics)
    {
        Json out = Json::array();
        for (const auto& d : diagnostics)
            out.push_back({{"severity", std::string(Core::Config::ToString(d.Severity))},
                           {"code", std::string(Core::Config::ToString(d.Code))},
                           {"subject", d.Subject}, {"message", d.Message}});
        return out;
    }
    inline const char* KindName(const Geometry::PropertyValueKind kind)
    {
        using K = Geometry::PropertyValueKind;
        switch (kind)
        {
        case K::Bool: return "bool";
        case K::Int32: return "int32";
        case K::UInt32: return "uint32";
        case K::UInt64: return "uint64";
        case K::Float: return "float";
        case K::Double: return "double";
        case K::Vec2: return "vec2";
        case K::Vec3: return "vec3";
        case K::Vec4: return "vec4";
        case K::Unknown: break;
        }
        return "unknown";
    }

    inline std::optional<EditorWorkspaceSnapshotPreparedFrame> PrepareSnapshot(const AgentOperationContext& context)
    {
        if (context.Attachment == nullptr || !context.Attachment->IsAttached()) return std::nullopt;
        return PrepareEditorWorkspaceSnapshotFrame(*context.Attachment);
    }
    inline constexpr const char* kNoWorkspace = "The editor workspace is not attached yet; retry after the next frame.";

    // ---- shared schema fragments and argument parsing ---------------------------------
    // Every tool that names an element domain shares one enum: GeometryElementDomain
    // without Unknown, in declaration order.
    inline constexpr unsigned kFirstDomain = 1u;
    inline constexpr unsigned kLastDomain = unsigned(GeometryElementDomain::PointCloudPoint);
    inline const std::string& DomainNames()
    {
        static const std::string names = [] {
            std::string out;
            for (unsigned i = kFirstDomain; i <= kLastDomain; ++i)
                out += std::string(out.empty() ? "" : ",") + "\"" + std::string(ToString(static_cast<GeometryElementDomain>(i))) + "\"";
            return out;
        }();
        return names;
    }
    inline std::string DomainProperty(const std::string& description)
    {
        return R"("domain":{"type":"string","enum":[)" + DomainNames() + R"(],"description":")" + description + R"("})";
    }
    inline const std::string kPositionsProperty =
        R"("positions":{"type":"string","description":"Name of the vec3 position property."})";
    inline const std::string kPositionsDefaultProperty =
        R"("positions":{"type":"string","default":"v:position","description":"Name of the vec3 position property."})";
    inline std::optional<GeometryElementDomain> ParseDomain(const std::optional<std::string>& name)
    {
        if (!name) return std::nullopt;
        for (unsigned i = kFirstDomain; i <= kLastDomain; ++i)
            if (*name == ToString(static_cast<GeometryElementDomain>(i))) return static_cast<GeometryElementDomain>(i);
        return std::nullopt;
    }
    inline std::string UnknownDomainMessage(const std::string& name)
    {
        return "Unknown domain '" + name + "'; valid: [" + DomainNames() + "].";
    }

    template <class Result>
    inline Json ResultJson(const Result& result)
    {
        return {{"status", DebugNameForEditorCommandStatus(result.Status)}, {"succeeded", result.Succeeded()},
                {"message", result.Message}};
    }
    // A preview answers like the panel's button: the method's readiness behind the same
    // config-lane gate (ResolveEditorProcessingActionReadiness).
    inline Json ReadinessJson(const EditorProcessingCommands& commands, ActionReadiness method, Json extra = Json::object())
    {
        const auto readiness = ResolveEditorProcessingActionReadiness(commands, std::move(method));
        extra["enabled"] = readiness.Enabled;
        extra["reason"] = readiness.DisabledReason;
        return extra;
    }

    // ---- progress of a deferred call -----------------------------------------------------
    // The call's own run, for `notifications/progress`: reads the UI-069 read model through the
    // session's processing commands, never "the oldest job".
    inline std::function<EditorOperationProgress(const AgentOperationContext&)> RunProgressProbe(EditorOperationRunKey key)
    {
        return [key = std::move(key)](const AgentOperationContext& current) -> EditorOperationProgress {
            // The session prepared its frame for the call that started this run; an attachment that
            // was detached since has nothing to report.
            if (current.Attachment == nullptr || !current.Attachment->IsAttached()) return {};
            auto progress = GetEditorOperationProgress(PrepareEditorProcessingCommands(*current.Attachment), key);
            // A job the session does not index (a scene file save or load) is projected from the
            // service directly, through the same pure projection.
            if (progress.State == EditorOperationState::None)
                if (const auto* token = std::get_if<JobToken>(&key); token != nullptr && current.Jobs != nullptr)
                    for (const JobSnapshot& job : current.Jobs->SnapshotAll())
                        if (job.Token == *token)
                            return ProjectEditorOperationProgress(EditorJobRecord{
                                .Token = job.Token, .CorrelationId = job.CorrelationId, .Name = job.DebugName,
                                .State = job.State, .NormalizedProgress = job.Progress.Normalized,
                                .ProgressDeterminate = job.Progress.Determinate,
                                .ElapsedMilliseconds = job.ElapsedMilliseconds});
            return progress;
        };
    }
    // The jobs alive before a command runs; the job it queues is the one not in this set.
    inline std::vector<JobToken> LiveJobTokens(const AgentOperationContext& context)
    {
        std::vector<JobToken> tokens;
        if (context.Jobs)
            for (const auto& job : context.Jobs->SnapshotAll()) tokens.push_back(job.Token);
        return tokens;
    }
    // The jobs queued since `before`, oldest first. A command runs synchronously on the main
    // thread, so every one of them belongs to it (a chained run queues several).
    inline std::vector<JobToken> NewJobTokens(const AgentOperationContext& context, const std::vector<JobToken>& before)
    {
        std::vector<JobToken> fresh;
        if (context.Jobs)
            for (const auto& job : context.Jobs->SnapshotAll())
                if (std::find(before.begin(), before.end(), job.Token) == before.end()) fresh.push_back(job.Token);
        std::sort(fresh.begin(), fresh.end(), [](JobToken a, JobToken b) { return a.Index < b.Index; });
        return fresh;
    }
    // Probe for the newest of them (nothing queued, nothing to watch).
    inline std::function<EditorOperationProgress(const AgentOperationContext&)> ProbeForNewestJob(const std::vector<JobToken>& fresh)
    {
        return fresh.empty() ? std::function<EditorOperationProgress(const AgentOperationContext&)>{} : RunProgressProbe(fresh.back());
    }
    // `notifications/cancelled` for a call that queued `tokens`: cancels them through the editor job
    // surface (RUNTIME-279), which refuses every job the editor did not submit and every token of an
    // earlier attachment. Nothing queued, nothing to cancel.
    inline std::function<void(const AgentOperationContext&)> CancelThroughEditorSurface(std::vector<JobToken> tokens)
    {
        if (tokens.empty()) return {};
        return [tokens = std::move(tokens)](const AgentOperationContext& current) {
            if (current.Attachment == nullptr || !current.Attachment->IsAttached()) return;
            const auto commands = PrepareEditorProcessingCommands(*current.Attachment);
            for (const JobToken token : tokens) (void)CancelEditorJob(commands, token);
        };
    }

    // ---- asynchronous completion -------------------------------------------------------
    // Runs `apply(onComplete)` (an Editor Apply* command whose callback fires for a newly
    // queued job) and answers with its immediate result or, for Pending, a continuation
    // that replies once the callback delivered. A detached workspace ends the wait with
    // an error because the callback never fires for a detached attachment.
    inline constexpr const char* kResultUnavailable =
        "No result will be delivered to this call: an identical job was already running, or the workspace was "
        "re-attached while it ran. Check jobs and the scene.";
    template <class Result, class Apply, class Describe>
    inline AgentOperationOutcome FinishApply(const AgentOperationContext& context, Apply apply, Describe describe)
    {
        auto done = std::make_shared<std::optional<Result>>();
        const auto before = LiveJobTokens(context);
        const auto immediate = apply([done](Result result) { *done = std::move(result); });
        if (immediate.Status != EditorCommandStatus::Pending)
            return {.IsError = !immediate.Succeeded(), .Text = Dump(describe(immediate))};
        // Only a queued job's callback holds `done`; when nobody does (a duplicate Pending
        // registers none, a dropped job releases it) no result can ever arrive.
        const auto orphaned = [](const std::shared_ptr<std::optional<Result>>& state) {
            return !state->has_value() && state.use_count() == 1;
        };
        if (orphaned(done))
            return {.IsError = true, .Text = (immediate.Message.empty() ? std::string{} : immediate.Message + " ") + kResultUnavailable,
                    .ErrorCode = "result_unavailable"};
        AgentOperationOutcome outcome{};
        auto fresh = NewJobTokens(context, before);
        outcome.Progress = ProbeForNewestJob(fresh);
        // A cancelled job's finalizer delivers its terminal result (or releases the callback), so the
        // continuation below still ends and frees the call's tombstone.
        outcome.Cancel = CancelThroughEditorSurface(std::move(fresh));
        outcome.Continuation = [done, describe, orphaned](const AgentOperationContext& current, AgentOperationOutcome& out) {
            if (!current.Attachment || !current.Attachment->IsAttached()) { out = Fail(kNoWorkspace); return true; }
            if (done->has_value())
            {
                out = {.IsError = !(*done)->Succeeded(), .Text = Dump(describe(**done))};
                return true;
            }
            if (orphaned(done)) { out = {.IsError = true, .Text = kResultUnavailable, .ErrorCode = "result_unavailable"}; return true; }
            return false;
        };
        return outcome;
    }
    template <class Result, class Apply>
    inline AgentOperationOutcome FinishApply(const AgentOperationContext& context, Apply apply)
    {
        return FinishApply<Result>(context, std::move(apply), [](const Result& result) { return ResultJson(result); });
    }

    // Same contract for runs reported through a service's completion event (k-means,
    // consolidation): the subscription lives as long as the pending call and is released
    // on completion, detach or when the call is dropped.
    template <class Result, class Service>
    struct ServiceRun
    {
        CommandCorrelationId Correlation{};
        std::optional<Result> Completed{};
        Service* Owner{};
        KernelEventSubscription Subscription{};
        void Release()
        {
            if (Owner && Subscription.IsValid()) Owner->Unsubscribe(Subscription);
            Subscription = {};
        }
        ~ServiceRun() { Release(); }
    };
    struct ServiceSubmission
    {
        CommandCorrelationId Correlation{};
        bool Queued{false};
        std::string Message{};
    };
    template <class Result, class Service, class Subscribe, class Submit, class Describe>
    inline AgentOperationOutcome AwaitServiceRun(Service* service, Subscribe subscribe, Submit submit, Describe describe)
    {
        auto run = std::make_shared<ServiceRun<Result, Service>>();
        run->Owner = service;
        run->Subscription = subscribe(*service, [weak = std::weak_ptr<ServiceRun<Result, Service>>(run)](const Result& result) {
            if (auto state = weak.lock(); state && state->Correlation == result.Correlation) state->Completed = result;
        });
        const ServiceSubmission submitted = submit();
        run->Correlation = submitted.Correlation;
        if (!submitted.Queued) { run->Release(); return Fail(submitted.Message); }
        AgentOperationOutcome outcome{};
        // The service stamped the submission's correlation id on its job(s).
        outcome.Progress = RunProgressProbe(EditorRunCorrelation{submitted.Correlation.Value});
        outcome.Continuation = [run, describe](const AgentOperationContext& current, AgentOperationOutcome& out) {
            if (!current.Attachment || !current.Attachment->IsAttached()) { run->Release(); out = Fail(kNoWorkspace); return true; }
            if (!run->Completed) return false;
            run->Release();
            out = {.IsError = !run->Completed->Succeeded(), .Text = Dump(describe(*run->Completed))};
            return true;
        };
        return outcome;
    }

    inline std::string Schema(std::string properties, std::string required = "[]")
    {
        return R"({"type":"object","properties":)" + std::move(properties) + R"(,"required":)" +
               std::move(required) + R"(,"additionalProperties":false})";
    }
    inline const std::string kEntityProperty = R"("entity":{"type":"integer","minimum":1,"description":"Stable entity id from scene_entities."})";
    inline const std::string kSectionProperty = R"("section":{"type":"string","description":"Config section name from config_sections, e.g. sandbox.property_smoothing."})";
    inline const std::string kPayloadProperty = R"("payload":{"type":["object","string"],"description":"Section payload; omitted fields keep their defaults."})";

    inline const std::string kNone = Schema("{}");
    // Registers one tool; `gpu` marks tools that may dispatch GPU work (NeedsPresentedFrame).
    inline void AddOperation(AgentOperationRegistry& registry, const char* name, const char* title,
                             std::string description, std::string schema, bool readOnly, AgentOperationInvoker invoke,
                             bool destructive = false, bool gpu = false)
    {
        (void)registry.Register({.Name = name, .Title = title, .Description = std::move(description),
                                 .InputSchemaJson = std::move(schema), .ReadOnly = readOnly, .Destructive = destructive,
                                 .NeedsPresentedFrame = gpu, .Invoke = std::move(invoke)});
    }
}

namespace Extrinsic::Runtime
{
    // Registers the point, registration and configured-operation tools (Operations.cpp).
    void RegisterProcessingAgentOperations(AgentOperationRegistry& registry);
}
}
