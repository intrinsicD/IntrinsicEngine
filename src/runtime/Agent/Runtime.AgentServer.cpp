module;

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

module Extrinsic.Runtime.AgentServer;

import Extrinsic.Core.Logging;
import Extrinsic.Platform.LocalSocket;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.ViewCapture;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        std::string Dump(const Json& value) { return value.dump(-1, ' ', false, Json::error_handler_t::replace); }

        Json ErrorResponse(const Json& id, int code, std::string message)
        {
            return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", std::move(message)}}}};
        }
        Json ResultResponse(const Json& id, Json result)
        {
            return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
        }
        // ISO dates compare chronologically as strings.
        bool HasStructuredContent(std::string_view negotiatedVersion) { return negotiatedVersion >= std::string_view{"2025-06-18"}; }
        Json ToolResultResponse(const Json& id, const AgentOperationOutcome& outcome, std::string_view negotiatedVersion)
        {
            Json content = Json::array();
            content.push_back({{"type", "text"}, {"text", outcome.Text}});
            for (const auto& image : outcome.Images)
                content.push_back({{"type", "image"}, {"data", image.Base64Data}, {"mimeType", image.MimeType}});
            Json result{{"content", content}, {"isError", outcome.IsError}};
            if (HasStructuredContent(negotiatedVersion))
            {
                if (outcome.IsError && !outcome.ErrorCode.empty())
                    result["structuredContent"] = {{"error", {{"code", outcome.ErrorCode}, {"message", outcome.Text}}}};
                else if (Json parsed = Json::parse(outcome.Text, nullptr, false); parsed.is_object())
                    result["structuredContent"] = std::move(parsed);
            }
            return ResultResponse(id, std::move(result));
        }
        Json ParsedSchema(const std::string& text)
        {
            Json schema = Json::parse(text, nullptr, false);
            return schema.is_discarded() || !schema.is_object() ? Json{{"type", "object"}} : schema;
        }
        // The id of a message that may be malformed JSON-RPC, for error replies.
        Json RequestId(std::string_view message)
        {
            const Json value = Json::parse(message, nullptr, false);
            if (value.is_object())
                if (const auto it = value.find("id"); it != value.end() && (it->is_string() || it->is_number_integer()))
                    return *it;
            return nullptr;
        }
    }

    std::string DefaultAgentSocketPath()
    {
        if (const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR"); runtimeDir != nullptr && *runtimeDir != '\0')
            return (std::filesystem::path(runtimeDir) / "intrinsic-sandbox.sock").string();
#if defined(__unix__) || defined(__APPLE__)
        return "/tmp/intrinsic-sandbox-" + std::to_string(::getuid()) + ".sock";
#else
        return "intrinsic-sandbox.sock";
#endif
    }

    std::optional<std::string> AgentProtocol::Handle(std::string_view message, const AgentOperationContext& context)
    {
        const Json request = Json::parse(message, nullptr, false);
        if (request.is_discarded()) return Dump(ErrorResponse(nullptr, -32700, "Parse error"));
        if (!request.is_object()) return Dump(ErrorResponse(nullptr, -32600, "Invalid request: expected one JSON-RPC object"));
        const auto methodIt = request.find("method");
        const auto idIt = request.find("id");
        const bool isNotification = idIt == request.end();
        const Json id = isNotification ? Json(nullptr) : *idIt;
        if (methodIt == request.end() || !methodIt->is_string())
            return isNotification ? std::nullopt : std::optional{Dump(ErrorResponse(id, -32600, "Invalid request: missing method"))};
        const std::string method = methodIt->get<std::string>();
        const auto paramsIt = request.find("params");
        const Json params = paramsIt != request.end() && paramsIt->is_object() ? *paramsIt : Json::object();

        if (isNotification && method == "notifications/cancelled")
        {
            if (const auto requestId = params.find("requestId"); requestId != params.end())
            {
                const std::string wanted = Dump(*requestId);
                const auto it = std::find_if(m_Pending.begin(), m_Pending.end(), [&](const PendingCall& call) { return call.Id == wanted; });
                if (it != m_Pending.end() && !it->Cancelled)
                {
                    // Tombstone: no reply or progress, but the slot stays taken until the continuation ends,
                    // which a cancelled job's terminal delivery (or a service run's completion) brings about.
                    it->Cancelled = true;
                    it->ProgressToken.clear();
                    const bool cancelsJob = static_cast<bool>(it->Cancel);
                    if (cancelsJob) std::exchange(it->Cancel, {})(context);
                    Core::Log::Info("[AgentServer] request {} cancelled{}", wanted,
                                    cancelsJob ? "; its editor jobs are cancelled" : "; it queued no editor job to cancel");
                }
            }
            return std::nullopt;
        }
        if (isNotification) return std::nullopt; // notifications/initialized, ...
        if (method == "initialize")
        {
            std::string version{kAgentProtocolVersion};
            if (const auto v = params.find("protocolVersion"); v != params.end() && v->is_string())
                for (const auto supported : kAgentSupportedProtocolVersions)
                    if (v->get<std::string>() == supported) version = supported;
            m_NegotiatedVersion = version;
            if (const auto info = params.find("clientInfo"); info != params.end() && info->is_object())
                if (const auto name = info->find("name"); name != info->end() && name->is_string()) m_ClientName = name->get<std::string>();
            m_Initialized = true;
            return Dump(ResultResponse(id, {
                {"protocolVersion", version},
                {"capabilities", {{"tools", {{"listChanged", false}}}}},
                {"serverInfo", {{"name", "intrinsic-sandbox"}, {"title", "IntrinsicEngine Sandbox"}, {"version", "0.1.0"}}},
                {"instructions",
                 "Drives the running IntrinsicEngine Sandbox through the same validated operations as its UI. "
                 "Typical flow: scene_entities -> entity_properties -> config_get/config_apply for a section -> "
                 "preview_mesh_operation -> run_mesh_operation -> undo if needed. Changes appear in the editor's "
                 "undo history as 'Agent: ...'."}}));
        }
        if (method == "ping") return Dump(ResultResponse(id, Json::object()));
        if (method == "tools/list")
        {
            Json tools = Json::array();
            for (const auto& spec : m_Registry->Entries())
            {
                if (m_ReadOnly && !spec.ReadOnly) continue;
                tools.push_back({{"name", spec.Name}, {"title", spec.Title}, {"description", spec.Description},
                                 {"inputSchema", ParsedSchema(spec.InputSchemaJson)},
                                 {"annotations", {{"title", spec.Title}, {"readOnlyHint", spec.ReadOnly},
                                                  {"destructiveHint", spec.Destructive}, {"idempotentHint", spec.ReadOnly},
                                                  {"openWorldHint", false}}}});
            }
            return Dump(ResultResponse(id, {{"tools", tools}}));
        }
        if (method == "tools/call")
        {
            const auto nameIt = params.find("name");
            if (nameIt == params.end() || !nameIt->is_string()) return Dump(ErrorResponse(id, -32602, "tools/call needs a tool name"));
            const auto name = nameIt->get<std::string>();
            const AgentOperationSpec* spec = m_Registry->Find(name);
            if (spec == nullptr) return Dump(ErrorResponse(id, -32602, "Unknown tool: " + name));
            const bool atCap = m_Pending.size() >= kMaxPendingCalls;
            // Mutating tools and read-only tools that always defer to a presented frame (captures
            // that move the camera) are refused before they run; any other read-only tool that
            // defers at the cap is refused after it ran, which is harmless because it changed nothing.
            if (atCap && (!spec->ReadOnly || spec->NeedsPresentedFrame))
                return Dump(ErrorResponse(id, -32000, "too many pending calls"));
            const auto argsIt = params.find("arguments");
            const std::string arguments = argsIt != params.end() && argsIt->is_object() ? Dump(*argsIt) : "{}";
            auto outcome = InvokeAgentOperation(*m_Registry, name, context, arguments, m_ReadOnly);
            if (outcome.Continuation)
            {
                // A read-only tool without side effects that defers at the cap is refused after it ran.
                if (atCap) return Dump(ErrorResponse(id, -32000, "too many pending calls"));
                PendingCall call{.Id = Dump(id), .Continue = std::move(outcome.Continuation), .Progress = std::move(outcome.Progress),
                                 .Cancel = std::move(outcome.Cancel)};
                call.NeedsPresentedFrame = spec->NeedsPresentedFrame;
                if (const auto meta = params.find("_meta"); meta != params.end() && meta->is_object())
                    if (const auto token = meta->find("progressToken"); token != meta->end() && (token->is_string() || token->is_number_integer()))
                        call.ProgressToken = Dump(*token);
                call.Started = call.LastEmit = std::chrono::steady_clock::now();
                m_Pending.push_back(std::move(call));
                return std::nullopt;
            }
            return Dump(ToolResultResponse(id, outcome, m_NegotiatedVersion));
        }
        return Dump(ErrorResponse(id, -32601, "Method not found: " + method));
    }

    std::vector<std::string> AgentProtocol::PollPending(const AgentOperationContext& context)
    {
        std::vector<std::string> replies;
        const auto now = std::chrono::steady_clock::now();
        for (auto it = m_Pending.begin(); it != m_Pending.end();)
        {
            AgentOperationOutcome outcome{};
            if (it->NeedsPresentedFrame && !context.ViewportPresentable)
            {
                // GPU work only progresses on presented frames; the editor job itself keeps its state
                // and publishes after the window is restored, without a reply.
                outcome = {.IsError = true,
                           .Text = "The Sandbox window was minimized while this call was waiting for GPU work; it may still "
                                   "finish after the window is restored. Check the scene or jobs before retrying.",
                           .ErrorCode = "viewport_not_presentable"};
                if (!it->Cancelled)
                    replies.push_back(Dump(ToolResultResponse(Json::parse(it->Id, nullptr, false), outcome, m_NegotiatedVersion)));
                it = m_Pending.erase(it);
                continue;
            }
            if (it->Continue(context, outcome))
            {
                if (!it->Cancelled)
                    replies.push_back(Dump(ToolResultResponse(Json::parse(it->Id, nullptr, false), outcome, m_NegotiatedVersion)));
                it = m_Pending.erase(it);
                continue;
            }
            if (!it->ProgressToken.empty() && now - it->LastEmit >= m_ProgressInterval)
            {
                it->LastEmit = now;
                // The call's own run, by the key its operation captured; none means "waiting".
                EditorOperationProgress run{};
                if (it->Progress) run = it->Progress(context);
                const bool known = run.State != EditorOperationState::None;
                // One unit per call, chosen at its first notification, so the value only ever grows.
                const bool determinate = known && run.Determinate;
                if (it->Unit == ProgressUnit::Unset) it->Unit = determinate ? ProgressUnit::Percent : ProgressUnit::Seconds;
                double progress = 0.0;
                if (it->Unit == ProgressUnit::Percent)
                {
                    if (!determinate) { ++it; continue; } // the run is not reporting; wait for a determinate value
                    progress = std::min(static_cast<double>(run.Normalized) * 100.0, 100.0);
                }
                else
                    progress = known ? run.ElapsedSeconds : std::chrono::duration<double>(now - it->Started).count();
                if (progress <= it->LastProgress) { ++it; continue; }
                it->LastProgress = progress;
                Json params{{"progressToken", Json::parse(it->ProgressToken, nullptr, false)}, {"progress", progress}};
                if (it->Unit == ProgressUnit::Percent) params["total"] = 100.0;
                params["message"] = known && !run.Label.empty() ? run.Label : std::string{"waiting"};
                replies.push_back(Dump(Json{{"jsonrpc", "2.0"}, {"method", "notifications/progress"}, {"params", std::move(params)}}));
            }
            ++it;
        }
        return replies;
    }

    struct AgentServerModule::Impl
    {
        AgentServerOptions Options;
        AgentOperationRegistry Registry{};
        std::unique_ptr<AgentProtocol> Protocol{};
        EditorWorkspaceAttachment Attachment{};
        bool AttachAttempted{false};

        mutable std::mutex Mutex{};
        std::deque<std::pair<std::uint64_t, std::string>> Inbound{}; // (connection generation, message)
        std::deque<std::pair<std::uint64_t, std::string>> Outbound{};
        AgentServerStatus Status{};
        std::uint64_t Generation{0};
        std::uint64_t PendingGeneration{0};
        bool DisconnectRequested{false};

        std::atomic_bool Stop{false};
        std::thread Worker{};
        Platform::LocalSocketListener Listener{};

        void Run()
        {
            Platform::LocalSocketConnection client;
            std::string buffer;
            std::uint64_t generation = 0;
            const auto dropClient = [&] {
                client.Close();
                buffer.clear();
                std::scoped_lock lock{Mutex};
                Status.ClientConnected = false;
                Status.ClientName.clear();
                ++Generation;
                Inbound.clear();
                Outbound.clear();
            };
            while (!Stop.load())
            {
                if (!client.IsOpen())
                {
                    if (Listener.Accept(client, 200) != Platform::LocalSocketStatus::Ok) continue;
                    std::scoped_lock lock{Mutex};
                    generation = ++Generation;
                    Status.ClientConnected = true;
                    DisconnectRequested = false;
                    continue;
                }
                {
                    std::unique_lock lock{Mutex};
                    if (DisconnectRequested) { DisconnectRequested = false; lock.unlock(); dropClient(); continue; }
                    std::vector<std::string> pending;
                    for (auto it = Outbound.begin(); it != Outbound.end();)
                        if (it->first == generation) { pending.push_back(std::move(it->second)); it = Outbound.erase(it); }
                        else ++it;
                    lock.unlock();
                    bool failed = false;
                    for (auto& line : pending)
                        failed |= client.SendAll(line + "\n") != Platform::LocalSocketStatus::Ok;
                    if (failed) { dropClient(); continue; }
                }
                const auto received = client.Receive(buffer, 20);
                if (received == Platform::LocalSocketStatus::Closed || received == Platform::LocalSocketStatus::SystemError)
                { dropClient(); continue; }
                for (std::size_t newline; (newline = buffer.find('\n')) != std::string::npos;)
                {
                    std::string line = buffer.substr(0, newline);
                    buffer.erase(0, newline + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty()) continue;
                    std::scoped_lock lock{Mutex};
                    if (Inbound.size() >= 64)
                        Outbound.emplace_back(generation, Dump(ErrorResponse(RequestId(line), -32000, "Sandbox busy; retry")));
                    else
                        Inbound.emplace_back(generation, std::move(line));
                }
                if (buffer.size() > Options.MaxMessageBytes)
                {
                    Core::Log::Warn("[AgentServer] dropping client: message exceeds {} bytes", Options.MaxMessageBytes);
                    dropClient();
                }
            }
            client.Close();
        }

        // `presentable` is false on minimized (Idle) frames.
        void Drain(RuntimeFrameHookContext& frame, const bool presentable)
        {
            if (!AttachAttempted)
            {
                AttachAttempted = true;
                Attachment.Attach(frame.Worlds, frame.Services);
            }
            AgentOperationContext context{
                .Attachment = &Attachment,
                .ConfigControl = frame.Services.Find<EngineConfigControl>(),
                .Jobs = &frame.Jobs,
                .History = frame.Services.Find<EditorCommandHistory>(),
                .ViewCapture = frame.Services.Find<ViewCaptureModule>(),
                .AllowedRoots = Options.AllowedRoots,
                .FrameIndex = frame.FrameIndex,
                .ViewportPresentable = presentable,
            };
            {
                // Deferred replies belong to the client that asked; a reconnect drops them.
                std::scoped_lock lock{Mutex};
                if (PendingGeneration != Generation) Protocol->DropPending();
                PendingGeneration = Generation;
            }
            for (auto& reply : Protocol->PollPending(context))
            {
                std::scoped_lock lock{Mutex};
                Outbound.emplace_back(PendingGeneration, std::move(reply));
            }
            for (std::uint32_t call = 0; call < Options.MaxCallsPerFrame; ++call)
            {
                std::pair<std::uint64_t, std::string> message;
                {
                    std::scoped_lock lock{Mutex};
                    if (Inbound.empty()) break;
                    message = std::move(Inbound.front());
                    Inbound.pop_front();
                }
                auto response = Protocol->Handle(message.second, context);
                std::scoped_lock lock{Mutex};
                ++Status.CallsHandled;
                Status.ClientName = Protocol->ClientName();
                if (response && message.first == Generation) Outbound.emplace_back(message.first, std::move(*response));
            }
        }

        void Shutdown()
        {
            Stop.store(true);
            if (Worker.joinable()) Worker.join();
            Listener.Close();
            if (Protocol) Protocol->DropPending();
            Attachment.Detach();
            std::scoped_lock lock{Mutex};
            Status.Listening = false;
            Status.ClientConnected = false;
        }
    };

    AgentServerModule::AgentServerModule(AgentServerOptions options) : m_Impl(std::make_unique<Impl>())
    {
        if (options.SocketPath.empty()) options.SocketPath = DefaultAgentSocketPath();
        if (options.AllowedRoots.empty()) options.AllowedRoots.push_back(std::filesystem::current_path().string());
        for (auto& root : options.AllowedRoots)
        {
            std::error_code error;
            const auto canonical = std::filesystem::weakly_canonical(root, error);
            if (!error) root = canonical.string();
        }
        m_Impl->Options = std::move(options);
        RegisterEditorAgentOperations(m_Impl->Registry);
        RegisterViewCaptureAgentOperations(m_Impl->Registry);
        m_Impl->Protocol = std::make_unique<AgentProtocol>(m_Impl->Registry, m_Impl->Options.ReadOnly);
        m_Impl->Protocol->SetProgressInterval(m_Impl->Options.ProgressInterval);
        m_Impl->Status.ReadOnly = m_Impl->Options.ReadOnly;
        m_Impl->Status.SocketPath = m_Impl->Options.SocketPath;
        m_Impl->Status.AllowedRoots = m_Impl->Options.AllowedRoots;
    }

    AgentServerModule::~AgentServerModule()
    {
        if (m_Impl) m_Impl->Shutdown();
    }

    RuntimeModuleResult AgentServerModule::OnRegister(EngineSetup& setup)
    {
        // Published so the editor can show the connection and offer Disconnect.
        if (auto provided = setup.Services().Provide<AgentServerModule>(*this, Name()); !provided) return provided;
        const auto status = m_Impl->Listener.Listen(m_Impl->Options.SocketPath);
        if (status != Platform::LocalSocketStatus::Ok)
        {
            // The engine keeps running without the agent lane; the status explains why.
            std::scoped_lock lock{m_Impl->Mutex};
            m_Impl->Status.LastError = std::string("Cannot listen on ") + m_Impl->Options.SocketPath + ": " +
                                       Platform::ToString(status);
            Core::Log::Error("[AgentServer] {}", m_Impl->Status.LastError);
        }
        else
        {
            {
                std::scoped_lock lock{m_Impl->Mutex};
                m_Impl->Status.Listening = true;
            }
            Core::Log::Info("[AgentServer] listening on {}{}", m_Impl->Options.SocketPath,
                            m_Impl->Options.ReadOnly ? " (read-only)" : "");
            m_Impl->Worker = std::thread([impl = m_Impl.get()] { impl->Run(); });
        }
        // Idle serves calls while the window is minimized and no UiBuild frame runs.
        if (auto idle = setup.RegisterFrameHook(
                FramePhase::Idle, [impl = m_Impl.get()](RuntimeFrameHookContext& frame) { impl->Drain(frame, false); });
            !idle)
            return idle;
        return setup.RegisterFrameHook(
            FramePhase::UiBuild, [impl = m_Impl.get()](RuntimeFrameHookContext& frame) { impl->Drain(frame, true); });
    }

    void AgentServerModule::OnShutdown(RuntimeModuleShutdownContext&) { m_Impl->Shutdown(); }

    AgentServerStatus AgentServerModule::Status() const
    {
        std::scoped_lock lock{m_Impl->Mutex};
        return m_Impl->Status;
    }

    void AgentServerModule::DisconnectClient()
    {
        std::scoped_lock lock{m_Impl->Mutex};
        m_Impl->DisconnectRequested = true;
    }

    const AgentOperationRegistry& AgentServerModule::Operations() const noexcept { return m_Impl->Registry; }
}
