// ARCH-006 Slice 4 app/runtime composition coverage.
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <glm/vec4.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Sandbox.Editor.DomainPanels;
import Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Geometry.PointCloud;

namespace Core = Extrinsic::Core;
namespace Runtime = Extrinsic::Runtime;
namespace Editor = Extrinsic::Sandbox::Editor;

namespace
{
    class OneFrameApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        std::function<void(Runtime::Engine&)> OnFrame{};
        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            if (OnFrame)
                OnFrame(engine);
            else
                engine.RequestExit();
        }
        void Shutdown() override {}
    };

    [[nodiscard]] Core::Config::EngineConfig HeadlessConfig()
    {
        Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }

    struct EditorUiShellHarness
    {
        Intrinsic::Tests::RuntimeTestKernel Kernel{HeadlessConfig(),
                                                   std::make_unique<OneFrameApplication>()};
        Editor::EditorShell Shell{};

        EditorUiShellHarness()
        {
            Kernel.EmplaceModule<Runtime::EditorUiModule>();
            Kernel.Initialize();
            Shell.Attach(Kernel.Worlds(), Kernel.Services());
        }

        ~EditorUiShellHarness()
        {
            Shell.Detach();
            Kernel.Shutdown();
        }
    };

    [[nodiscard]] const Runtime::EditorWindowMenuEntry* FindWindow(
        const std::vector<Runtime::EditorWindowMenuEntry>& menu,
        const std::string_view id)
    {
        const auto found = std::find_if(
            menu.begin(), menu.end(),
            [id](const Runtime::EditorWindowMenuEntry& entry)
            {
                return entry.Id == id;
            });
        return found == menu.end() ? nullptr : &*found;
    }
}

TEST(SandboxDomainPanels, RegistersTheNineAppOwnedWindowsWithStableMenuMetadata)
{
    struct ExpectedWindow
    {
        std::string_view Id;
        std::vector<std::string> MenuPath;
        std::string_view Title;
    };
    const std::array<ExpectedWindow, 9> expected{{
        {"pointcloud.appearance", {"PointCloud"}, "Appearance"},
        {"pointcloud.properties", {"PointCloud"}, "Properties"},
        {"pointcloud.selection", {"PointCloud"}, "Selection"},
        {"graph.appearance", {"Graph"}, "Appearance"},
        {"graph.properties", {"Graph"}, "Properties"},
        {"graph.selection", {"Graph"}, "Selection"},
        {"mesh.appearance", {"Mesh"}, "Appearance"},
        {"mesh.properties", {"Mesh"}, "Properties"},
        {"mesh.selection", {"Mesh"}, "Selection"},
    }};

    EditorUiShellHarness harness;
    Editor::DomainPanels panels;
    panels.Register(harness.Shell);

    const auto menu = harness.Shell.BuildEditorWindowMenuModel();
    ASSERT_EQ(menu.size(), expected.size() + 10u);
    for (const ExpectedWindow& expectedWindow : expected)
    {
        const Runtime::EditorWindowMenuEntry* entry =
            FindWindow(menu, expectedWindow.Id);
        ASSERT_NE(entry, nullptr) << expectedWindow.Id;
        EXPECT_EQ(entry->MenuPath, expectedWindow.MenuPath) << expectedWindow.Id;
        EXPECT_EQ(entry->Title, expectedWindow.Title) << expectedWindow.Id;
        EXPECT_FALSE(entry->Open) << expectedWindow.Id;
    }
}

TEST(SandboxDomainPanels, RegistrationIsIdempotentAndLifetimeUnregistersEveryWindow)
{
    EditorUiShellHarness first;
    EditorUiShellHarness second;

    {
        Editor::DomainPanels panels;
        panels.Register(first.Shell);
        ASSERT_EQ(first.Shell.BuildEditorWindowMenuModel().size(), 19u);

        panels.Register(first.Shell);
        EXPECT_EQ(first.Shell.BuildEditorWindowMenuModel().size(), 19u);

        panels.Register(second.Shell);
        EXPECT_EQ(first.Shell.BuildEditorWindowMenuModel().size(), 10u);
        EXPECT_EQ(second.Shell.BuildEditorWindowMenuModel().size(), 19u);

        panels.Unregister();
        EXPECT_EQ(second.Shell.BuildEditorWindowMenuModel().size(), 10u);

        panels.Register(second.Shell);
        ASSERT_EQ(second.Shell.BuildEditorWindowMenuModel().size(), 19u);
    }

    EXPECT_EQ(first.Shell.BuildEditorWindowMenuModel().size(), 10u);
    EXPECT_EQ(second.Shell.BuildEditorWindowMenuModel().size(), 10u);
}

TEST(SandboxDomainPanels, ClosedRegisteredWindowsBuildNoDomainModels)
{
    EditorUiShellHarness harness;
    Editor::DomainPanels panels;
    panels.Register(harness.Shell);
    harness.Kernel.Run();

    EXPECT_EQ(
        harness.Shell.GetLastFrame().ModelBuildStats.DomainWindowModelBuilds,
        0u);
    EXPECT_EQ(
        harness.Shell.GetLastFrame().ModelBuildStats.DomainWindowModelCacheHits,
        0u);
}

TEST(SandboxDomainPanels, OpenSameDomainWindowsShareOneModelBuildPerFrame)
{
    EditorUiShellHarness harness;
    Editor::DomainPanels panels;
    panels.Register(harness.Shell);
    for (const std::string_view id :
         {"pointcloud.appearance",
          "pointcloud.properties",
          "pointcloud.selection"})
    {
        ASSERT_TRUE(harness.Shell.SetEditorWindowOpen(id, true)) << id;
    }

    harness.Kernel.Run();

    EXPECT_EQ(
        harness.Shell.GetLastFrame().ModelBuildStats.DomainWindowModelBuilds,
        1u);
    EXPECT_EQ(
        harness.Shell.GetLastFrame().ModelBuildStats.DomainWindowModelCacheHits,
        2u);
}

TEST(SandboxDomainPanels, UniformColorCommandsDisableBakingAndRetainScalarStyle)
{
    Runtime::EditorVisualizationConfigModel model{};
    model.Source = decltype(model.Source)::ScalarField;
    model.Color = {0.2f, 0.3f, 0.4f, 0.5f};
    model.ScalarFieldName = "f:curvature";
    model.ScalarDomain = decltype(model.ScalarDomain)::Face;
    model.ScalarAutoRange = false;
    model.ScalarRangeMin = -2.0f;
    model.ScalarRangeMax = 3.0f;
    model.ScalarColormap = decltype(model.ScalarColormap)::Inferno;
    model.IsolineValues = {-1.0f, 0.5f, 2.0f};
    model.IsolineValueCount = 3u;
    model.UseBakedTexture = true;

    const auto base = Editor::MakeVisualizationConfigCommandFromModel(
        42u, model, Runtime::EditorVisualizationTarget::Surface);
    const glm::vec4 replacement{0.9f, 0.8f, 0.7f, 1.0f};
    const auto uniform = Editor::MakeUniformVisualizationConfigCommandFromModel(
        42u, model, Runtime::EditorVisualizationTarget::Surface, replacement);

    EXPECT_EQ(base.Source, decltype(model.Source)::ScalarField);
    EXPECT_EQ(base.Color, model.Color);
    EXPECT_TRUE(base.UseBakedTexture);
    EXPECT_EQ(uniform.Source, decltype(model.Source)::UniformColor);
    EXPECT_EQ(uniform.Color, replacement);
    EXPECT_FALSE(uniform.UseBakedTexture);
    for (const auto* command : {&base, &uniform})
    {
        EXPECT_EQ(command->StableEntityId, 42u);
        EXPECT_EQ(command->Target, Runtime::EditorVisualizationTarget::Surface);
        EXPECT_TRUE(command->EnableConfig);
        EXPECT_EQ(command->ScalarFieldName, "f:curvature");
        EXPECT_EQ(command->ScalarDomain, decltype(model.ScalarDomain)::Face);
        EXPECT_FALSE(command->ScalarAutoRange);
        EXPECT_FLOAT_EQ(command->ScalarRangeMin, -2.0f);
        EXPECT_FLOAT_EQ(command->ScalarRangeMax, 3.0f);
        EXPECT_EQ(command->ScalarColormap, decltype(model.ScalarColormap)::Inferno);
        EXPECT_EQ(command->IsolineValues, model.IsolineValues);
        EXPECT_EQ(command->IsolineValueCount, 3u);
    }
}

TEST(SandboxDomainPanels, AppearanceCheckboxesCanEnableAndReenableEverySupportedLayer)
{
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
    namespace G = Extrinsic::Graphics::Components;
    using Kind = Runtime::EditorDomainWindowKind;
    for (const auto kind : {Kind::Mesh, Kind::Graph, Kind::PointCloud})
    {
        SCOPED_TRACE(Runtime::DebugNameForEditorDomainWindowKind(kind));
        auto application = std::make_unique<OneFrameApplication>();
        auto* driver = application.get();
        Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::move(application));
        engine.EmplaceModule<Runtime::SceneInteractionModule>();
        engine.EmplaceModule<Runtime::EditorUiModule>();
        engine.Initialize();
        auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
        auto& raw = scene.Raw();
        const auto entity = scene.Create();
        raw.emplace<Extrinsic::ECS::Components::Selection::SelectableTag>(entity);
        if (kind == Kind::Mesh)
        {
            Geometry::HalfedgeMesh::Mesh mesh;
            const auto a = mesh.AddVertex({0.0f, 0.0f, 0.0f});
            const auto b = mesh.AddVertex({1.0f, 0.0f, 0.0f});
            const auto c = mesh.AddVertex({0.0f, 1.0f, 0.0f});
            (void)mesh.AddTriangle(a, b, c);
            GS::PopulateFromMesh(raw, entity, mesh);
        }
        else if (kind == Kind::Graph)
        {
            Geometry::Graph::Graph graph;
            const auto a = graph.AddVertex({0.0f, 0.0f, 0.0f});
            const auto b = graph.AddVertex({1.0f, 0.0f, 0.0f});
            (void)graph.AddEdge(a, b);
            GS::PopulateFromGraph(raw, entity, graph);
        }
        else
        {
            Geometry::PointCloud::Cloud cloud;
            (void)cloud.AddPoint({0.0f, 0.0f, 0.0f});
            GS::PopulateFromCloud(raw, entity, cloud);
        }
        raw.emplace<G::VisualizationConfig>(entity).Source =
            G::VisualizationConfig::ColorSource::UniformColor;
        auto* selection = engine.Services().Find<Runtime::SelectionController>();
        ASSERT_NE(selection, nullptr);
        ASSERT_TRUE(selection->SetSelectedEntity(scene, entity));
        Editor::EditorShell shell;
        shell.Attach(engine.Worlds(), engine.Services());
        Editor::DomainPanels panels;
        panels.Register(shell);
        const char* windowId = kind == Kind::Mesh ? "mesh.appearance"
            : kind == Kind::Graph ? "graph.appearance" : "pointcloud.appearance";
        ASSERT_TRUE(shell.SetEditorWindowOpen(windowId, true));
        const std::string title = std::string(Runtime::DebugNameForEditorDomainWindowKind(kind)) +
                                  " / Appearance";
        const std::vector<Kind> lanes = kind == Kind::Mesh
            ? std::vector{Kind::Mesh, Kind::Graph, Kind::PointCloud}
            : kind == Kind::Graph ? std::vector{Kind::Graph, Kind::PointCloud}
                                  : std::vector{Kind::PointCloud};
        int frame = 0;
        std::size_t checked = 0;
        int materialStep = 0;
        driver->OnFrame = [&](Runtime::Engine& kernel) {
            ++frame;
            if (frame < 3)
                return;
            if (checked == lanes.size() * 4u)
            {
                auto* window = ImGui::FindWindowByName(title.c_str());
                if (window == nullptr)
                {
                    ADD_FAILURE() << "Appearance window disappeared";
                    kernel.RequestExit();
                    return;
                }
                const int scope = static_cast<int>(Kind::PointCloud);
                const auto seed = ImHashData(&scope, sizeof(scope), window->ID);
                const auto settings = ImHashStr("Settings", 0, seed);
                if (materialStep == 0)
                    ImGui::ActivateItemByID(ImHashStr("Points", 0, seed));
                if (materialStep == 3)
                    ImGui::ActivateItemByID(settings);
                if (materialStep == 6)
                    ImGui::ActivateItemByID(ImHashStr("Property", 0, settings));
                if (materialStep == 9)
                {
                    const auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
                    if (popups.empty() || popups.back().Window == nullptr)
                    {
                        ADD_FAILURE() << "Property dropdown did not open";
                        kernel.RequestExit();
                        return;
                    }
                    ImGui::ActivateItemByID(
                        popups.back().Window->GetID("Material / default"));
                }
                if (++materialStep == 12)
                {
                    const auto* overrides = raw.try_get<G::VisualizationLaneOverrides>(entity);
                    EXPECT_TRUE(overrides != nullptr && overrides->Points.has_value());
                    if (overrides != nullptr && overrides->Points.has_value())
                        EXPECT_EQ(overrides->Points->Source,
                                  G::VisualizationConfig::ColorSource::Material);
                    kernel.RequestExit();
                }
                return;
            }
            auto* window = ImGui::FindWindowByName(title.c_str());
            if (window == nullptr)
            {
                ADD_FAILURE() << "Appearance window did not open";
                kernel.RequestExit();
                return;
            }
            ImGui::SetWindowSize(window, ImVec2{600.0f, 900.0f});
            const auto lane = lanes[checked / 4u];
            if (frame % 3 == 0)
            {
                const int scope = static_cast<int>(lane);
                const auto seed = ImHashData(&scope, sizeof(scope), window->ID);
                const auto id = ImHashStr(lane == Kind::Mesh ? "Surface"
                    : lane == Kind::Graph ? "Edges" : "Points", 0, seed);
                ImGui::FocusWindow(window);
                ImGui::ActivateItemByID(id);
            }
            if (frame % 3 == 2)
            {
                const bool visible = lane == Kind::Mesh ? raw.all_of<G::RenderSurface>(entity)
                    : lane == Kind::Graph ? raw.all_of<G::RenderEdges>(entity)
                                          : raw.all_of<G::RenderPoints>(entity);
                EXPECT_EQ(visible, checked % 2u == 0u) << "toggle " << checked;
                ++checked;
            }
        };
        engine.Run();
        EXPECT_EQ(checked, lanes.size() * 4u);
        EXPECT_EQ(materialStep, 12);
        panels.Unregister();
        shell.Detach();
        engine.Shutdown();
    }
}
