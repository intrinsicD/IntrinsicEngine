// RUNTIME-287/288: agent operation registry, MCP protocol core, read-only policy, path
// containment and the "Agent:" history label, without sockets or an engine.
#include <atomic>
#include <algorithm>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
import Extrinsic.Runtime.AgentServer;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.ScalarRidgeOperations;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Core.Tasks;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
namespace R = Extrinsic::Runtime;
using Json = nlohmann::json;
namespace
{
    R::AgentOperationRegistry TestRegistry(int& mutations)
    {
        R::AgentOperationRegistry registry;
        EXPECT_TRUE(registry.Register({.Name = "echo", .Title = "Echo", .Description = "Returns its arguments.",
            .ReadOnly = true, .Invoke = [](const R::AgentOperationContext&, std::string_view args) {
                return R::AgentOperationOutcome{.Text = std::string(args)}; }}));
        EXPECT_TRUE(registry.Register({.Name = "mutate", .Title = "Mutate", .ReadOnly = false,
            .Invoke = [&mutations](const R::AgentOperationContext& context, std::string_view) {
                ++mutations;
                if (context.History)
                    (void)context.History->Execute({.Label = "Change", .Redo = [] { return R::EditorCommandHistoryStatus::Applied; },
                                                    .Undo = [] { return R::EditorCommandHistoryStatus::Applied; }});
                return R::AgentOperationOutcome{.Text = "{}"}; }}));
        return registry;
    }
    Json Call(R::AgentProtocol& protocol, const R::AgentOperationContext& context, const Json& request)
    {
        const auto response = protocol.Handle(request.dump(), context);
        EXPECT_TRUE(response.has_value());
        return response ? Json::parse(*response) : Json{};
    }
}

TEST(AgentOperations, RegistryRejectsDuplicatesAndMissingInvokers)
{
    int mutations = 0;
    auto registry = TestRegistry(mutations);
    EXPECT_FALSE(registry.Register({.Name = "echo", .Invoke = [](const R::AgentOperationContext&, std::string_view) { return R::AgentOperationOutcome{}; }}));
    EXPECT_FALSE(registry.Register({.Name = "", .Invoke = [](const R::AgentOperationContext&, std::string_view) { return R::AgentOperationOutcome{}; }}));
    EXPECT_FALSE(registry.Register({.Name = "noop"}));
    EXPECT_EQ(registry.Entries().size(), 2u);
    ASSERT_NE(registry.Find("mutate"), nullptr);
    EXPECT_FALSE(registry.Find("mutate")->ReadOnly);
}

TEST(AgentOperations, EditorOperationsHaveUniqueNamesAndValidSchemas)
{
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    EXPECT_GE(registry.Entries().size(), 15u);
    ASSERT_NE(registry.Find("run_keypoint_analysis"),nullptr);
    EXPECT_FALSE(registry.Find("run_keypoint_analysis")->ReadOnly);
    for (const auto& spec : registry.Entries())
    {
        SCOPED_TRACE(spec.Name);
        const auto schema = Json::parse(spec.InputSchemaJson, nullptr, false);
        ASSERT_FALSE(schema.is_discarded());
        EXPECT_EQ(schema.value("type", ""), "object");
        EXPECT_FALSE(spec.Description.empty());
        const bool reader = spec.Name.starts_with("scene_") || spec.Name.starts_with("entity_") || spec.Name == "history" ||
                            spec.Name.starts_with("config_sections") || spec.Name == "config_schema" || spec.Name == "config_get" || spec.Name == "config_preview" ||
                            spec.Name == "jobs_list" || spec.Name == "jobs_wait" || spec.Name == "log" || spec.Name.starts_with("preview_") ||
                            spec.Name == "attribute_bindings";
        EXPECT_EQ(spec.ReadOnly, reader) << "read-only flag follows the naming convention";
    }
    // Without an engine every operation answers without crashing: history and log report
    // what they can, everything that needs the workspace or services fails cleanly.
    const R::AgentOperationContext empty{};
    for (const auto& spec : registry.Entries())
    {
        const auto outcome = R::InvokeAgentOperation(registry, spec.Name, empty, "{}", false);
        EXPECT_EQ(outcome.IsError, spec.Name != "history" && spec.Name != "log") << spec.Name << ": " << outcome.Text;
    }
}

TEST(AgentOperations, ProtocolHandshakeListingAndCalls)
{
    int mutations = 0;
    auto registry = TestRegistry(mutations);
    R::AgentProtocol protocol{registry, false};
    const R::AgentOperationContext context{};
    auto init = Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
        {"params", {{"protocolVersion", "2025-06-18"}, {"clientInfo", {{"name", "test-client"}}}, {"capabilities", Json::object()}}}});
    EXPECT_EQ(init["id"], 1);
    EXPECT_EQ(init["result"]["protocolVersion"], "2025-06-18");
    EXPECT_TRUE(init["result"]["capabilities"].contains("tools"));
    EXPECT_EQ(init["result"]["serverInfo"]["name"], "intrinsic-sandbox");
    EXPECT_TRUE(protocol.Initialized());
    EXPECT_EQ(protocol.ClientName(), "test-client");
    EXPECT_FALSE(protocol.Handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", context).has_value());
    EXPECT_EQ(Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", "p"}, {"method", "ping"}})["result"], Json::object());

    auto list = Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}});
    ASSERT_EQ(list["result"]["tools"].size(), 2u);
    EXPECT_EQ(list["result"]["tools"][0]["name"], "echo");
    EXPECT_TRUE(list["result"]["tools"][0]["annotations"]["readOnlyHint"].get<bool>());
    EXPECT_EQ(list["result"]["tools"][0]["inputSchema"]["type"], "object");

    auto echo = Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"},
        {"params", {{"name", "echo"}, {"arguments", {{"x", 5}}}}}});
    EXPECT_FALSE(echo["result"]["isError"].get<bool>());
    EXPECT_EQ(Json::parse(echo["result"]["content"][0]["text"].get<std::string>())["x"], 5);

    EXPECT_EQ(Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 4}, {"method", "tools/call"},
        {"params", {{"name", "missing"}}}})["error"]["code"], -32602);
    EXPECT_EQ(Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 5}, {"method", "resources/list"}})["error"]["code"], -32601);
    EXPECT_EQ(Json::parse(*protocol.Handle("{not json", context))["error"]["code"], -32700);
    EXPECT_EQ(Json::parse(*protocol.Handle("[1,2]", context))["error"]["code"], -32600);
}

TEST(AgentOperations, ReadOnlySessionsHideAndRefuseMutatingTools)
{
    int mutations = 0;
    auto registry = TestRegistry(mutations);
    R::AgentProtocol protocol{registry, true};
    const R::AgentOperationContext context{};
    auto list = Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
    ASSERT_EQ(list["result"]["tools"].size(), 1u);
    EXPECT_EQ(list["result"]["tools"][0]["name"], "echo");
    auto call = Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/call"}, {"params", {{"name", "mutate"}}}});
    EXPECT_TRUE(call["result"]["isError"].get<bool>());
    EXPECT_EQ(mutations, 0);
}

TEST(AgentOperations, HistoryEntriesAreLabeledAsAgentChanges)
{
    int mutations = 0;
    auto registry = TestRegistry(mutations);
    R::EditorCommandHistory history;
    const R::AgentOperationContext context{.History = &history};
    ASSERT_FALSE(R::InvokeAgentOperation(registry, "mutate", context, "{}", false).IsError);
    EXPECT_EQ(mutations, 1);
    EXPECT_EQ(history.Snapshot().UndoLabel, "Agent: Change");
    EXPECT_EQ(history.LabelPrefix(), "") << "the prefix is scoped to the call";
    (void)history.Execute({.Label = "Panel edit", .Redo = [] { return R::EditorCommandHistoryStatus::Applied; },
                           .Undo = [] { return R::EditorCommandHistoryStatus::Applied; }});
    EXPECT_EQ(history.Snapshot().UndoLabel, "Panel edit");
}

TEST(AgentOperations, PathsStayInsideAllowedRoots)
{
    namespace fs = std::filesystem;
    const auto root = fs::weakly_canonical(fs::temp_directory_path() / "intrinsic-agent-root");
    fs::create_directories(root / "models");
    R::AgentOperationContext context{.AllowedRoots = {root.string()}};
    EXPECT_EQ(R::ResolveAgentPath(context, "models/a.obj"), (root / "models" / "a.obj").string());
    EXPECT_EQ(R::ResolveAgentPath(context, (root / "b.ply").string()), (root / "b.ply").string());
    EXPECT_FALSE(R::ResolveAgentPath(context, "../outside.obj"));
    EXPECT_FALSE(R::ResolveAgentPath(context, "models/../../outside.obj"));
    EXPECT_FALSE(R::ResolveAgentPath(context, "/etc/passwd"));
    EXPECT_FALSE(R::ResolveAgentPath(context, root.string() + "-sibling/x.obj")) << "prefix siblings are outside";
    EXPECT_FALSE(R::ResolveAgentPath(context, ""));
    EXPECT_FALSE(R::ResolveAgentPath(R::AgentOperationContext{}, "a.obj")) << "no roots, no files";
}

TEST(AgentOperations, DeferredCallsReplyWhenTheirContinuationFinishes)
{
    R::AgentOperationRegistry registry;
    int polls = 0;
    ASSERT_TRUE(registry.Register({.Name = "slow", .Title = "Slow", .ReadOnly = true,
        .Invoke = [&polls](const R::AgentOperationContext&, std::string_view) {
            R::AgentOperationOutcome outcome{};
            outcome.Continuation = [&polls](const R::AgentOperationContext&, R::AgentOperationOutcome& out) {
                if (++polls < 3) return false;
                out = R::AgentOperationOutcome{.Text = R"({"done":true})", .Images = {{.Base64Data = "AAAA"}}};
                return true;
            };
            return outcome; }}));
    R::AgentProtocol protocol{registry, false};
    const R::AgentOperationContext context{};
    const Json request{{"jsonrpc", "2.0"}, {"id", "call-7"}, {"method", "tools/call"}, {"params", {{"name", "slow"}}}};
    EXPECT_FALSE(protocol.Handle(request.dump(), context).has_value()) << "the reply waits for the continuation";
    EXPECT_EQ(protocol.PendingCount(), 1u);
    EXPECT_TRUE(protocol.PollPending(context).empty());
    EXPECT_TRUE(protocol.PollPending(context).empty());
    const auto replies = protocol.PollPending(context);
    ASSERT_EQ(replies.size(), 1u);
    const Json reply = Json::parse(replies.front());
    EXPECT_EQ(reply["id"], "call-7");
    EXPECT_EQ(reply["result"]["isError"], false);
    EXPECT_EQ(reply["result"]["content"][0]["text"], R"({"done":true})");
    EXPECT_EQ(reply["result"]["content"][1]["type"], "image");
    EXPECT_EQ(protocol.PendingCount(), 0u);

    EXPECT_FALSE(protocol.Handle(request.dump(), context).has_value());
    protocol.DropPending(); // a reconnecting client never sees the old reply
    EXPECT_TRUE(protocol.PollPending(context).empty());
}

TEST(AgentOperations, CaptureToolsFailClearlyWithoutTheCaptureServiceOrOutsideTheRoots)
{
    R::AgentOperationRegistry registry;
    R::RegisterViewCaptureAgentOperations(registry);
    R::AgentProtocol protocol{registry, true};
    const auto root = std::filesystem::temp_directory_path().string();
    const R::AgentOperationContext context{.AllowedRoots = {root}};
    const auto tools = Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
    ASSERT_EQ(tools["result"]["tools"].size(), 1u) << "read-only sessions only get view_screenshot";
    EXPECT_EQ(tools["result"]["tools"][0]["name"], "view_screenshot");

    const auto shot = Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/call"},
                                               {"params", {{"name", "view_screenshot"}}}});
    EXPECT_EQ(shot["result"]["isError"], true);
    const auto badRegion = Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"},
                                                    {"params", {{"name", "view_screenshot"}, {"arguments", {{"region", "desk"}}}}}});
    EXPECT_EQ(badRegion["result"]["isError"], true);

    R::AgentProtocol writer{registry, false};
    const auto outside = Call(writer, context, {{"jsonrpc", "2.0"}, {"id", 4}, {"method", "tools/call"},
                                                {"params", {{"name", "view_capture"}, {"arguments", {{"path", "/etc/shot.png"}}}}}});
    EXPECT_EQ(outside["result"]["isError"], true);
    EXPECT_NE(outside["result"]["content"][0]["text"].get<std::string>().find("allowed roots"), std::string::npos);
    const auto badPreset = Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 5}, {"method", "tools/call"},
                                                    {"params", {{"name", "view_screenshot"}, {"arguments", {{"preset", "sideways"}}}}}});
    EXPECT_EQ(badPreset["result"]["isError"], true);
    const auto fitWithoutPreset = Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 6}, {"method", "tools/call"},
        {"params", {{"name", "view_screenshot"}, {"arguments", {{"fit_entity", 3}}}}}});
    EXPECT_NE(fitWithoutPreset["result"]["content"][0]["text"].get<std::string>().find("needs a preset"), std::string::npos);
}

TEST(AgentOperations, CaptureFailsFastWhenViewportNotPresentable)
{
    R::AgentOperationRegistry registry;
    R::RegisterViewCaptureAgentOperations(registry);
    R::AgentProtocol protocol{registry, false};
    const R::AgentOperationContext minimized{.AllowedRoots = {std::filesystem::temp_directory_path().string()},
                                             .ViewportPresentable = false};
    for (const char* tool : {"view_screenshot", "view_capture"})
    {
        const auto reply = Call(protocol, minimized, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                                                      {"params", {{"name", tool}}}});
        EXPECT_TRUE(reply["result"]["isError"].get<bool>()) << tool;
        EXPECT_EQ(reply["result"]["structuredContent"]["error"]["code"], "viewport_not_presentable") << tool;
    }
    EXPECT_EQ(protocol.PendingCount(), 0u) << "the failure is immediate, nothing waits";
}

TEST(AgentOperations, PendingCapIsEnforced)
{
    R::AgentOperationRegistry registry;
    int invocations = 0;
    const auto stuck = [&invocations](const R::AgentOperationContext&, std::string_view) {
        ++invocations;
        R::AgentOperationOutcome outcome{};
        outcome.Continuation = [](const R::AgentOperationContext&, R::AgentOperationOutcome&) { return false; };
        return outcome; };
    ASSERT_TRUE(registry.Register({.Name = "stuck", .Title = "Stuck", .ReadOnly = false, .Invoke = stuck}));
    ASSERT_TRUE(registry.Register({.Name = "stuck_read", .Title = "Stuck read", .ReadOnly = true, .Invoke = stuck}));
    ASSERT_TRUE(registry.Register({.Name = "quick", .Title = "Quick", .ReadOnly = true,
        .Invoke = [](const R::AgentOperationContext&, std::string_view) { return R::AgentOperationOutcome{.Text = R"({"ok":true})"}; }}));
    R::AgentProtocol protocol{registry, false};
    const R::AgentOperationContext context{};
    const auto call = [&](const char* name, int id) {
        return protocol.Handle(Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"}, {"params", {{"name", name}}}}.dump(), context);
    };
    for (std::size_t i = 0; i < R::AgentProtocol::kMaxPendingCalls; ++i) EXPECT_FALSE(call("stuck", int(i)).has_value());
    EXPECT_EQ(protocol.PendingCount(), 16u);
    const auto refused = Json::parse(*call("stuck", 99));
    EXPECT_EQ(refused["error"]["code"], -32000);
    EXPECT_EQ(refused["error"]["message"], "too many pending calls");
    EXPECT_EQ(invocations, 16) << "a refused state-changing call never runs the tool";
    // Read-only tools that answer immediately are still served at the cap.
    const auto served = Json::parse(*call("quick", 100));
    EXPECT_EQ(served["result"]["isError"], false);
    // A read-only tool that would defer is refused (it ran, but changed nothing).
    EXPECT_EQ(Json::parse(*call("stuck_read", 101))["error"]["code"], -32000);
    EXPECT_EQ(protocol.PendingCount(), 16u);
}

TEST(AgentOperations, GpuToolsAreRefusedAndPendingGpuCallsFailWhileMinimized)
{
    R::AgentOperationRegistry registry;
    int invocations = 0;
    ASSERT_TRUE(registry.Register({.Name = "gpu_job", .Title = "GPU job", .ReadOnly = false, .NeedsPresentedFrame = true,
        .Invoke = [&invocations](const R::AgentOperationContext&, std::string_view) {
            ++invocations;
            R::AgentOperationOutcome outcome{};
            outcome.Continuation = [](const R::AgentOperationContext&, R::AgentOperationOutcome&) { return false; };
            return outcome; }}));
    ASSERT_TRUE(registry.Register({.Name = "cpu_job", .Title = "CPU job", .ReadOnly = false,
        .Invoke = [](const R::AgentOperationContext&, std::string_view) {
            R::AgentOperationOutcome outcome{};
            outcome.Continuation = [](const R::AgentOperationContext&, R::AgentOperationOutcome&) { return false; };
            return outcome; }}));
    R::AgentProtocol protocol{registry, false};
    const R::AgentOperationContext presented{};
    const R::AgentOperationContext minimized{.ViewportPresentable = false};
    const auto request = [](const char* name, int id) {
        return Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"}, {"params", {{"name", name}}}}.dump(); };

    // Not started while minimized: no job, no pending slot.
    const auto refused = Json::parse(*protocol.Handle(request("gpu_job", 1), minimized));
    EXPECT_EQ(refused["result"]["structuredContent"]["error"]["code"], "viewport_not_presentable");
    EXPECT_EQ(invocations, 0);
    EXPECT_EQ(protocol.PendingCount(), 0u);

    // Already pending when the window minimizes: failed instead of occupying a slot forever.
    EXPECT_FALSE(protocol.Handle(request("gpu_job", 2), presented).has_value());
    EXPECT_FALSE(protocol.Handle(request("cpu_job", 3), presented).has_value());
    EXPECT_EQ(protocol.PendingCount(), 2u);
    const auto replies = protocol.PollPending(minimized);
    ASSERT_EQ(replies.size(), 1u);
    const Json reply = Json::parse(replies.front());
    EXPECT_EQ(reply["id"], 2);
    EXPECT_EQ(reply["result"]["structuredContent"]["error"]["code"], "viewport_not_presentable");
    EXPECT_EQ(protocol.PendingCount(), 1u) << "CPU-only calls keep waiting";
}

TEST(AgentOperations, LateContinuationStillRepliesAfterManyPollRounds)
{
    R::AgentOperationRegistry registry;
    int polls = 0;
    ASSERT_TRUE(registry.Register({.Name = "late", .Title = "Late", .ReadOnly = true,
        .Invoke = [&polls](const R::AgentOperationContext&, std::string_view) {
            R::AgentOperationOutcome outcome{};
            outcome.Continuation = [&polls](const R::AgentOperationContext&, R::AgentOperationOutcome& out) {
                if (++polls < 5000) return false;
                out = R::AgentOperationOutcome{.Text = R"({"late":true})"};
                return true;
            };
            return outcome; }}));
    R::AgentProtocol protocol{registry, false};
    const R::AgentOperationContext context{};
    ASSERT_FALSE(protocol.Handle(Json{{"jsonrpc", "2.0"}, {"id", "slow"}, {"method", "tools/call"},
                                      {"params", {{"name", "late"}}}}.dump(), context).has_value());
    for (int round = 0; round < 4999; ++round) ASSERT_TRUE(protocol.PollPending(context).empty());
    const auto replies = protocol.PollPending(context);
    ASSERT_EQ(replies.size(), 1u);
    EXPECT_EQ(Json::parse(replies.front())["id"], "slow");
    EXPECT_EQ(protocol.PendingCount(), 0u);
}

namespace
{
    // A call that never finishes; `probe` (when given) is the run's own progress, as an operation
    // that knows its run key would set it.
    R::AgentOperationRegistry NeverFinishingRegistry(
        std::shared_ptr<int> alive,
        std::function<R::EditorOperationProgress(const R::AgentOperationContext&)> probe = {})
    {
        R::AgentOperationRegistry registry;
        EXPECT_TRUE(registry.Register({.Name = "forever", .Title = "Forever", .ReadOnly = true,
            .Invoke = [alive, probe](const R::AgentOperationContext&, std::string_view) {
                R::AgentOperationOutcome outcome{};
                outcome.Continuation = [alive](const R::AgentOperationContext&, R::AgentOperationOutcome&) { return false; };
                outcome.Progress = probe;
                return outcome; }}));
        return registry;
    }
    // The projection of one job as the read model would give it.
    R::EditorOperationProgress ProjectJob(R::JobService& jobs, const R::JobToken token, std::string label)
    {
        const auto progress = jobs.GetProgress(token);
        return {.State = R::EditorOperationState::Running, .Determinate = progress.Determinate,
                .Normalized = progress.Normalized, .Label = std::move(label)};
    }
    Json ForeverCall(const Json& id, const Json& meta = Json::object())
    {
        Json params{{"name", "forever"}};
        if (!meta.empty()) params["_meta"] = meta;
        return {{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"}, {"params", params}};
    }
}

TEST(AgentOperations, ProgressNotificationsAreRateLimited)
{
    auto registry = NeverFinishingRegistry(std::make_shared<int>(0));
    R::AgentProtocol protocol{registry, false};
    protocol.SetProgressInterval(std::chrono::hours(1));
    const R::AgentOperationContext context{};
    ASSERT_FALSE(protocol.Handle(ForeverCall("p1", {{"progressToken", "tok"}}).dump(), context).has_value());
    for (int i = 0; i < 5; ++i) EXPECT_TRUE(protocol.PollPending(context).empty()) << "inside the interval";
    protocol.SetProgressInterval(std::chrono::milliseconds(0));
    double last = -1.0;
    for (int i = 0; i < 3; ++i)
    {
        const auto lines = protocol.PollPending(context);
        ASSERT_EQ(lines.size(), 1u);
        const Json note = Json::parse(lines.front());
        EXPECT_FALSE(note.contains("id")) << "a notification";
        EXPECT_EQ(note["method"], "notifications/progress");
        EXPECT_EQ(note["params"]["progressToken"], "tok");
        EXPECT_EQ(note["params"]["message"], "waiting");
        EXPECT_FALSE(note["params"].contains("total")) << "no job, so no total";
        EXPECT_GE(note["params"]["progress"].get<double>(), last) << "progress never decreases";
        last = note["params"]["progress"].get<double>();
    }
    EXPECT_EQ(protocol.PendingCount(), 1u);
}

TEST(AgentOperations, ProgressOnlyWithToken)
{
    auto registry = NeverFinishingRegistry(std::make_shared<int>(0));
    R::AgentProtocol protocol{registry, false};
    protocol.SetProgressInterval(std::chrono::milliseconds(0));
    const R::AgentOperationContext context{};
    ASSERT_FALSE(protocol.Handle(ForeverCall("a").dump(), context).has_value());
    ASSERT_FALSE(protocol.Handle(ForeverCall("b", {{"progressToken", 7}}).dump(), context).has_value());
    ASSERT_FALSE(protocol.Handle(ForeverCall("c", {{"progressToken", Json::array()}}).dump(), context).has_value());
    const auto lines = protocol.PollPending(context);
    ASSERT_EQ(lines.size(), 1u) << "only the call with a string or integer token reports";
    EXPECT_EQ(Json::parse(lines.front())["params"]["progressToken"], 7);
}

TEST(AgentOperations, CancelledCallsStayAsSilentTombstonesUntilTheirContinuationEnds)
{
    auto finish = std::make_shared<bool>(false);
    R::AgentOperationRegistry registry;
    ASSERT_TRUE(registry.Register({.Name = "gated", .Title = "Gated", .ReadOnly = false,
        .Invoke = [finish](const R::AgentOperationContext&, std::string_view) {
            R::AgentOperationOutcome outcome{};
            outcome.Continuation = [finish](const R::AgentOperationContext&, R::AgentOperationOutcome& out) {
                if (!*finish) return false;
                out = R::AgentOperationOutcome{.Text = "{}"};
                return true;
            };
            return outcome; }}));
    R::AgentProtocol protocol{registry, false};
    protocol.SetProgressInterval(std::chrono::milliseconds(0));
    const R::AgentOperationContext context{};
    const auto call = [&](const Json& id, const Json& meta = Json::object()) {
        Json params{{"name", "gated"}};
        if (!meta.empty()) params["_meta"] = meta;
        return protocol.Handle(Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"}, {"params", params}}.dump(), context);
    };
    const auto cancel = [&](const Json& requestId) {
        return protocol.Handle(Json{{"jsonrpc", "2.0"}, {"method", "notifications/cancelled"},
                                    {"params", {{"requestId", requestId}, {"reason", "test"}}}}.dump(), context);
    };
    ASSERT_FALSE(call(41, {{"progressToken", "t"}}).has_value());
    EXPECT_FALSE(cancel("41").has_value()) << "string 41 is not integer 41";
    EXPECT_FALSE(cancel(99).has_value()) << "unknown ids are ignored";
    EXPECT_FALSE(cancel(41).has_value()) << "notifications never get a reply";
    EXPECT_EQ(protocol.PendingCount(), 1u) << "the tombstone still counts";
    EXPECT_TRUE(protocol.PollPending(context).empty()) << "a cancelled call reports no progress";

    // Call + cancel in a loop cannot dodge the cap: every tombstone keeps its slot.
    for (std::size_t i = 1; i < R::AgentProtocol::kMaxPendingCalls; ++i)
    {
        ASSERT_FALSE(call(int(100 + i)).has_value());
        (void)cancel(int(100 + i));
    }
    EXPECT_EQ(protocol.PendingCount(), 16u);
    EXPECT_EQ(Json::parse(*call(999))["error"]["code"], -32000);

    *finish = true;
    EXPECT_TRUE(protocol.PollPending(context).empty()) << "finished tombstones are dropped without a reply";
    EXPECT_EQ(protocol.PendingCount(), 0u);
}

TEST(AgentOperations, ProgressStrictlyIncreasesAndKeepsOneUnit)
{
    auto registry = NeverFinishingRegistry(std::make_shared<int>(0));
    R::AgentProtocol protocol{registry, false};
    protocol.SetProgressInterval(std::chrono::milliseconds(0));
    const R::AgentOperationContext context{};
    ASSERT_FALSE(protocol.Handle(ForeverCall("p", {{"progressToken", "tok"}}).dump(), context).has_value());
    double last = -1.0;
    int emitted = 0;
    for (int i = 0; i < 200; ++i)
        for (const auto& line : protocol.PollPending(context))
        {
            const Json params = Json::parse(line)["params"];
            EXPECT_GT(params["progress"].get<double>(), last);
            EXPECT_FALSE(params.contains("total")) << "seconds never carry a total";
            last = params["progress"].get<double>();
            ++emitted;
        }
    EXPECT_GT(emitted, 0);
}

// A determinate job reports percent with total 100, capped at 100; an indeterminate job pauses
// the notifications instead of switching unit, and a later determinate value resumes them.
TEST(AgentOperations, ProgressPercentModeCapsAndSkipsIndeterminateJobs)
{
    if (Extrinsic::Core::Tasks::Scheduler::IsInitialized()) Extrinsic::Core::Tasks::Scheduler::Shutdown();
    Extrinsic::Core::Tasks::Scheduler::Initialize(1);
    R::JobService jobs;
    std::atomic_bool release{false};
    const auto token = jobs.Submit({.DebugName = "long job",
        .Work = [&release](const R::JobCancellation&) {
            while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return R::JobResultEnvelope{}; },
        .PublishCompletion = [](R::KernelEventBus&, const R::JobResultEnvelope&) { return true; }});
    ASSERT_TRUE(token.IsValid());
    auto registry = NeverFinishingRegistry(std::make_shared<int>(0),
        [&jobs, token](const R::AgentOperationContext&) { return ProjectJob(jobs, token, "long job"); });
    R::AgentProtocol protocol{registry, false};
    protocol.SetProgressInterval(std::chrono::milliseconds(0));
    const R::AgentOperationContext context{.Jobs = &jobs};
    ASSERT_FALSE(protocol.Handle(ForeverCall("p", {{"progressToken", "tok"}}).dump(), context).has_value());
    const auto next = [&](float normalized, bool determinate) -> std::optional<Json> {
        jobs.ReportProgress(token, {normalized, determinate});
        const auto lines = protocol.PollPending(context);
        if (lines.empty()) return std::nullopt;
        return Json::parse(lines.front())["params"];
    };
    const auto first = next(0.25f, true);
    ASSERT_TRUE(first.has_value());
    EXPECT_DOUBLE_EQ((*first)["progress"].get<double>(), 25.0);
    EXPECT_DOUBLE_EQ((*first)["total"].get<double>(), 100.0);
    EXPECT_EQ((*first)["message"], "long job");
    EXPECT_FALSE(next(0.25f, true).has_value()) << "an equal value is not an increase";
    EXPECT_FALSE(next(0.9f, false).has_value()) << "indeterminate: no switch of unit, no notification";
    EXPECT_DOUBLE_EQ((*next(0.5f, true))["progress"].get<double>(), 50.0) << "a determinate job resumes";
    EXPECT_DOUBLE_EQ((*next(3.0f, true))["progress"].get<double>(), 100.0) << "capped at total";
    EXPECT_FALSE(next(3.0f, true).has_value());
    release.store(true);
    jobs.CancelAndDrain();
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();
    Extrinsic::Core::Tasks::Scheduler::Shutdown();
}

// RUNTIME-312 slice 8: with two jobs running, the notifications follow the call's own run, not
// the oldest job; a call with no run key reports only its age as "waiting".
TEST(AgentOperations, ProgressFollowsTheCallsOwnJobNotTheOldestOne)
{
    if (Extrinsic::Core::Tasks::Scheduler::IsInitialized()) Extrinsic::Core::Tasks::Scheduler::Shutdown();
    Extrinsic::Core::Tasks::Scheduler::Initialize(2);
    R::JobService jobs;
    std::atomic_bool release{false};
    const auto submit = [&](const char* name) {
        return jobs.Submit({.DebugName = name,
            .Work = [&release](const R::JobCancellation&) {
                while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return R::JobResultEnvelope{}; },
            .PublishCompletion = [](R::KernelEventBus&, const R::JobResultEnvelope&) { return true; }});
    };
    const auto older = submit("older job");
    ASSERT_TRUE(older.IsValid());
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); // the older job is also the longer-running one
    const auto own = submit("this call's job");
    ASSERT_TRUE(own.IsValid());
    jobs.ReportProgress(older, {0.9f, true});
    jobs.ReportProgress(own, {0.2f, true});

    auto registry = NeverFinishingRegistry(std::make_shared<int>(0),
        [&jobs, own](const R::AgentOperationContext&) { return ProjectJob(jobs, own, "this call's job"); });
    R::AgentProtocol protocol{registry, false};
    protocol.SetProgressInterval(std::chrono::milliseconds(0));
    const R::AgentOperationContext context{.Jobs = &jobs};
    ASSERT_FALSE(protocol.Handle(ForeverCall("own", {{"progressToken", "t"}}).dump(), context).has_value());
    const auto lines = protocol.PollPending(context);
    ASSERT_EQ(lines.size(), 1u);
    const Json note = Json::parse(lines.front())["params"];
    EXPECT_NEAR(note["progress"].get<double>(), 20.0, 1e-4) << "the call's own job, not the older one's 90%";
    EXPECT_EQ(note["message"], "this call's job");

    // No run key: the oldest running job must not stand in for it.
    auto keyless = NeverFinishingRegistry(std::make_shared<int>(0));
    R::AgentProtocol other{keyless, false};
    other.SetProgressInterval(std::chrono::milliseconds(0));
    ASSERT_FALSE(other.Handle(ForeverCall("keyless", {{"progressToken", "k"}}).dump(), context).has_value());
    const auto waiting = other.PollPending(context);
    ASSERT_EQ(waiting.size(), 1u);
    const Json waitingNote = Json::parse(waiting.front())["params"];
    EXPECT_EQ(waitingNote["message"], "waiting");
    EXPECT_FALSE(waitingNote.contains("total"));

    release.store(true);
    jobs.CancelAndDrain();
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();
    Extrinsic::Core::Tasks::Scheduler::Shutdown();
}

// A cancelled GPU call that then sees the window minimized frees its slot without a reply.
TEST(AgentOperations, CancelledGpuCallIsFreedWithoutReplyWhenMinimized)
{
    R::AgentOperationRegistry registry;
    ASSERT_TRUE(registry.Register({.Name = "gpu_job", .Title = "GPU job", .ReadOnly = false, .NeedsPresentedFrame = true,
        .Invoke = [](const R::AgentOperationContext&, std::string_view) {
            R::AgentOperationOutcome outcome{};
            outcome.Continuation = [](const R::AgentOperationContext&, R::AgentOperationOutcome&) { return false; };
            return outcome; }}));
    R::AgentProtocol protocol{registry, false};
    const R::AgentOperationContext presented{};
    const R::AgentOperationContext minimized{.ViewportPresentable = false};
    ASSERT_FALSE(protocol.Handle(Json{{"jsonrpc", "2.0"}, {"id", 5}, {"method", "tools/call"},
                                      {"params", {{"name", "gpu_job"}}}}.dump(), presented).has_value());
    ASSERT_FALSE(protocol.Handle(Json{{"jsonrpc", "2.0"}, {"method", "notifications/cancelled"},
                                      {"params", {{"requestId", 5}}}}.dump(), presented).has_value());
    EXPECT_EQ(protocol.PendingCount(), 1u) << "the tombstone holds its slot";
    EXPECT_TRUE(protocol.PollPending(minimized).empty()) << "no reply for a cancelled call";
    EXPECT_EQ(protocol.PendingCount(), 0u);
}

// A read-only tool that needs a presented frame is refused at the pending cap before it runs.
TEST(AgentOperations, ReadOnlyGpuToolIsRefusedAtTheCapBeforeItRuns)
{
    R::AgentOperationRegistry registry;
    int runs = 0;
    const auto stuck = [&runs](const R::AgentOperationContext&, std::string_view) {
        ++runs;
        R::AgentOperationOutcome outcome{};
        outcome.Continuation = [](const R::AgentOperationContext&, R::AgentOperationOutcome&) { return false; };
        return outcome;
    };
    ASSERT_TRUE(registry.Register({.Name = "stuck", .Title = "Stuck", .ReadOnly = false, .Invoke = stuck}));
    ASSERT_TRUE(registry.Register({.Name = "shot", .Title = "Shot", .ReadOnly = true, .NeedsPresentedFrame = true, .Invoke = stuck}));
    R::AgentProtocol protocol{registry, false};
    const R::AgentOperationContext context{};
    const auto call = [&](const char* name, int id) {
        return protocol.Handle(Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"}, {"params", {{"name", name}}}}.dump(), context);
    };
    for (int i = 0; i < 16; ++i) ASSERT_FALSE(call("stuck", i).has_value());
    EXPECT_EQ(runs, 16);
    EXPECT_EQ(Json::parse(*call("shot", 100))["error"]["code"], -32000);
    EXPECT_EQ(runs, 16) << "the capture never started";
}

TEST(AgentOperations, NegotiatesProtocolVersion)
{
    int mutations = 0;
    auto registry = TestRegistry(mutations);
    const R::AgentOperationContext context{};
    const auto initialize = [&](R::AgentProtocol& protocol, const char* version) {
        return Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
                                        {"params", {{"protocolVersion", version}, {"capabilities", Json::object()}}}});
    };
    const auto callEcho = [&](R::AgentProtocol& protocol) {
        return Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/call"},
                                        {"params", {{"name", "echo"}, {"arguments", {{"x", 1}}}}}});
    };
    for (const char* version : {"2025-06-18", "2025-03-26", "2024-11-05"})
    {
        R::AgentProtocol protocol{registry, false};
        EXPECT_EQ(initialize(protocol, version)["result"]["protocolVersion"], version);
        EXPECT_EQ(protocol.NegotiatedVersion(), version);
    }
    R::AgentProtocol unknown{registry, false};
    EXPECT_EQ(initialize(unknown, "1999-01-01")["result"]["protocolVersion"], "2025-06-18");
    EXPECT_EQ(unknown.NegotiatedVersion(), "2025-06-18");

    R::AgentProtocol legacy{registry, false};
    (void)initialize(legacy, "2024-11-05");
    EXPECT_FALSE(callEcho(legacy)["result"].contains("structuredContent")) << "older revisions predate structured results";
    R::AgentProtocol current{registry, false};
    (void)initialize(current, "2025-06-18");
    EXPECT_TRUE(callEcho(current)["result"].contains("structuredContent"));
}

TEST(AgentOperations, ToolResultsCarryStructuredContent)
{
    R::AgentOperationRegistry registry;
    ASSERT_TRUE(registry.Register({.Name = "object", .ReadOnly = true, .Invoke = [](const R::AgentOperationContext&, std::string_view) {
        return R::AgentOperationOutcome{.Text = R"({"a":1})"}; }}));
    ASSERT_TRUE(registry.Register({.Name = "plain", .ReadOnly = true, .Invoke = [](const R::AgentOperationContext&, std::string_view) {
        return R::AgentOperationOutcome{.IsError = true, .Text = "not json"}; }}));
    ASSERT_TRUE(registry.Register({.Name = "coded", .ReadOnly = true, .Invoke = [](const R::AgentOperationContext&, std::string_view) {
        return R::AgentOperationOutcome{.IsError = true, .Text = "it exists", .ErrorCode = "file_exists"}; }}));
    ASSERT_TRUE(registry.Register({.Name = "array", .ReadOnly = true, .Invoke = [](const R::AgentOperationContext&, std::string_view) {
        return R::AgentOperationOutcome{.Text = "[1,2]"}; }}));
    R::AgentProtocol protocol{registry, false};
    const R::AgentOperationContext context{};
    const auto call = [&](const char* name) {
        return Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"}, {"params", {{"name", name}}}})["result"];
    };
    const auto object = call("object");
    EXPECT_EQ(object["structuredContent"], (Json{{"a", 1}}));
    EXPECT_EQ(object["content"][0]["text"], R"({"a":1})") << "the text content stays";
    const auto plain = call("plain");
    EXPECT_FALSE(plain.contains("structuredContent"));
    EXPECT_TRUE(plain["isError"].get<bool>());
    const auto coded = call("coded");
    EXPECT_EQ(coded["structuredContent"]["error"]["code"], "file_exists");
    EXPECT_EQ(coded["structuredContent"]["error"]["message"], "it exists");
    EXPECT_FALSE(call("array").contains("structuredContent")) << "structuredContent is an object";
}

TEST(AgentOperations, AnnotationsReflectUndoability)
{
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    R::RegisterViewCaptureAgentOperations(registry);
    R::AgentProtocol protocol{registry, false};
    const R::AgentOperationContext context{};
    const auto list = Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
    bool sawCapture = false;
    for (const auto& tool : list["result"]["tools"])
    {
        const bool destructive = tool["annotations"]["destructiveHint"].get<bool>();
        // Destructive: files, the replaced scene document and engine config, the effects the undo history does not cover.
        EXPECT_EQ(destructive, tool["name"] == "view_capture" || tool["name"] == "config_apply" || tool["name"] == "save_scene" ||
                                   tool["name"] == "load_scene") << tool["name"];
        sawCapture |= tool["name"] == "view_capture";
    }
    EXPECT_TRUE(sawCapture);
}

// Every tool that may dispatch GPU work is refused on a minimized frame instead of waiting.
TEST(AgentOperations, GpuCapableEditorToolsNeedAPresentedFrame)
{
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    for (const char* name : {"run_registration", "run_point_sampling", "run_keypoint_analysis", "run_kmeans",
                             "run_point_cloud_consolidation", "run_mesh_operation"})
    {
        ASSERT_NE(registry.Find(name), nullptr) << name;
        EXPECT_TRUE(registry.Find(name)->NeedsPresentedFrame) << name;
    }
    for (const char* name : {"preview_registration", "preview_mesh_operation", "scene_entities", "select_entity"})
    {
        ASSERT_NE(registry.Find(name), nullptr) << name;
        EXPECT_FALSE(registry.Find(name)->NeedsPresentedFrame) << name;
    }
    const R::AgentOperationContext minimized{.ViewportPresentable = false};
    const auto refused = R::InvokeAgentOperation(registry, "run_mesh_operation", minimized,
                                                 R"({"operation":"property_smoothing","entity":1})", false);
    EXPECT_TRUE(refused.IsError);
    EXPECT_EQ(refused.ErrorCode, "viewport_not_presentable");
}

// Tools that name an element domain share one enum, generated from GeometryElementDomain.
TEST(AgentOperations, EditorOperationsShareTheDomainEnum)
{
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    Json expected = Json::array();
    for (unsigned i = 1; i <= unsigned(R::GeometryElementDomain::PointCloudPoint); ++i)
        expected.push_back(std::string(R::ToString(static_cast<R::GeometryElementDomain>(i))));
    int seen = 0;
    for (const auto& spec : registry.Entries())
    {
        const auto schema = Json::parse(spec.InputSchemaJson);
        if (!schema["properties"].contains("domain")) continue;
        ++seen;
        EXPECT_EQ(schema["properties"]["domain"]["enum"], expected) << spec.Name;
    }
    EXPECT_GE(seen, 2) << "k-means and consolidation at least";
    EXPECT_EQ(Json::parse(registry.Find("run_point_cloud_consolidation")->InputSchemaJson)["properties"]["positions"]["default"], "v:position");
    const R::AgentOperationContext context{};
    const auto bad = R::InvokeAgentOperation(registry, "run_kmeans", context, R"({"entity":1,"domain":"Bogus"})", false);
    EXPECT_TRUE(bad.IsError);
    EXPECT_NE(bad.Text.find("Unknown domain 'Bogus'"), std::string::npos) << bad.Text;
    const auto badShow = R::InvokeAgentOperation(registry, "show_property", context,
                                                 R"({"entity":1,"name":"x","domain":"Bogus"})", false);
    EXPECT_NE(badShow.Text.find("Unknown domain 'Bogus'"), std::string::npos) << badShow.Text;
}

// RUNTIME-316: the attribute tools take their attribute enum and row documentation from the
// runtime table (RenderAttributeRules), and refuse malformed arguments with invalid_params.
TEST(AgentOperations, AttributeBindingToolsFollowTheRuntimeTable)
{
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    Json attributes = Json::array();
    for (const R::RenderAttributeRule& rule : R::RenderAttributeRules())
        if (std::ranges::find(attributes, Json(std::string(R::ToString(rule.Attribute)))) == attributes.end())
            attributes.push_back(std::string(R::ToString(rule.Attribute)));
    ASSERT_EQ(attributes.size(), 6u);
    const auto* listing = registry.Find("attribute_bindings");
    ASSERT_NE(listing, nullptr);
    EXPECT_TRUE(listing->ReadOnly);
    EXPECT_FALSE(listing->Destructive);
    EXPECT_FALSE(listing->NeedsPresentedFrame);
    const auto schema = Json::parse(listing->InputSchemaJson);
    EXPECT_EQ(schema["properties"]["attribute"]["enum"], attributes);
    EXPECT_EQ(schema["required"], Json::array({"entity"}));
    for (const R::RenderAttributeRule& rule : R::RenderAttributeRules())
    {
        const std::string row = std::string(R::ToString(rule.Domain)) + " [default: " + std::string(rule.DefaultDescription) + "]";
        EXPECT_NE(listing->Description.find(row), std::string::npos) << "the description documents " << row;
    }

    const R::AgentOperationContext empty{};
    for (const char* arguments : {R"({})", R"({"entity":1,"attribute":"radius"})", R"({"entity":1,"domain":"Bogus"})"})
    {
        const auto refused = R::InvokeAgentOperation(registry, "attribute_bindings", empty, arguments, false);
        EXPECT_TRUE(refused.IsError) << arguments;
        EXPECT_EQ(refused.ErrorCode, "invalid_params") << arguments;
    }
    const auto detached = R::InvokeAgentOperation(registry, "attribute_bindings", empty, R"({"entity":1})", false);
    EXPECT_TRUE(detached.IsError);
    EXPECT_NE(detached.Text.find("not attached"), std::string::npos) << detached.Text;
}

// RUNTIME-316: bind_attribute is one undoable mutation with the same generated enums as the
// listing, exactly one of property / default, and invalid_params for malformed arguments.
TEST(AgentOperations, BindAttributeValidatesItsArgumentsBeforeTouchingTheScene)
{
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    const auto* bind = registry.Find("bind_attribute");
    ASSERT_NE(bind, nullptr);
    EXPECT_FALSE(bind->ReadOnly);
    EXPECT_FALSE(bind->Destructive) << "undoable: recorded as an Agent: history entry";
    EXPECT_FALSE(bind->NeedsPresentedFrame);
    const auto schema = Json::parse(bind->InputSchemaJson);
    EXPECT_EQ(schema["properties"]["attribute"], Json::parse(registry.Find("attribute_bindings")->InputSchemaJson)["properties"]["attribute"]);
    EXPECT_EQ(schema["required"], Json::array({"entity", "attribute", "domain"}));
    EXPECT_EQ(schema["oneOf"].size(), 2u);
    for (const R::RenderAttributeRule& rule : R::RenderAttributeRules())
        EXPECT_NE(bind->Description.find(std::string(R::ToString(rule.Domain)) + " [default: " + std::string(rule.DefaultDescription) + "]"),
                  std::string::npos);

    const R::AgentOperationContext empty{};
    for (const char* arguments : {
             R"({})",
             R"({"entity":1,"attribute":"position","domain":"MeshVertex"})",
             R"({"entity":1,"attribute":"position","domain":"MeshVertex","property":"p","default":true})",
             R"({"entity":1,"attribute":"position","domain":"MeshVertex","default":false})",
             R"({"entity":1,"attribute":"position","domain":"MeshVertex","property":""})",
             R"({"entity":1,"attribute":"radius","domain":"MeshVertex","property":"p"})",
             R"({"entity":1,"attribute":"position","domain":"Bogus","property":"p"})"})
    {
        const auto refused = R::InvokeAgentOperation(registry, "bind_attribute", empty, arguments, false);
        EXPECT_TRUE(refused.IsError) << arguments;
        EXPECT_EQ(refused.ErrorCode, "invalid_params") << arguments;
    }
    const auto detached = R::InvokeAgentOperation(registry, "bind_attribute", empty,
                                                  R"({"entity":1,"attribute":"position","domain":"MeshVertex","default":true})", false);
    EXPECT_TRUE(detached.IsError);
    EXPECT_NE(detached.Text.find("not attached"), std::string::npos) << detached.Text;
    EXPECT_TRUE(R::InvokeAgentOperation(registry, "bind_attribute", empty, "{}", true).IsError) << "refused in a read-only session";
}

// A job queued during an agent call publishes on a later frame, after the call's label scope
// ended: the submit path carries the prefix. A job queued without a prefix (a panel) gets none.
TEST(AgentOperations, QueuedJobsKeepTheSubmittingCallsLabelPrefix)
{
    if (Extrinsic::Core::Tasks::Scheduler::IsInitialized()) Extrinsic::Core::Tasks::Scheduler::Shutdown();
    Extrinsic::Core::Tasks::Scheduler::Initialize(1);
    {
        R::JobService jobs;
        R::KernelEventBus events;
        R::EditorCommandHistory history;
        const auto submit = [&](const char* label) {
            R::JobDesc desc;
            desc.DebugName = label;
            desc.Work = [](const R::JobCancellation&) { return R::JobResultEnvelope::Make(1); };
            desc.PublishCompletion = [&history, label](R::KernelEventBus&, const R::JobResultEnvelope&) {
                (void)history.Execute({.Label = label, .Redo = [] { return R::EditorCommandHistoryStatus::Applied; },
                                       .Undo = [] { return R::EditorCommandHistoryStatus::Applied; }});
                return true;
            };
            R::CarryEditorLabelPrefix(desc, &history, history.LabelPrefix());
            EXPECT_TRUE(jobs.Submit(std::move(desc)).IsValid());
        };
        {
            const R::ScopedEditorCommandLabelPrefix scope{&history, "Agent: "};
            submit("agent job");
        }
        EXPECT_TRUE(history.LabelPrefix().empty()) << "the call's scope ended before the job publishes";
        submit("panel job");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (history.UndoCount() < 2 && std::chrono::steady_clock::now() < deadline)
        {
            (void)jobs.DrainCompletions(events);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_EQ(history.UndoCount(), 2u);
        std::vector<std::string> labels;
        for (int i = 0; i < 2; ++i) labels.push_back(history.Undo().Label);
        std::ranges::sort(labels);
        EXPECT_EQ(labels, (std::vector<std::string>{"Agent: agent job", "panel job"}));
        EXPECT_TRUE(history.LabelPrefix().empty()) << "publishing restored the prefix";
        jobs.CancelAndDrain();
    }
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();
    Extrinsic::Core::Tasks::Scheduler::Shutdown();
}

TEST(AgentOperations, ConfiguredOperationEnumMatchesTable)
{
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    const auto enumOf = [&](const char* tool) {
        const auto* spec = registry.Find(tool);
        EXPECT_NE(spec, nullptr) << tool;
        return spec ? Json::parse(spec->InputSchemaJson)["properties"]["operation"]["enum"] : Json::array();
    };
    const Json all = enumOf("run_operation");
    EXPECT_EQ(all, enumOf("preview_operation"));
    for (const char* name : {"property_smoothing", "spectral_modes", "harmonic_field", "scalar_gradient", "mesh_curvature", "geodesics",
                             "curvature_segmentation", "normal_estimation", "kernel_density", "point_spacing", "outlier_analysis",
                             "density_weight", "descriptor_analysis", "bilateral_filter", "point_construction", "mesh_denoise",
                             "mesh_remesh", "mesh_subdivide", "mesh_simplify", "scalar_ridge", "progressive_poisson", "parameterization"})
        EXPECT_NE(std::ranges::find(all, name), all.end()) << name;
    // The mesh aliases keep their four-operation enum, all of which the table serves.
    EXPECT_EQ(enumOf("run_mesh_operation"), (Json{"property_smoothing", "spectral_modes", "harmonic_field", "scalar_gradient"}));
    EXPECT_EQ(enumOf("preview_mesh_operation"), enumOf("run_mesh_operation"));
    EXPECT_TRUE(registry.Find("run_operation")->NeedsPresentedFrame);
    EXPECT_FALSE(registry.Find("run_operation")->ReadOnly);
    EXPECT_TRUE(registry.Find("preview_operation")->ReadOnly);
    EXPECT_FALSE(registry.Find("preview_operation")->NeedsPresentedFrame);
    const auto required = Json::parse(registry.Find("run_operation")->InputSchemaJson)["required"];
    EXPECT_EQ(required, (Json{"operation"})) << "entity is optional: config-sourced rows take none";
    const auto description = registry.Find("run_operation")->Description;
    EXPECT_NE(description.find("mesh_simplify (entity argument; params metric=fa_qem [one of: classical_qem, fa_qem], target_faces=0 [0 to 1e+09], max_error=0.0 [at least 0]"), std::string::npos)
        << "params defaults come from the command structs: " << description;
    const Json schema = Json::parse(registry.Find("run_operation")->InputSchemaJson);
    EXPECT_TRUE(schema["properties"].contains("params"));
    // Each explicit-parameter operation exposes exactly the fields its owner declares, with the owner's ranges.
    const auto paramsOf = [&](const char* operation) {
        for (const auto& rule : schema["allOf"])
            if (rule["if"]["properties"]["operation"]["const"] == operation) return rule["then"]["properties"]["params"];
        return Json{};
    };
    const struct { const char* Operation; std::span<const R::ConfigFieldSpec> Fields; } owners[] = {
        {"mesh_denoise", R::EditorMeshDenoiseFieldSpecs()}, {"mesh_remesh", R::EditorMeshRemeshFieldSpecs()},
        {"mesh_subdivide", R::EditorMeshSubdivideFieldSpecs()}, {"mesh_simplify", R::EditorMeshSimplifyFieldSpecs()},
        {"scalar_ridge", R::EditorScalarRidgeFieldSpecs()}};
    for (const auto& owner : owners)
    {
        const Json params = paramsOf(owner.Operation);
        ASSERT_TRUE(params.contains("properties")) << owner.Operation;
        EXPECT_EQ(params["properties"].size(), owner.Fields.size()) << owner.Operation;
        for (const auto& field : owner.Fields) EXPECT_TRUE(params["properties"].contains(std::string(field.Name))) << owner.Operation << "." << field.Name;
    }
    EXPECT_EQ(paramsOf("mesh_subdivide")["properties"]["iterations"]["maximum"], R::kMeshSubdivideMaxIterations);
    EXPECT_EQ(paramsOf("mesh_subdivide")["properties"]["iterations"]["default"], 1);
    EXPECT_EQ(paramsOf("mesh_remesh")["properties"]["iterations"]["maximum"], R::kMeshRemeshMaxIterations);

}

TEST(AgentOperations, ConfiguredOperationEntityRulesFollowTheSection)
{
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    const R::AgentOperationContext context{};
    const auto call = [&](const char* tool, const char* arguments) { return R::InvokeAgentOperation(registry, tool, context, arguments, false); };
    // A section that names the entity supplies it: an argument is refused and the message names the section.
    const auto refused = call("run_operation", R"({"operation":"outlier_analysis","entity":3})");
    EXPECT_TRUE(refused.IsError);
    EXPECT_NE(refused.Text.find("sandbox.outlier_analysis"), std::string::npos) << refused.Text;
    EXPECT_NE(refused.Text.find("do not pass entity"), std::string::npos) << refused.Text;
    EXPECT_NE(call("preview_operation", R"({"operation":"mesh_curvature","entity":3})").Text.find("sandbox.mesh_curvature"), std::string::npos);
    // The other rows need the entity.
    const auto missing = call("run_operation", R"({"operation":"geodesics"})");
    EXPECT_TRUE(missing.IsError);
    EXPECT_NE(missing.Text.find("needs {\"entity\""), std::string::npos) << missing.Text;
    EXPECT_NE(call("preview_mesh_operation", R"({"operation":"scalar_gradient"})").Text.find("needs"), std::string::npos);
    // Unknown names list the valid ones; the mesh alias does not serve other families.
    const auto unknown = call("run_operation", R"({"operation":"nope","entity":1})");
    EXPECT_NE(unknown.Text.find("Unknown operation 'nope'"), std::string::npos);
    const auto aliasMiss = call("run_mesh_operation", R"({"operation":"outlier_analysis"})");
    EXPECT_NE(aliasMiss.Text.find("Unknown operation"), std::string::npos) << aliasMiss.Text;
    EXPECT_EQ(aliasMiss.Text.find("outlier_analysis\"]"), std::string::npos);
}

// Scene files and imports resolve their path against the allowed roots before anything runs.
TEST(AgentOperations, SceneFileToolsStayInsideTheRootsAndRefuseToOverwrite)
{
    namespace fs = std::filesystem;
    const auto info = ::testing::UnitTest::GetInstance()->current_test_info();
    const auto root = fs::weakly_canonical(fs::temp_directory_path() / (std::string("intrinsic-agent-scene-") + info->name() + "-" +
                                                                         std::to_string(::getpid())));
    const auto outside = fs::weakly_canonical(fs::temp_directory_path() / (std::string("intrinsic-agent-outside-") + info->name() + "-" +
                                                                            std::to_string(::getpid())));
    fs::remove_all(root);
    fs::remove_all(outside);
    fs::create_directories(root);
    fs::create_directories(outside);
    { std::ofstream(root / "taken.scene") << "x"; }
    // A dangling symlink inside the root must not lead a write to its target outside the roots.
    std::error_code linkError;
    fs::create_symlink(outside / "created.scene", root / "dangling.scene", linkError);
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    const R::AgentOperationContext context{.AllowedRoots = {root.string()}};
    const auto call = [&](const char* tool, const Json& arguments) {
        return R::InvokeAgentOperation(registry, tool, context, arguments.dump(), false); };
    for (const char* tool : {"import_file", "save_scene", "load_scene"})
    {
        const auto outside = call(tool, {{"path", "/etc/passwd"}});
        EXPECT_TRUE(outside.IsError) << tool;
        EXPECT_NE(outside.Text.find("allowed agent roots"), std::string::npos) << tool << ": " << outside.Text;
    }
    const auto taken = call("save_scene", {{"path", "taken.scene"}});
    EXPECT_TRUE(taken.IsError);
    EXPECT_EQ(taken.ErrorCode, "file_exists");
    // With overwrite the call gets past the file check to the missing workspace.
    const auto allowed = call("save_scene", {{"path", "taken.scene"}, {"overwrite", true}});
    EXPECT_TRUE(allowed.IsError);
    EXPECT_TRUE(allowed.ErrorCode.empty()) << allowed.Text;
    EXPECT_TRUE(call("save_scene", {{"path", "taken.scene"}, {"overwrite", "yes"}}).IsError);
    EXPECT_NE(call("load_scene", {{"path", "missing.scene"}}).Text.find("not an existing file"), std::string::npos);
    EXPECT_NE(call("load_scene", {{"path", "taken.scene"}}).Text.find("workspace is not attached"), std::string::npos);
    if (!linkError)
    {
        const auto dangling = call("save_scene", {{"path", "dangling.scene"}, {"overwrite", true}});
        EXPECT_TRUE(dangling.IsError);
        EXPECT_NE(dangling.Text.find("allowed agent roots"), std::string::npos) << dangling.Text;
        EXPECT_FALSE(fs::exists(outside / "created.scene"));
    }
    EXPECT_TRUE(registry.Find("save_scene")->Destructive);
    EXPECT_TRUE(registry.Find("load_scene")->Destructive);
    EXPECT_FALSE(registry.Find("import_file")->Destructive);
    fs::remove_all(root);
    fs::remove_all(outside);
}

TEST(AgentOperations, ViewCaptureRefusesToOverwriteUnlessAsked)
{
    namespace fs = std::filesystem;
    const auto info = ::testing::UnitTest::GetInstance()->current_test_info();
    const auto root = fs::weakly_canonical(fs::temp_directory_path() / (std::string("intrinsic-agent-overwrite-") + info->name()));
    fs::remove_all(root);
    fs::create_directories(root);
    { std::ofstream(root / "shot.png") << "x"; }
    std::error_code linkError;
    fs::create_symlink(root / "nowhere.png", root / "dangling.png", linkError);
    R::AgentOperationRegistry registry;
    R::RegisterViewCaptureAgentOperations(registry);
    R::AgentProtocol protocol{registry, false};
    const R::AgentOperationContext context{.AllowedRoots = {root.string()}};
    const auto capture = [&](const Json& arguments) {
        return Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                                        {"params", {{"name", "view_capture"}, {"arguments", arguments}}}})["result"];
    };
    const auto refused = capture({{"path", "shot.png"}});
    EXPECT_TRUE(refused["isError"].get<bool>());
    EXPECT_EQ(refused["structuredContent"]["error"]["code"], "file_exists");
    if (!linkError)
    {
        // A dangling symlink is refused before the capture: path resolution never follows it out of the roots.
        const auto dangling = capture({{"path", "dangling.png"}});
        EXPECT_TRUE(dangling["isError"].get<bool>());
        EXPECT_NE(dangling["content"][0]["text"].get<std::string>().find("outside the Sandbox"), std::string::npos);
    }
    // With overwrite the preflight passes and the call reaches the (absent) capture service.
    const auto allowed = capture({{"path", "shot.png"}, {"overwrite", true}});
    EXPECT_TRUE(allowed["isError"].get<bool>());
    EXPECT_FALSE(allowed.contains("structuredContent")) << allowed.dump();
    EXPECT_NE(allowed["content"][0]["text"].get<std::string>().find("not available"), std::string::npos);
    // A new file name is not an overwrite: it gets past the preflight to the missing service.
    const auto fresh = capture({{"path", "fresh.png"}});
    EXPECT_FALSE(fresh.contains("structuredContent")) << fresh.dump();
    EXPECT_NE(fresh["content"][0]["text"].get<std::string>().find("not available"), std::string::npos);
    EXPECT_TRUE(capture({{"path", "shot.png"}, {"overwrite", "yes"}})["isError"].get<bool>());
    fs::remove_all(root);
}

TEST(AgentOperations, Base64MatchesTheRfcVectors)
{
    const auto encode = [](std::string_view text) {
        return R::EncodeBase64({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}); };
    EXPECT_EQ(encode(""), "");
    EXPECT_EQ(encode("f"), "Zg==");
    EXPECT_EQ(encode("fo"), "Zm8=");
    EXPECT_EQ(encode("foo"), "Zm9v");
    EXPECT_EQ(encode("foob"), "Zm9vYg==");
    EXPECT_EQ(encode("fooba"), "Zm9vYmE=");
    EXPECT_EQ(encode("foobar"), "Zm9vYmFy");
}

// RUNTIME-279: notifications/cancelled runs the call's Cancel hook (its editor jobs) exactly once;
// the tombstone still holds its slot until the continuation ends. A call without a hook (a
// service run, a capture, a wait) only loses its reply.
TEST(AgentOperations, CancelNotificationCancelsTheCallsJobsOnceAndKeepsTheTombstone)
{
    auto finish = std::make_shared<bool>(false);
    auto cancels = std::make_shared<int>(0);
    R::AgentOperationRegistry registry;
    const auto deferred = [finish](std::function<std::string(const R::AgentOperationContext&)> cancel) {
        return [finish, cancel](const R::AgentOperationContext&, std::string_view) {
            R::AgentOperationOutcome outcome{};
            outcome.Continuation = [finish](const R::AgentOperationContext&, R::AgentOperationOutcome& out) {
                if (!*finish) return false;
                out = R::AgentOperationOutcome{.Text = "{}"};
                return true;
            };
            outcome.Cancel = cancel;
            return outcome;
        };
    };
    ASSERT_TRUE(registry.Register({.Name = "queues_job", .Title = "Queues a job", .ReadOnly = false,
                                   .Invoke = deferred([cancels](const R::AgentOperationContext&) { ++*cancels; return std::string{"1 job"}; })}));
    ASSERT_TRUE(registry.Register({.Name = "service_run", .Title = "Service run", .ReadOnly = false, .Invoke = deferred({})}));
    R::AgentProtocol protocol{registry, false};
    const R::AgentOperationContext context{};
    const auto call = [&](const char* name, int id) {
        return protocol.Handle(Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"}, {"params", {{"name", name}}}}.dump(), context);
    };
    const auto cancel = [&](int id) {
        return protocol.Handle(Json{{"jsonrpc", "2.0"}, {"method", "notifications/cancelled"}, {"params", {{"requestId", id}}}}.dump(),
                               context);
    };
    ASSERT_FALSE(call("queues_job", 1).has_value());
    ASSERT_FALSE(call("service_run", 2).has_value());
    EXPECT_EQ(*cancels, 0) << "nothing is cancelled before the notification";
    EXPECT_FALSE(cancel(1).has_value());
    EXPECT_EQ(*cancels, 1) << "the call's jobs are cancelled";
    EXPECT_FALSE(cancel(1).has_value());
    EXPECT_EQ(*cancels, 1) << "a repeated notification cancels nothing more";
    EXPECT_FALSE(cancel(2).has_value());
    EXPECT_EQ(*cancels, 1);
    EXPECT_EQ(protocol.PendingCount(), 2u) << "both tombstones hold their slots";
    EXPECT_TRUE(protocol.PollPending(context).empty());
    *finish = true;
    EXPECT_TRUE(protocol.PollPending(context).empty()) << "finished tombstones are dropped without a reply";
    EXPECT_EQ(protocol.PendingCount(), 0u);
}

// The job tools: list and wait are read-only, cancel mutates editor state but no scene data or
// file (not destructive); a wait needs presented frames, so a minimize ends it.
TEST(AgentOperations, JobToolsClassifyAndValidateTheirArguments)
{
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    EXPECT_EQ(registry.Find("jobs"), nullptr) << "jobs_list replaced it";
    const auto* list = registry.Find("jobs_list");
    const auto* wait = registry.Find("jobs_wait");
    const auto* cancel = registry.Find("jobs_cancel");
    ASSERT_TRUE(list && wait && cancel);
    EXPECT_TRUE(list->ReadOnly && !list->Destructive && !list->NeedsPresentedFrame);
    EXPECT_TRUE(wait->ReadOnly && !wait->Destructive && wait->NeedsPresentedFrame);
    EXPECT_TRUE(!cancel->ReadOnly && !cancel->Destructive && !cancel->NeedsPresentedFrame);
    const auto schema = Json::parse(wait->InputSchemaJson);
    EXPECT_EQ(schema["properties"]["timeout_ms"]["maximum"], 60000);

    const R::AgentOperationContext empty{};
    const auto code = [&](const char* name, const char* arguments) {
        return R::InvokeAgentOperation(registry, name, empty, arguments, false).ErrorCode;
    };
    EXPECT_EQ(code("jobs_wait", R"({"token":"1:0","timeout_ms":60001})"), "invalid_params");
    EXPECT_EQ(code("jobs_wait", R"({"token":"1:0","timeout_ms":-1})"), "invalid_params");
    EXPECT_EQ(code("jobs_wait", R"({"token":"1:0","entity":1,"output":"x"})"), "invalid_params");
    EXPECT_EQ(code("jobs_wait", "{}"), "invalid_params");
    EXPECT_EQ(code("jobs_cancel", R"({"token":"x"})"), "invalid_params");
    EXPECT_EQ(code("jobs_cancel", R"({"token":"1"})"), "invalid_params");
    EXPECT_TRUE(R::InvokeAgentOperation(registry, "jobs_cancel", empty, R"({"token":"1:0"})", true).IsError)
        << "a read-only session cannot cancel";
}
