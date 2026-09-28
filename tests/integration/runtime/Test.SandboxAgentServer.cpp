// RUNTIME-288: an MCP client drives a running engine through the agent socket: handshake,
// scene and property queries, config apply, a mesh-field run, its "Agent:" undo entry and
// undo, with requests served on the main thread between frames.
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <unistd.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include "RuntimeTestModule.hpp"
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.Platform.LocalSocket;
import Extrinsic.Runtime.AgentServer;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ViewCapture;
import Extrinsic.Sandbox.ConfigSections;
import Extrinsic.Sandbox.Editor.Shell;
import Geometry.HalfedgeMesh;
import Geometry.Properties;
namespace R = Extrinsic::Runtime;
namespace P = Extrinsic::Platform;
namespace Config = Extrinsic::Core::Config;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Editor = Extrinsic::Sandbox::Editor;
using Json = nlohmann::json;
namespace
{
    class Driver final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        std::function<void(R::Engine&)> OnFrame{};
        void Frame(double, double) override { OnFrame(Kernel()); }
    };

    // Blocking MCP client over the agent socket; each call waits for its response line.
    struct Client
    {
        P::LocalSocketConnection Connection;
        std::string Buffer;
        int NextId{1};
        Json Request(const std::string& method, Json params = Json::object())
        {
            const int id = NextId++;
            const Json request{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}};
            if (Connection.SendAll(request.dump() + "\n") != P::LocalSocketStatus::Ok) return {};
            for (int attempt = 0; attempt < 500; ++attempt)
            {
                if (const auto newline = Buffer.find('\n'); newline != std::string::npos)
                {
                    const auto line = Buffer.substr(0, newline);
                    Buffer.erase(0, newline + 1);
                    return Json::parse(line, nullptr, false);
                }
                if (Connection.Receive(Buffer, 20) == P::LocalSocketStatus::Closed) return {};
            }
            return {};
        }
        Json Tool(const std::string& name, Json arguments = Json::object(), bool* isError = nullptr)
        {
            const auto response = Request("tools/call", {{"name", name}, {"arguments", std::move(arguments)}});
            if (!response.contains("result")) return response;
            if (isError) *isError = response["result"]["isError"].get<bool>();
            const auto text = response["result"]["content"][0]["text"].get<std::string>();
            const auto parsed = Json::parse(text, nullptr, false);
            return parsed.is_discarded() ? Json(text) : parsed;
        }
    };
}

TEST(SandboxAgentServer, ClientRunsSmoothingThroughTheSocketAndUndoesIt)
{
    const auto socketPath = (std::filesystem::temp_directory_path() /
                             ("intrinsic-agent-test-" + std::to_string(::getpid()) + ".sock")).string();
    auto sections = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    Config::EngineConfig config{};
    Config::PopulateEngineConfigSectionDefaults(config, sections);
    config.Simulation.WorkerThreadCount = 1u;
    config.ReferenceScene.Enabled = false;
    config.Camera.Enabled = false;
    config.Window.Backend = Config::WindowBackend::Null;
    auto driver = std::make_unique<Driver>();
    Driver* frames = driver.get();
    Intrinsic::Tests::RuntimeTestKernel engine{config, std::move(driver)};
    engine.EmplaceModule<R::EngineConfigControl>(std::move(sections));
    engine.EmplaceModule<R::SceneInteractionModule>();
    engine.EmplaceModule<R::SceneDocumentModule>();
    engine.EmplaceModule<R::AsyncWorkModule>();
    engine.EmplaceModule<R::ViewCaptureModule>(std::filesystem::temp_directory_path());
    auto* server = &engine.EmplaceModule<R::AgentServerModule>(
        R::AgentServerOptions{.SocketPath = socketPath, .AllowedRoots = {std::filesystem::temp_directory_path().string()}});
    engine.Initialize();
    ASSERT_TRUE(server->Status().Listening) << server->Status().LastError;

    // A 6x6 grid with a rough scalar field.
    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const auto entity = scene.Create();
    Geometry::HalfedgeMesh::Mesh mesh;
    std::vector<Geometry::VertexHandle> v;
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 6; ++x) v.push_back(mesh.AddVertex({float(x), float(y), 0.0f}));
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x)
        {
            ASSERT_TRUE(mesh.AddTriangle(v[std::size_t(y * 6 + x)], v[std::size_t(y * 6 + x + 1)], v[std::size_t((y + 1) * 6 + x + 1)]));
            ASSERT_TRUE(mesh.AddTriangle(v[std::size_t(y * 6 + x)], v[std::size_t((y + 1) * 6 + x + 1)], v[std::size_t((y + 1) * 6 + x)]));
        }
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    auto& vertices = scene.Raw().get<GS::Vertices>(entity).Properties;
    {
        auto rough = vertices.GetOrAdd<double>("rough", 0.0);
        for (std::size_t i = 0; i < vertices.Size(); ++i) rough[i] = (i % 2) ? 1.0 : -1.0;
    }
    const auto stableId = R::SelectionController::ToStableEntityId(entity);
    // A shifted copy of the grid as a point cloud: the CPD target.
    const auto cloud = scene.Create();
    {
        auto& points = scene.Raw().emplace<GS::Vertices>(cloud).Properties;
        points.Resize(36);
        auto positions = points.GetOrAdd<glm::vec3>("v:position");
        for (std::size_t i = 0; i < 36; ++i) positions[i] = glm::vec3(float(i % 6) + 0.5f, float(i / 6), 0.0f);
    }
    const auto cloudId = R::SelectionController::ToStableEntityId(cloud);

    std::atomic_bool done{false};
    std::vector<std::string> failures;
    const auto check = [&](bool ok, std::string what) { if (!ok) failures.push_back(std::move(what)); };
    std::thread client([&] {
        Client c;
        for (int attempt = 0; attempt < 50 && P::ConnectLocalSocket(socketPath, c.Connection) != P::LocalSocketStatus::Ok; ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(c.Connection.IsOpen(), "connect");
        auto init = c.Request("initialize", {{"protocolVersion", "2025-06-18"}, {"clientInfo", {{"name", "gtest"}}},
                                                   {"capabilities", Json::object()}});
        check(init["result"]["serverInfo"]["name"] == "intrinsic-sandbox", "initialize: " + init.dump());
        auto tools = c.Request("tools/list");
        check(tools["result"]["tools"].size() >= 15u, "tools/list: " + tools.dump());

        auto entities = c.Tool("scene_entities");
        bool found = false;
        for (const auto& row : entities["entities"])
            found |= row["entity"] == stableId && row["geometry"]["vertices"] == 36;
        check(found, "scene_entities: " + entities.dump());
        auto properties = c.Tool("entity_properties", {{"entity", stableId}});
        bool rough = false;
        for (const auto& row : properties["properties"]) rough |= row["name"] == "rough" && row["kind"] == "double";
        check(rough, "entity_properties: " + properties.dump());

        R::PropertySmoothingConfig smoothing;
        smoothing.Input = {R::GeometryElementDomain::MeshVertex, "rough", Geometry::PropertyValueKind::Double};
        smoothing.Output = {R::GeometryElementDomain::MeshVertex, "smooth", Geometry::PropertyValueKind::Double};
        bool isError = true;
        auto applied = c.Tool("config_apply", {{"section", std::string(R::kPropertySmoothingConfigSectionName)},
                                                     {"payload", Json::parse(R::SerializePropertySmoothingConfig(smoothing))}}, &isError);
        check(!isError && applied["applied"] == true, "config_apply: " + applied.dump());
        auto rejected = c.Tool("config_apply", {{"section", std::string(R::kPropertySmoothingConfigSectionName)},
                                                      {"payload", {{"iterations", 0}}}}, &isError);
        check(isError && !rejected["diagnostics"].empty(), "invalid config is rejected: " + rejected.dump());

        // CORE-010/RUNTIME-276: the generated schema reaches the agent.
        auto sectionSchema = c.Tool("config_schema", {{"section", std::string(R::kPropertySmoothingConfigSectionName)}});
        check(sectionSchema["properties"]["iterations"]["maximum"] == 10000 &&
              sectionSchema["properties"]["method"]["x-enum-names"].size() == 6u,
              "config_schema section: " + sectionSchema.dump().substr(0, 400));
        auto fullSchema = c.Tool("config_schema");
        check(fullSchema["$defs"].contains(std::string(R::kPropertySmoothingConfigSectionName)), "config_schema export");
        auto preview = c.Tool("preview_mesh_operation", {{"operation", "property_smoothing"}, {"entity", stableId}});
        check(preview["enabled"] == true, "preview: " + preview.dump());
        auto run = c.Tool("run_mesh_operation", {{"operation", "property_smoothing"}, {"entity", stableId}}, &isError);
        check(!isError && run["succeeded"] == true, "run: " + run.dump());
        auto shown = c.Tool("show_property", {{"entity", stableId}, {"name", "smooth"}}, &isError);
        check(!isError && shown["domain"] == "MeshVertex", "show_property: " + shown.dump());
        c.Tool("show_property", {{"entity", stableId}, {"name", "missing"}}, &isError);
        check(isError, "show_property rejects unknown properties");
        auto history = c.Tool("history");
        check(history["undo_label"].get<std::string>().starts_with("Agent: "), "history label: " + history.dump());
        // Showing a property is an undoable step too, like the panel's Show button.
        auto undone = c.Tool("undo", {{"steps", 2}});
        check(undone["undone"].size() == 2u, "undo: " + undone.dump());
        // RUNTIME-273: CPD through the configured section; the reply waits for the job.
        auto cpdConfig = c.Tool("config_apply", {{"section", "sandbox.coherent_point_drift"},
            {"payload", {{"source", stableId}, {"target", cloudId}, {"output", 1}, {"outlier_weight", 0.0}}}}, &isError);
        check(!isError, "cpd config_apply: " + cpdConfig.dump());
        auto cpdReady = c.Tool("preview_registration", {{"method", "cpd"}});
        check(cpdReady["enabled"] == true, "preview_registration: " + cpdReady.dump());
        auto cpd = c.Tool("run_registration", {{"method", "cpd"}}, &isError);
        check(!isError && cpd["succeeded"] == true && cpd["iterations"].get<int>() > 0,
              "run_registration cpd: " + cpd.dump());
        check(std::abs(vertices.Get<glm::vec3>("v:position")[0].x - 0.5f) < 1e-2f, "cpd moved the source positions");
        c.Tool("undo");
        auto outside = c.Tool("import_file", {{"path", "/etc/passwd"}}, &isError);
        check(isError, "paths outside the roots are refused");
        // Screenshots need an operational render device; the Null backend refuses with the reason.
        auto screenshot = c.Tool("view_screenshot", {{"region", "window"}}, &isError);
        check(isError && screenshot.dump().find("operational render device") != std::string::npos,
              "view_screenshot on Null: " + screenshot.dump());
        auto unknown = c.Request("tools/call", {{"name", "delete_everything"}});
        check(unknown["error"]["code"] == -32602, "unknown tool");
        done.store(true);
    });

    bool smoothedSeen = false;
    int frameCount = 0;
    frames->OnFrame = [&](R::Engine& kernel) {
        ++frameCount;
        smoothedSeen |= vertices.Exists("smooth");
        if (done.load() || frameCount > 3000) kernel.RequestExit();
    };
    engine.Run();
    client.join();
    for (const auto& failure : failures) ADD_FAILURE() << failure;
    EXPECT_TRUE(done.load()) << "client did not finish";
    EXPECT_TRUE(smoothedSeen) << "the agent's smoothing published its output";
    EXPECT_FALSE(vertices.Exists("smooth")) << "the agent's undo removed it again";
    EXPECT_EQ(server->Status().CallsHandled, 21u) << "one main-thread call per request";
    const auto& lastApply = engine.Services().Find<R::EngineConfigControl>()->GetEngineConfigControlState().LastApply;
    EXPECT_EQ(lastApply.Source, R::RuntimeConfigControlSource::AgentCli) << "config_apply records the agent as the source";
    engine.Shutdown();
    EXPECT_FALSE(std::filesystem::exists(socketPath)) << "the socket file is removed on shutdown";
}

TEST(SandboxAgentServer, ConnectionWindowShowsTheClientAndDisconnectsIt)
{
    const auto socketPath = (std::filesystem::temp_directory_path() /
                             ("intrinsic-agent-ui-" + std::to_string(::getpid()) + ".sock")).string();
    auto sections = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    Config::EngineConfig config{};
    Config::PopulateEngineConfigSectionDefaults(config, sections);
    config.Simulation.WorkerThreadCount = 1u;
    config.ReferenceScene.Enabled = false;
    config.Camera.Enabled = false;
    config.Window.Backend = Config::WindowBackend::Null;
    auto driver = std::make_unique<Driver>();
    Driver* frames = driver.get();
    Intrinsic::Tests::RuntimeTestKernel engine{config, std::move(driver)};
    engine.EmplaceModule<R::EngineConfigControl>(std::move(sections));
    engine.EmplaceModule<R::SceneInteractionModule>();
    engine.EmplaceModule<R::AsyncWorkModule>();
    engine.EmplaceModule<R::EditorUiModule>();
    auto* server = &engine.EmplaceModule<R::AgentServerModule>(R::AgentServerOptions{.SocketPath = socketPath, .ReadOnly = true});
    engine.Initialize();
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    ASSERT_TRUE(shell.SetEditorWindowOpen("view.agent_connection", true)) << "the window exists with --agent-socket";

    Client client;
    std::atomic_bool connected{false}, closed{false};
    std::thread thread([&] {
        for (int attempt = 0; attempt < 50 && P::ConnectLocalSocket(socketPath, client.Connection) != P::LocalSocketStatus::Ok; ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto init = client.Request("initialize", {{"protocolVersion", "2025-06-18"}, {"clientInfo", {{"name", "ui-test"}}}});
        connected.store(init.contains("result"));
        for (int attempt = 0; attempt < 500 && !closed.load(); ++attempt)
            closed.store(client.Connection.Receive(client.Buffer, 20) == P::LocalSocketStatus::Closed);
    });
    int frameCount = 0;
    bool clicked = false;
    frames->OnFrame = [&](R::Engine& kernel) {
        ++frameCount;
        auto* window = ImGui::FindWindowByName("Agent Connection");
        if (window && connected.load() && !clicked && server->Status().ClientName == "ui-test")
        {
            ImGui::ActivateItemByID(window->GetID("Disconnect agent"));
            clicked = true;
        }
        if (closed.load() || frameCount > 2000) kernel.RequestExit();
    };
    engine.Run();
    thread.join();
    EXPECT_TRUE(connected.load());
    EXPECT_TRUE(clicked) << "the window showed the connected client";
    EXPECT_TRUE(closed.load()) << "Disconnect agent closed the client's connection";
    EXPECT_FALSE(server->Status().ClientConnected);
    shell.Detach();
    engine.Shutdown();
}

// UI-062: the Screenshot window and F12 go through the same capture queue as the agent
// tools; on the Null backend the window's Save PNG stays disabled and nothing is queued.
TEST(SandboxScreenshotWindow, SavePngIsDisabledWithoutAnOperationalDevice)
{
    Config::EngineConfig config{};
    config.Simulation.WorkerThreadCount = 1u;
    config.ReferenceScene.Enabled = false;
    config.Camera.Enabled = false;
    config.Window.Backend = Config::WindowBackend::Null;
    auto driver = std::make_unique<Driver>();
    Driver* frames = driver.get();
    Intrinsic::Tests::RuntimeTestKernel engine{config, std::move(driver)};
    engine.EmplaceModule<R::SceneInteractionModule>();
    engine.EmplaceModule<R::EditorUiModule>();
    auto* capture = &engine.EmplaceModule<R::ViewCaptureModule>(std::filesystem::temp_directory_path());
    engine.Initialize();
    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    ASSERT_TRUE(shell.SetEditorWindowOpen("view.screenshot", true));

    int frameCount = 0;
    bool windowSeen = false;
    frames->OnFrame = [&](R::Engine& kernel) {
        ++frameCount;
        if (auto* window = ImGui::FindWindowByName("Screenshot"); window != nullptr && frameCount > 3)
        {
            windowSeen = true;
            ImGui::ActivateItemByID(window->GetID("Save PNG"));
            ImGui::GetIO().AddKeyEvent(ImGuiKey_F12, (frameCount % 2) == 0);
        }
        if (frameCount > 12) kernel.RequestExit();
    };
    engine.Run();
    EXPECT_TRUE(windowSeen);
    EXPECT_TRUE(capture->UnavailableReason().has_value());
    EXPECT_FALSE(capture->LastFinished().has_value()) << "a disabled Save PNG / F12 must not queue captures";
    shell.Detach();
    engine.Shutdown();
}
