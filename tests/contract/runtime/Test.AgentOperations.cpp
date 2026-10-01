// RUNTIME-287/288: agent operation registry, MCP protocol core, read-only policy, path
// containment and the "Agent:" history label, without sockets or an engine.
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <string>
#include <string_view>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
import Extrinsic.Runtime.AgentServer;
import Extrinsic.Runtime.EditorCommandHistory;
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
                            spec.Name == "jobs" || spec.Name == "log" || spec.Name.starts_with("preview_");
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
    ASSERT_TRUE(registry.Register({.Name = "stuck", .Title = "Stuck", .ReadOnly = true,
        .Invoke = [&invocations](const R::AgentOperationContext&, std::string_view) {
            ++invocations;
            R::AgentOperationOutcome outcome{};
            outcome.Continuation = [](const R::AgentOperationContext&, R::AgentOperationOutcome&) { return false; };
            return outcome; }}));
    R::AgentProtocol protocol{registry, false};
    const R::AgentOperationContext context{};
    for (std::size_t i = 0; i < R::AgentProtocol::kMaxPendingCalls; ++i)
        EXPECT_FALSE(protocol.Handle(Json{{"jsonrpc", "2.0"}, {"id", int(i)}, {"method", "tools/call"},
                                          {"params", {{"name", "stuck"}}}}.dump(), context).has_value());
    EXPECT_EQ(protocol.PendingCount(), 16u);
    const auto refused = Call(protocol, context, {{"jsonrpc", "2.0"}, {"id", 99}, {"method", "tools/call"},
                                                  {"params", {{"name", "stuck"}}}});
    EXPECT_EQ(refused["error"]["code"], -32000);
    EXPECT_EQ(refused["error"]["message"], "too many pending calls");
    EXPECT_EQ(invocations, 16) << "a refused call never runs the tool";
    EXPECT_EQ(protocol.PendingCount(), 16u);
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
        EXPECT_EQ(destructive, tool["name"] == "view_capture") << tool["name"];
        sawCapture |= tool["name"] == "view_capture";
    }
    EXPECT_TRUE(sawCapture);
}

TEST(AgentOperations, ViewCaptureRefusesToOverwriteUnlessAsked)
{
    namespace fs = std::filesystem;
    const auto root = fs::weakly_canonical(fs::temp_directory_path() / "intrinsic-agent-overwrite");
    fs::create_directories(root);
    { std::ofstream(root / "shot.png") << "x"; }
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
    // With overwrite the preflight passes and the call reaches the (absent) capture service.
    const auto allowed = capture({{"path", "shot.png"}, {"overwrite", true}});
    EXPECT_TRUE(allowed["isError"].get<bool>());
    EXPECT_FALSE(allowed.contains("structuredContent")) << allowed.dump();
    // A new file name is not an overwrite.
    EXPECT_FALSE(capture({{"path", "fresh.png"}}).contains("structuredContent"));
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
