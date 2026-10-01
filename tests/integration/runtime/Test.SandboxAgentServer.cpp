// RUNTIME-288: an MCP client drives a running engine through the agent socket: handshake,
// scene and property queries, config apply, a mesh-field run, its "Agent:" undo entry and
// undo, with requests served on the main thread between frames.
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
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
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.Platform.Backend.Null;
import Extrinsic.Platform.LocalSocket;
import Extrinsic.Runtime.AgentServer;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.SpatialIndexCache;
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
namespace TF = Extrinsic::ECS::Components::Transform;
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
        std::vector<Json> Notifications; // server notifications read while waiting for a response
        // One line from the socket, empty when nothing arrives within `attempts` x 20 ms.
        Json ReadLine(int attempts = 500)
        {
            for (int attempt = 0; attempt < attempts; ++attempt)
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
        // Sends a request and returns its id; the response is read with Await.
        int Send(const std::string& method, Json params = Json::object())
        {
            const int id = NextId++;
            const Json request{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}};
            return Connection.SendAll(request.dump() + "\n") == P::LocalSocketStatus::Ok ? id : -1;
        }
        bool Notify(const std::string& method, Json params)
        {
            return Connection.SendAll(Json{{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}}.dump() + "\n") ==
                   P::LocalSocketStatus::Ok;
        }
        std::map<int, Json> Early; // responses that arrived before the one being awaited
        Json Await(int id, int attempts = 500)
        {
            if (const auto early = Early.find(id); early != Early.end())
            {
                Json line = std::move(early->second);
                Early.erase(early);
                return line;
            }
            for (;;)
            {
                Json line = ReadLine(attempts);
                if (line.is_discarded() || line.is_null()) return {};
                if (line.contains("id") && line["id"] == id) return line;
                if (line.contains("id") && line["id"].is_number_integer())
                {
                    const int other = line["id"].get<int>();
                    Early[other] = std::move(line);
                }
                if (line.contains("method") && !line.contains("id")) Notifications.push_back(std::move(line));
            }
        }
        Json Request(const std::string& method, Json params = Json::object())
        {
            const int id = Send(method, std::move(params));
            return id < 0 ? Json{} : Await(id);
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
    // A running engine with the agent server, the editor modules the tools need and a
    // scripted client thread: tests add entities, then Run(script) serves the client.
    struct AgentRig
    {
        std::string SocketPath;
        Driver* Frames{nullptr};
        std::unique_ptr<Intrinsic::Tests::RuntimeTestKernel> Engine;
        R::AgentServerModule* Server{nullptr};
        std::vector<std::string> Failures; // written by the client thread only, read after join
        std::atomic_bool Done{false};

        explicit AgentRig(const std::string& tag)
        {
            SocketPath = (std::filesystem::temp_directory_path() / ("intrinsic-agent-" + tag + "-" + std::to_string(::getpid()) + ".sock")).string();
            auto sections = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
            Config::EngineConfig config{};
            Config::PopulateEngineConfigSectionDefaults(config, sections);
            config.Simulation.WorkerThreadCount = 1u;
            config.ReferenceScene.Enabled = false;
            config.Camera.Enabled = false;
            config.Window.Backend = Config::WindowBackend::Null;
            auto driver = std::make_unique<Driver>();
            Frames = driver.get();
            Engine = std::make_unique<Intrinsic::Tests::RuntimeTestKernel>(config, std::move(driver));
            Engine->EmplaceModule<R::EngineConfigControl>(std::move(sections));
            Engine->EmplaceModule<R::SceneInteractionModule>();
            Engine->EmplaceModule<R::SceneDocumentModule>();
            Engine->EmplaceModule<R::AsyncWorkModule>();
            Engine->EmplaceModule<R::SpatialIndexCache>();
            Engine->EmplaceModule<R::ClusteringModule>();
            Engine->EmplaceModule<R::PointCloudConsolidationModule>();
            Engine->EmplaceModule<R::ViewCaptureModule>(std::filesystem::temp_directory_path());
            Server = &Engine->EmplaceModule<R::AgentServerModule>(
                R::AgentServerOptions{.SocketPath = SocketPath, .AllowedRoots = {std::filesystem::temp_directory_path().string()}});
            Engine->Initialize();
        }
        void Check(bool ok, std::string what) { if (!ok) Failures.push_back(std::move(what)); }
        auto& Scene() { return *Engine->Worlds().Get(Engine->ActiveWorld()); }
        // A point cloud of `count` points on a 6-wide lattice.
        std::uint32_t AddCloud(std::size_t count = 36, bool withTransform = false)
        {
            const auto entity = Scene().Create();
            if (withTransform)
            {
                Scene().Raw().emplace<TF::Component>(entity);
            }
            auto& points = Scene().Raw().emplace<GS::Vertices>(entity).Properties;
            points.Resize(count);
            auto positions = points.GetOrAdd<glm::vec3>("v:position");
            for (std::size_t i = 0; i < count; ++i)
                positions[i] = glm::vec3(float(i % 6) + 0.1f * float(i % 5), float(i / 6), 0.1f * float(i % 3));
            return R::SelectionController::ToStableEntityId(entity);
        }
        // A 6x6 vertex grid mesh (50 triangles) with a vertex scalar "height" (a ridge along x = 2.5).
        std::uint32_t AddGrid()
        {
            const auto entity = Scene().Create();
            Geometry::HalfedgeMesh::Mesh mesh;
            std::vector<Geometry::VertexHandle> v;
            for (int y = 0; y < 6; ++y)
                for (int x = 0; x < 6; ++x) v.push_back(mesh.AddVertex({float(x), float(y), 0.0f}));
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 5; ++x)
                {
                    (void)mesh.AddTriangle(v[std::size_t(y * 6 + x)], v[std::size_t(y * 6 + x + 1)], v[std::size_t((y + 1) * 6 + x + 1)]);
                    (void)mesh.AddTriangle(v[std::size_t(y * 6 + x)], v[std::size_t((y + 1) * 6 + x + 1)], v[std::size_t((y + 1) * 6 + x)]);
                }
            GS::PopulateFromMesh(Scene().Raw(), entity, mesh);
            auto& vertices = Scene().Raw().get<GS::Vertices>(entity).Properties;
            auto height = vertices.GetOrAdd<double>("height", 0.0);
            for (std::size_t i = 0; i < vertices.Size(); ++i) height[i] = 1.0 - std::abs(double(i % 6) - 2.5) * 0.3;
            return R::SelectionController::ToStableEntityId(entity);
        }
        // Serves `script` on a client thread (already initialized) until it returns.
        void Run(const std::function<void(Client&)>& script, std::chrono::seconds limit = std::chrono::seconds(60))
        {
            std::thread client([&] {
                Client c;
                for (int attempt = 0; attempt < 50 && P::ConnectLocalSocket(SocketPath, c.Connection) != P::LocalSocketStatus::Ok; ++attempt)
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                Check(c.Connection.IsOpen(), "connect");
                Check(c.Request("initialize", {{"protocolVersion", "2025-06-18"}, {"capabilities", Json::object()}}).contains("result"),
                      "initialize");
                script(c);
                Done.store(true);
            });
            const auto deadline = std::chrono::steady_clock::now() + limit;
            Frames->OnFrame = [&](R::Engine& kernel) {
                if (Done.load() || std::chrono::steady_clock::now() > deadline) kernel.RequestExit();
            };
            Engine->Run();
            client.join();
            for (const auto& failure : Failures) ADD_FAILURE() << failure;
            EXPECT_TRUE(Done.load()) << "client did not finish";
            Engine->Shutdown();
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
        const glm::vec3 beforeCpd = std::as_const(vertices).Get<glm::vec3>("v:position")[0];
        auto cpd = c.Tool("run_registration", {{"method", "cpd"}}, &isError);
        check(!isError && cpd["succeeded"] == true && cpd["iterations"].get<int>() > 0,
              "run_registration cpd: " + cpd.dump());
        check(cpd["variant"] == "rigid" && cpd["output"] == "positions", "cpd names its variant and output: " + cpd.dump());
        check(std::abs(std::as_const(vertices).Get<glm::vec3>("v:position")[0].x - 0.5f) < 1e-2f,
              "cpd moved the source positions");
        auto cpdUndo = c.Tool("undo", {}, &isError);
        check(!isError && cpdUndo["undone"].size() == 1u, "cpd undo: " + cpdUndo.dump());
        check(std::as_const(vertices).Get<glm::vec3>("v:position")[0] == beforeCpd, "undo restored the source positions");
        // RUNTIME-274: standalone point sampling through its section.
        auto samplingConfig = c.Tool("config_apply", {{"section", "sandbox.point_sampling"},
            {"payload", {{"source", stableId}, {"count", 8}}}}, &isError);
        check(!isError, "point sampling config_apply: " + samplingConfig.dump());
        check(c.Tool("preview_point_sampling")["enabled"] == true, "preview_point_sampling");
        auto sampled = c.Tool("run_point_sampling", Json::object(), &isError);
        check(!isError && sampled["succeeded"] == true && sampled["samples"] == 8, "run_point_sampling: " + sampled.dump());
        check(std::as_const(vertices).Exists("v:sample_rank"), "sampling published the rank property");
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
    EXPECT_EQ(server->Status().CallsHandled, 25u) << "one main-thread call per request";
    const auto& lastApply = engine.Services().Find<R::EngineConfigControl>()->GetEngineConfigControlState().LastApply;
    EXPECT_EQ(lastApply.Source, R::RuntimeConfigControlSource::AgentCli) << "config_apply records the agent as the source";
    engine.Shutdown();
    EXPECT_FALSE(std::filesystem::exists(socketPath)) << "the socket file is removed on shutdown";
}

// RUNTIME-312 slice 5: a CPD call with a progress token streams notifications/progress, and
// notifications/cancelled drops the pending reply while the connection stays usable.
TEST(SandboxAgentServer, ProgressAndCancelOverTheSocket)
{
    const auto socketPath = (std::filesystem::temp_directory_path() /
                             ("intrinsic-agent-prog-" + std::to_string(::getpid()) + ".sock")).string();
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
        R::AgentServerOptions{.SocketPath = socketPath, .AllowedRoots = {std::filesystem::temp_directory_path().string()},
                              .ProgressInterval = std::chrono::milliseconds(0)});
    engine.Initialize();
    ASSERT_TRUE(server->Status().Listening) << server->Status().LastError;

    // Two 1200-point clouds: enough EM work to span several frames.
    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const auto makeCloud = [&](float shift) {
        const auto entity = scene.Create();
        auto& points = scene.Raw().emplace<GS::Vertices>(entity).Properties;
        points.Resize(1200);
        auto positions = points.GetOrAdd<glm::vec3>("v:position");
        for (std::size_t i = 0; i < 1200; ++i)
            positions[i] = glm::vec3(float(i % 40) * 0.1f + shift, float((i / 40) % 30) * 0.1f, float(i % 7) * 0.05f);
        return R::SelectionController::ToStableEntityId(entity);
    };
    const auto sourceId = makeCloud(0.0f);
    const auto targetId = makeCloud(0.05f);

    std::atomic_bool done{false};
    std::vector<std::string> failures;
    const auto check = [&](bool ok, std::string what) { if (!ok) failures.push_back(std::move(what)); };
    std::thread client([&] {
        Client c;
        for (int attempt = 0; attempt < 50 && P::ConnectLocalSocket(socketPath, c.Connection) != P::LocalSocketStatus::Ok; ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(c.Connection.IsOpen(), "connect");
        check(c.Request("initialize", {{"protocolVersion", "2025-06-18"}, {"capabilities", Json::object()}}).contains("result"), "initialize");
        // No warm-up: the first call of a session may be a feature preview (frames are prepared per call).
        bool firstError = true;
        c.Tool("config_apply", {{"section", "sandbox.coherent_point_drift"},
            {"payload", {{"source", sourceId}, {"target", targetId}, {"output", 1}, {"outlier_weight", 0.0}}}}, &firstError);
        check(!firstError, "first config_apply");
        const auto firstPreview = c.Tool("preview_registration", {{"method", "cpd"}}, &firstError);
        check(!firstError && firstPreview["enabled"] == true, "preview_registration as the first feature call: " + firstPreview.dump());
        const auto configure = [&](int iterations) {
            bool isError = true;
            const auto applied = c.Tool("config_apply", {{"section", "sandbox.coherent_point_drift"},
                {"payload", {{"source", sourceId}, {"target", targetId}, {"output", 1}, {"outlier_weight", 0.0},
                             {"max_iterations", iterations}}}}, &isError);
            check(!isError && applied["applied"] == true, "cpd config_apply: " + applied.dump());
        };

        // Progress: the run reports with its token, then replies with the same id.
        configure(40);
        const int runId = c.Send("tools/call", {{"name", "run_registration"}, {"arguments", {{"method", "cpd"}}},
                                                {"_meta", {{"progressToken", "run-1"}}}});
        const auto reply = c.Await(runId);
        check(reply.contains("result") && reply["result"]["isError"] == false, "run_registration reply: " + reply.dump());
        check(!c.Notifications.empty(), "at least one progress notification before the reply");
        double last = -1.0;
        for (const auto& note : c.Notifications)
        {
            check(note["method"] == "notifications/progress" && note["params"]["progressToken"] == "run-1", "note shape: " + note.dump());
            check(note["params"]["progress"].get<double>() > last, "progress strictly increases");
            check(!note["params"].contains("total") || note["params"]["progress"].get<double>() <= note["params"]["total"].get<double>(),
                  "progress stays within total: " + note.dump());
            last = note["params"]["progress"].get<double>();
        }
        c.Notifications.clear();

        // No token, no notifications.
        configure(10);
        bool isError = true;
        const auto untokened = c.Tool("run_registration", {{"method", "cpd"}}, &isError);
        check(!isError && c.Notifications.empty(), "no progress without a token: " + untokened.dump() +
              (c.Notifications.empty() ? "" : c.Notifications.front().dump()));

        // Cancel: the reply never arrives, the connection keeps working.
        configure(60);
        const int cancelledId = c.Send("tools/call", {{"name", "run_registration"}, {"arguments", {{"method", "cpd"}}}});
        check(c.Notify("notifications/cancelled", {{"requestId", cancelledId}}), "send cancel");
        const int pingId = c.Send("ping");
        check(c.Await(pingId).contains("result"), "ping is answered after the cancel");
        // Wait (bounded by wall clock) until no job is active, then make sure no reply surfaced.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        bool idle = false;
        while (!idle && std::chrono::steady_clock::now() < deadline)
        {
            const auto jobs = c.Tool("jobs");
            idle = true;
            for (const auto& job : jobs.value("jobs", Json::array()))
            {
                const auto state = job.value("state", std::string{});
                idle &= state != "queued" && state != "running" && state != "awaiting-dependencies" &&
                        state != "awaiting-gate" && state != "awaiting-apply";
            }
            if (!idle) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        check(idle, "the cancelled job finished within the time limit");
        for (int i = 0; i < 10; ++i)
        {
            const auto line = c.ReadLine(1);
            if (line.is_object() && line.contains("id"))
                check(line["id"] != cancelledId, "the cancelled call was answered: " + line.dump());
        }
        done.store(true);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    frames->OnFrame = [&](R::Engine& kernel) {
        if (done.load() || std::chrono::steady_clock::now() > deadline) kernel.RequestExit();
    };
    engine.Run();
    client.join();
    for (const auto& failure : failures) ADD_FAILURE() << failure;
    EXPECT_TRUE(done.load()) << "client did not finish";
    engine.Shutdown();
}

namespace
{
    // Idle-phase probe: minimized frames skip the Driver's UiBuild hook, so exit is decided here.
    class IdleExit final : public R::IRuntimeModule
    {
    public:
        IdleExit(std::atomic_bool& done, R::Engine& engine) : m_Done(done), m_Engine(engine) {}
        [[nodiscard]] std::string_view Name() const noexcept override { return "Z.IdleExit"; }
        [[nodiscard]] Extrinsic::Core::Result OnRegister(R::EngineSetup& setup) override
        {
            return setup.RegisterFrameHook(R::FramePhase::Idle, [this](R::RuntimeFrameHookContext&) {
                // The Null window does not sleep while minimized, so bound by time, not frames.
                if (m_Done.load() || std::chrono::steady_clock::now() > m_Deadline) m_Engine.RequestExit();
            });
        }
        void OnShutdown(R::RuntimeModuleShutdownContext&) override {}

    private:
        std::atomic_bool& m_Done;
        R::Engine& m_Engine;
        std::chrono::steady_clock::time_point m_Deadline{std::chrono::steady_clock::now() + std::chrono::seconds(30)};
    };
}

TEST(SandboxAgentServer, MinimizedSandboxStillServesCallsAndCapturesFailFast)
{
    const auto socketPath = (std::filesystem::temp_directory_path() /
                             ("intrinsic-agent-min-" + std::to_string(::getpid()) + ".sock")).string();
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
    std::atomic_bool done{false};
    engine.EmplaceModule<IdleExit>(done, engine);
    engine.Initialize();
    ASSERT_TRUE(server->Status().Listening) << server->Status().LastError;

    std::vector<std::string> failures;
    const auto check = [&](bool ok, std::string what) { if (!ok) failures.push_back(std::move(what)); };
    std::thread client([&] {
        Client c;
        for (int attempt = 0; attempt < 50 && P::ConnectLocalSocket(socketPath, c.Connection) != P::LocalSocketStatus::Ok; ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(c.Connection.IsOpen(), "connect");
        // The first frame minimizes the window, so every call below is served by Idle frames.
        auto init = c.Request("initialize", {{"protocolVersion", "2025-06-18"}, {"capabilities", Json::object()}});
        check(init["result"]["serverInfo"]["name"] == "intrinsic-sandbox", "initialize: " + init.dump());
        bool isError = true;
        auto entities = c.Tool("scene_entities", Json::object(), &isError);
        check(!isError && entities.contains("entities"), "scene_entities while minimized: " + entities.dump());
        auto shot = c.Request("tools/call", {{"name", "view_screenshot"}, {"arguments", Json::object()}});
        check(shot.contains("result") && shot["result"].contains("structuredContent") &&
              shot["result"]["structuredContent"]["error"]["code"] == "viewport_not_presentable",
              "capture fails fast while minimized: " + shot.dump());
        done.store(true);
    });
    int uiFrames = 0;
    frames->OnFrame = [&](R::Engine& kernel) {
        if (++uiFrames == 1)
            static_cast<P::Backends::Null::NullWindow&>(kernel.GetWindow()).QueueResize(0, 0);
        if (uiFrames > 3000) kernel.RequestExit();
    };
    engine.Run();
    client.join();
    for (const auto& failure : failures) ADD_FAILURE() << failure;
    EXPECT_TRUE(done.load()) << "client did not finish";
    EXPECT_EQ(uiFrames, 1) << "the window stayed minimized";
    EXPECT_EQ(server->Status().CallsHandled, 3u);
    engine.Shutdown();
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

// RUNTIME-312 slice 6: keypoint, k-means and consolidation previews answer with the panels'
// readiness (behind the same config-lane gate).
TEST(SandboxAgentServer, PreviewKMeansAndConsolidationReportReadiness)
{
    AgentRig rig("ready");
    ASSERT_TRUE(rig.Server->Status().Listening) << rig.Server->Status().LastError;
    const auto cloud = rig.AddCloud();
    rig.Run([&](Client& c) {
        bool isError = true;
        const auto kmeans = c.Tool("preview_kmeans", {{"entity", cloud}, {"domain", "PointCloudPoint"}}, &isError);
        rig.Check(!isError && kmeans["enabled"] == true && kmeans["reason"] == "", "preview_kmeans: " + kmeans.dump());
        const auto missing = c.Tool("preview_kmeans", {{"entity", 999999}, {"domain", "PointCloudPoint"}}, &isError);
        rig.Check(!isError && missing["enabled"] == false && !missing["reason"].get<std::string>().empty(),
                  "preview_kmeans on a missing entity explains itself: " + missing.dump());
        c.Tool("preview_kmeans", {{"entity", cloud}, {"domain", "Bogus"}}, &isError);
        rig.Check(isError, "an unknown domain is a call error");
        // The first preview starts the validation scan (pending); a later one is ready.
        Json consolidation;
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            consolidation = c.Tool("preview_point_cloud_consolidation", {{"entity", cloud}, {"domain", "PointCloudPoint"}}, &isError);
            if (isError || consolidation["pending"] == false) break;
            rig.Check(consolidation["enabled"] == false && !consolidation["reason"].get<std::string>().empty(),
                      "a pending scan explains itself: " + consolidation.dump());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        rig.Check(!isError && consolidation["pending"] == false && consolidation["input_points"] == 36,
                  "preview_point_cloud_consolidation: " + consolidation.dump());
        // A property the entity lacks is a settled "no", not a pending scan.
        Json absent;
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            absent = c.Tool("preview_point_cloud_consolidation",
                            {{"entity", cloud}, {"domain", "PointCloudPoint"}, {"positions", "no_such_property"}}, &isError);
            if (isError || absent["pending"] == false) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        rig.Check(!isError && absent["enabled"] == false && absent["pending"] == false && !absent["reason"].get<std::string>().empty(),
                  "consolidation preview of a missing property is disabled with a reason: " + absent.dump());
        const auto noEntity = c.Tool("preview_point_cloud_consolidation", {{"domain", "PointCloudPoint"}}, &isError);
        rig.Check(isError, "consolidation preview needs an entity: " + noEntity.dump());
        const auto unconfigured = c.Tool("preview_keypoint_analysis", Json::object(), &isError);
        rig.Check(!isError && unconfigured["enabled"] == false && !unconfigured["reason"].get<std::string>().empty(),
                  "keypoint preview before config_apply explains itself: " + unconfigured.dump());
        c.Tool("config_apply", {{"section", "sandbox.keypoint_analysis"}, {"payload", {{"entity", cloud}}}}, &isError);
        rig.Check(!isError, "keypoint config_apply");
        // Input validation runs for a few frames before the preview turns ready.
        Json keypoints;
        for (int attempt = 0; attempt < 200 && keypoints["enabled"] != true; ++attempt)
        {
            keypoints = c.Tool("preview_keypoint_analysis", Json::object(), &isError);
            if (isError) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        rig.Check(!isError && keypoints["enabled"] == true && keypoints["reason"] == "",
                  "preview_keypoint_analysis: " + keypoints.dump());
    });
}

// Two identical ICP runs requested back to back: the second finds the first still active, gets no
// callback of its own and must end with result_unavailable instead of waiting forever.
TEST(SandboxAgentServer, DuplicateRunEndsWithResultUnavailable)
{
    AgentRig rig("dup");
    ASSERT_TRUE(rig.Server->Status().Listening) << rig.Server->Status().LastError;
    const auto source = rig.AddCloud(1200, true);
    const auto target = rig.AddCloud(1200);
    rig.Run([&](Client& c) {
        bool isError = true;
        const auto configured = c.Tool("config_apply", {{"section", "sandbox.registration"},
            {"payload", {{"source_entity", source}, {"target_entity", target}, {"max_iterations", 4000},
                         {"convergence_threshold", 1e-12}}}}, &isError);
        rig.Check(!isError, "icp config_apply: " + configured.dump());
        const Json call{{"name", "run_registration"}, {"arguments", {{"method", "icp"}}}};
        const int first = c.Send("tools/call", call);
        const int second = c.Send("tools/call", call);
        const auto a = c.Await(first, 2500), b = c.Await(second, 2500);
        rig.Check(a.contains("result") && b.contains("result"), "both calls are answered: " + a.dump() + " " + b.dump());
        const auto unavailable = [](const Json& reply) {
            if (!reply.contains("result")) return false;
            const auto structured = reply["result"].value("structuredContent", Json::object());
            return structured.contains("error") && structured["error"]["code"] == "result_unavailable";
        };
        rig.Check(unavailable(b) && !unavailable(a), "the duplicate ends with result_unavailable: " + a.dump().substr(0, 200) + " | " + b.dump().substr(0, 300));
    }, std::chrono::seconds(60));
}

// RUNTIME-312 slice 7: run_operation on a config-sourced operation, end to end: config, preview,
// run, the properties it published, and its single undo step.
TEST(SandboxAgentServer, RunOperationOutlierAnalysisPublishesAndUndoes)
{
    AgentRig rig("outlier");
    ASSERT_TRUE(rig.Server->Status().Listening) << rig.Server->Status().LastError;
    const auto cloud = rig.AddCloud(64);
    rig.Run([&](Client& c) {
        const auto hasProperty = [&](const char* name) {
            for (const auto& row : c.Tool("entity_properties", {{"entity", cloud}})["properties"])
                if (row["name"] == name) return true;
            return false;
        };
        bool isError = true;
        const auto configured = c.Tool("config_apply", {{"section", "sandbox.outlier_analysis"}, {"payload", {{"entity", cloud}}}}, &isError);
        rig.Check(!isError && configured["applied"] == true, "outlier config_apply: " + configured.dump());
        // The entity comes from the section: an argument is refused with the section named.
        const auto refused = c.Tool("run_operation", {{"operation", "outlier_analysis"}, {"entity", cloud}}, &isError);
        rig.Check(isError && refused.dump().find("sandbox.outlier_analysis") != std::string::npos, "entity refused: " + refused.dump());
        // Input validation runs for a few frames before the preview turns ready.
        Json preview;
        for (int attempt = 0; attempt < 200 && preview["enabled"] != true; ++attempt)
        {
            preview = c.Tool("preview_operation", {{"operation", "outlier_analysis"}}, &isError);
            if (isError) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        rig.Check(!isError && preview["enabled"] == true && preview["operation"] == "outlier_analysis", "preview_operation: " + preview.dump());
        rig.Check(!hasProperty("outlier_score"), "nothing published before the run");
        const auto run = c.Tool("run_operation", {{"operation", "outlier_analysis"}}, &isError);
        rig.Check(!isError && run["succeeded"] == true && run["operation"] == "outlier_analysis", "run_operation: " + run.dump());
        rig.Check(hasProperty("outlier_score") && hasProperty("outlier_mask"), "the run published its properties");
        const auto history = c.Tool("history");
        // The queued job publishes on a later frame; its entry still carries the call's label.
        rig.Check(history["undo_count"] == 1 && history["undo_label"].get<std::string>().starts_with("Agent: "),
                  "labeled history entry: " + history.dump());
        const auto undone = c.Tool("undo", Json::object(), &isError);
        rig.Check(!isError && undone["undone"].size() == 1u, "one undo step: " + undone.dump());
        rig.Check(!hasProperty("outlier_score") && !hasProperty("outlier_mask"), "undo removed the properties");
        // Geodesics reports the panel's readiness: no source vertex yet, so it is disabled with a reason.
        const auto geodesics = c.Tool("preview_operation", {{"operation", "geodesics"}, {"entity", cloud}}, &isError);
        rig.Check(!isError && geodesics["enabled"] == false && geodesics["reason"].get<std::string>().find("source") != std::string::npos,
                  "geodesics preview: " + geodesics.dump());
        // Parameterization has no readiness function and says so instead of inventing one.
        const auto noCheck = c.Tool("preview_operation", {{"operation", "parameterization"}, {"entity", cloud}}, &isError);
        rig.Check(!isError && noCheck["enabled"].is_null() && !noCheck["reason"].get<std::string>().empty(), "parameterization preview: " + noCheck.dump());
        // Every Config row names a real section.
        for (const char* op : {"mesh_curvature", "normal_estimation", "kernel_density", "point_spacing", "outlier_analysis",
                               "density_weight", "descriptor_analysis", "bilateral_filter", "point_construction"})
        {
            const auto refusal = c.Tool("preview_operation", {{"operation", op}, {"entity", cloud}}, &isError);
            const auto text = refusal.is_string() ? refusal.get<std::string>() : refusal.dump();
            const auto open = text.find("section '");
            rig.Check(isError && open != std::string::npos, std::string(op) + " refuses an entity: " + text);
            if (open == std::string::npos) continue;
            const auto name = text.substr(open + 9, text.find('\'', open + 9) - open - 9);
            bool sectionError = true;
            c.Tool("config_get", {{"section", name}}, &sectionError);
            rig.Check(!sectionError, std::string(op) + " reads the registered section " + name);
        }
    });
}

// RUNTIME-312 slice 7D: explicit-parameter operations over the command defaults, with undo, and
// typed errors for bad params and for values outside the ranges the command owners declare.
TEST(SandboxAgentServer, RunOperationTakesExplicitParamsAndUndoes)
{
    AgentRig rig("params");
    ASSERT_TRUE(rig.Server->Status().Listening) << rig.Server->Status().LastError;
    const auto grid = rig.AddGrid();
    const auto cloud = rig.AddCloud(64);
    rig.Run([&](Client& c) {
        const auto faces = [&] {
            for (const auto& row : c.Tool("scene_entities")["entities"])
                if (row["entity"] == grid) return row["geometry"]["faces"].get<int>();
            return -1;
        };
        const auto entityCount = [&] { return c.Tool("scene_entities")["entities"].size(); };
        const auto undoCount = [&] { return c.Tool("history")["undo_count"].get<int>(); };
        bool isError = true;
        const auto invalid = [&](const Json& arguments, const std::string& what) {
            const auto reply = c.Request("tools/call", {{"name", "run_operation"}, {"arguments", arguments}});
            const bool ok = reply["result"]["isError"] == true && reply["result"]["structuredContent"]["error"]["code"] == "invalid_params";
            rig.Check(ok, what + ": " + reply.dump().substr(0, 400));
            return reply["result"]["content"][0]["text"].get<std::string>();
        };
        rig.Check(faces() == 50, "the grid has 50 triangles");
        // Bad params are typed errors before anything runs.
        const auto type = invalid({{"operation", "mesh_simplify"}, {"entity", grid}, {"params", {{"target_faces", "many"}}}}, "param type");
        rig.Check(type.find("target_faces") != std::string::npos, "the message names the parameter: " + type);
        invalid({{"operation", "mesh_simplify"}, {"entity", grid}, {"params", {{"faces", 5}}}}, "unknown param");
        invalid({{"operation", "mesh_simplify"}, {"entity", grid}, {"params", {{"metric", "bogus"}}}}, "enum value outside the list");
        // Ranges are the command owners': the agent cannot ask for more than the panel's controls allow.
        const auto range = invalid({{"operation", "mesh_subdivide"}, {"entity", grid}, {"params", {{"iterations", 30}}}}, "subdivide iterations");
        rig.Check(range.find("1 to 10") != std::string::npos, "the range is reported: " + range);
        invalid({{"operation", "mesh_remesh"}, {"entity", grid}, {"params", {{"iterations", 65}}}}, "remesh iterations");
        invalid({{"operation", "mesh_denoise"}, {"entity", grid}, {"params", {{"vertex_iterations", 5000}}}}, "denoise iterations");
        invalid({{"operation", "mesh_denoise"}, {"entity", grid}, {"params", {{"sigma_spatial", -1.0}}}}, "negative sigma");
        invalid({{"operation", "scalar_ridge"}, {"entity", grid}, {"params", {{"radius_ratio", 0.5}}}}, "ridge radius");
        c.Tool("run_operation", {{"operation", "outlier_analysis"}, {"params", {{"k", 1}}}}, &isError);
        rig.Check(isError, "a config-backed operation takes no params");
        rig.Check(faces() == 50, "refused calls changed nothing");
        // The ranges and defaults are discoverable in the schema.
        const auto tools = c.Request("tools/list");
        for (const auto& tool : tools["result"]["tools"])
        {
            if (tool["name"] != "run_operation") continue;
            std::string rules = tool["inputSchema"]["allOf"].dump();
            rig.Check(rules.find("\"maximum\":10") != std::string::npos && rules.find("x-enum-names") != std::string::npos &&
                          rules.find("\"const\":\"mesh_subdivide\"") != std::string::npos,
                      "per-operation params schema in allOf: " + rules.substr(0, 300));
        }

        // scalar_ridge: the property comes from the catalog; the graph is a new entity.
        const auto before = entityCount();
        const int undoBefore = undoCount();
        const auto ridgeArgs = Json{{"operation", "scalar_ridge"}, {"entity", grid},
            {"params", {{"property", "height"}, {"radius_ratio", 0.25}, {"scale", 2}, {"minimum_sharpness", 0.0}, {"minimum_strength", 0.0}}}};
        const auto missing = c.Tool("run_operation", {{"operation", "scalar_ridge"}, {"entity", grid}, {"params", {{"property", "nope"}}}}, &isError);
        rig.Check(isError && missing.dump().find("nope") != std::string::npos, "unknown ridge property: " + missing.dump());
        const auto ridgePreview = c.Tool("preview_operation", ridgeArgs, &isError);
        rig.Check(!isError && ridgePreview["enabled"] == true, "scalar_ridge has the panel's readiness: " + ridgePreview.dump());
        const auto noOutput = c.Tool("preview_operation", {{"operation", "scalar_ridge"}, {"entity", grid},
            {"params", {{"property", "height"}, {"publish_graph", false}}}}, &isError);
        rig.Check(!isError && noOutput["enabled"] == false && !noOutput["reason"].get<std::string>().empty(),
                  "no output selected is disabled with a reason: " + noOutput.dump());
        const auto ridge = c.Tool("run_operation", ridgeArgs, &isError);
        rig.Check(!isError && ridge["succeeded"] == true, "scalar_ridge: " + ridge.dump());
        rig.Check(entityCount() == before + 1, "the ridge graph is a new entity");
        // Publishing only the graph is one undo step; mesh features add a second one (documented).
        rig.Check(undoCount() - undoBefore == 1, "graph-only ridge is one undo step, not " + std::to_string(undoCount() - undoBefore));
        const auto ridgeUndo = c.Tool("undo", Json::object(), &isError);
        rig.Check(!isError && entityCount() == before, "undo removed the graph entity: " + ridgeUndo.dump());
        auto featureArgs = ridgeArgs;
        featureArgs["params"]["publish_mesh_features"] = true;
        const int featuresBefore = undoCount();
        const auto features = c.Tool("run_operation", featureArgs, &isError);
        rig.Check(!isError && features["succeeded"] == true, "scalar_ridge with mesh features: " + features.dump());
        const int featureSteps = undoCount() - featuresBefore;
        rig.Check(featureSteps == 2, "graph plus mesh features are two undo steps, not " + std::to_string(featureSteps));
        const auto featureUndo = c.Tool("undo", {{"steps", featureSteps}}, &isError);
        rig.Check(!isError && entityCount() == before, "undoing both steps removes the graph again: " + featureUndo.dump());

        // Topology operations: each is one undo step that restores the face count.
        // (The mesh's custom vertex properties do not survive a topology undo: BUG-230. The ridge
        // runs above come first for that reason, and nothing below reads "height".)
        const auto topology = [&](const char* operation, const Json& params, const std::function<bool(int)>& expected) {
            const auto preview = c.Tool("preview_operation", {{"operation", operation}, {"entity", grid}, {"params", params}}, &isError);
            rig.Check(!isError && preview["enabled"].is_boolean(), std::string("preview ") + operation + ": " + preview.dump());
            const auto run = c.Tool("run_operation", {{"operation", operation}, {"entity", grid}, {"params", params}}, &isError);
            rig.Check(!isError && run["succeeded"] == true && run["input_faces"] == 50 || std::string(operation) == "mesh_denoise",
                      std::string(operation) + ": " + run.dump());
            rig.Check(expected(faces()), std::string(operation) + " left " + std::to_string(faces()) + " faces");
            const auto undone = c.Tool("undo", Json::object(), &isError);
            rig.Check(!isError && undone["undone"].size() == 1u, std::string(operation) + " is one undo step: " + undone.dump());
            rig.Check(faces() == 50, std::string("undo of ") + operation + " restored the mesh");
        };
        topology("mesh_simplify", {{"target_faces", 20}}, [](int f) { return f < 50 && f >= 1; });
        topology("mesh_subdivide", {{"iterations", 1}}, [](int f) { return f == 200; });
        topology("mesh_remesh", {{"iterations", 1}, {"target_edge_length", 1.5}}, [](int f) { return f > 0 && f != 50; });
        // Denoise of a flat grid moves nothing (NoChange), so only the reply is asserted.
        const auto denoise = c.Tool("run_operation", {{"operation", "mesh_denoise"}, {"entity", grid}, {"params", {{"normal_iterations", 2}, {"vertex_iterations", 2}}}}, &isError);
        rig.Check(denoise.is_object() && denoise["operation"] == "mesh_denoise", "mesh_denoise answers: " + denoise.dump());
        // Section-backed rows answer too: progressive Poisson on a cloud, parameterization on the mesh.
        const auto poisson = c.Tool("run_operation", {{"operation", "progressive_poisson"}, {"entity", cloud}}, &isError);
        rig.Check(poisson.is_object() && poisson["operation"] == "progressive_poisson", "progressive_poisson answers: " + poisson.dump());
        const auto uv = c.Tool("run_operation", {{"operation", "parameterization"}, {"entity", grid}}, &isError);
        rig.Check(uv.is_object() && uv["operation"] == "parameterization", "parameterization answers: " + uv.dump());
    });
}
