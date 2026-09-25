#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
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
#include "TestImGuiFrameScope.hpp"

import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Sandbox.Editor.DomainPanels;
import Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
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
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.PointCloudConsolidationTypes;

#include "../../../src/app/Sandbox/Editor/Sandbox.PanelSupport.hpp"

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

TEST(SandboxDomainPanels, SharedScalarControlsPreserveStylingAndEditAuthority)
{
    namespace G = Extrinsic::Graphics::Components;
    TestSupport::ImGuiFrameScope gui;
    ImGui::GetIO().DisplaySize = {1200, 1200};
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = scene.Create();
    const auto stableId = Runtime::SelectionController::ToStableEntityId(entity);
    std::size_t publications = 0;
    Editor::SandboxEditorContext context;
    context.VisualizationCommands = Runtime::BindEditorVisualizationEditingCommands({
        .Scene = &scene,
        .InvalidateWorkspaceSnapshotCache = [&] { ++publications; },
        .VisualizationCommandsAvailable = true,
    });
    Runtime::EditorVisualizationConfigModel model;
    model.HasConfig = true;
    model.Source = Editor::kScalarFieldSource;
    model.Color = {0.2f, 0.4f, 0.6f, 1.0f};
    model.ScalarFieldName = "curvature";
    model.ColorBufferName = "saved-colors";
    model.ScalarAutoRange = false;
    model.ScalarRangeMin = -2.0f;
    model.ScalarRangeMax = 6.0f;
    model.ScalarColormap = decltype(model.ScalarColormap)::Plasma;
    model.ScalarBinCount = 7u;
    model.IsolineCount = 9u;
    model.IsolineWidth = 2.5f;
    model.IsolineColor = {0.8f, 0.6f, 0.4f, 1.0f};
    model.IsolineValues = {1.0f, 3.0f, 5.0f};
    model.IsolineValueCount = 3u;

    for (const auto target : {Runtime::EditorVisualizationTarget::Entity,
                              Runtime::EditorVisualizationTarget::Surface})
    {
        SCOPED_TRACE(Runtime::DebugNameForEditorVisualizationTarget(target));
        const auto reset = [&]
        {
            const auto status = Runtime::ApplyEditorVisualizationConfigCommand(
                context.VisualizationCommands,
                Editor::MakeVisualizationConfigCommandFromModel(stableId, model, target));
            EXPECT_TRUE(status == Runtime::EditorCommandStatus::Applied ||
                        status == Runtime::EditorCommandStatus::NoChange);
            publications = 0;
        };
        const auto current = [&]() -> G::VisualizationConfig
        {
            if (target == Runtime::EditorVisualizationTarget::Entity)
                return scene.Raw().get<G::VisualizationConfig>(entity);
            const auto* lanes = scene.Raw().try_get<G::VisualizationLaneOverrides>(entity);
            EXPECT_NE(lanes, nullptr);
            if (lanes == nullptr) return {};
            EXPECT_TRUE(lanes->Surface.has_value());
            return lanes->Surface.value_or(G::VisualizationConfig{});
        };
        const auto activate = [&](const char* label, const bool canEdit, const int index = -1)
        {
            for (int frame = 0; frame != 2; ++frame)
            {
                gui.NextFrame();
                ImGui::SetNextWindowPos({0, 0});
                ImGui::SetNextWindowSize({1100, 1100});
                ImGui::Begin("Shared scalar controls", nullptr, ImGuiWindowFlags_NoSavedSettings);
                ImGui::PushID(static_cast<int>(target));
                if (frame == 0)
                {
                    if (index >= 0) ImGui::PushID(index);
                    ImGui::ActivateItemByID(ImGui::GetID(label));
                    if (index >= 0) ImGui::PopID();
                }
                Editor::DrawScalarFieldColorControls(model, context, stableId, target, canEdit);
                Editor::DrawScalarFieldBinAndIsolineControls(model, context, stableId, target, canEdit);
                ImGui::PopID();
                ImGui::End();
            }
        };

        const auto expectStyle = [&](const G::VisualizationConfig& config)
        {
            EXPECT_EQ(config.Source, model.Source);
            EXPECT_EQ(config.Color, model.Color);
            EXPECT_EQ(config.ScalarFieldName, model.ScalarFieldName);
            EXPECT_EQ(config.ColorBufferName, model.ColorBufferName);
            EXPECT_EQ(config.Interpretation, model.Interpretation);
            EXPECT_EQ(config.ScalarDomain, model.ScalarDomain);
            EXPECT_FLOAT_EQ(config.Scalar.RangeMin, model.ScalarRangeMin);
            EXPECT_FLOAT_EQ(config.Scalar.RangeMax, model.ScalarRangeMax);
            EXPECT_EQ(config.Scalar.BinCount, model.ScalarBinCount);
            EXPECT_EQ(config.Scalar.Map, model.ScalarColormap);
            EXPECT_EQ(config.Scalar.Isolines.Num, model.IsolineCount);
            EXPECT_FLOAT_EQ(config.Scalar.Isolines.Width, model.IsolineWidth);
            EXPECT_EQ(config.Scalar.Isolines.Color, model.IsolineColor);
            EXPECT_EQ(config.UseBakedTexture, model.UseBakedTexture);
        };
        reset();
        activate("Auto range", true);
        EXPECT_EQ(publications, 1u);
        const auto changed = current();
        EXPECT_TRUE(changed.Scalar.AutoRange);
        expectStyle(changed);
        EXPECT_EQ(changed.Scalar.Isolines.Values, model.IsolineValues);
        EXPECT_EQ(changed.Scalar.Isolines.ValueCount, model.IsolineValueCount);

        for (const char* label : {"Auto range", "Add isovalue", "Remove"})
        {
            reset();
            activate(label, false, std::string_view{label} == "Remove" ? 1 : -1);
            EXPECT_EQ(publications, 0u) << label;
            EXPECT_FALSE(current().Scalar.AutoRange);
            EXPECT_EQ(current().Scalar.Isolines.ValueCount, 3u);
        }
        for (const bool autoRange : {false, true})
        {
            model.ScalarAutoRange = autoRange;
            reset();
            activate("Add isovalue", true);
            EXPECT_EQ(publications, 1u);
            const auto added = current();
            expectStyle(added);
            EXPECT_EQ(added.Scalar.AutoRange, autoRange);
            EXPECT_EQ(added.Scalar.Isolines.ValueCount, 4u);
            for (std::size_t i = 0; i < 3; ++i)
                EXPECT_FLOAT_EQ(added.Scalar.Isolines.Values[i], model.IsolineValues[i]);
            EXPECT_FLOAT_EQ(added.Scalar.Isolines.Values[3], autoRange ? 0.0f : 2.0f);
        }
        model.ScalarAutoRange = false;
        for (int remove = 0; remove < 3; ++remove)
        {
            reset();
            activate("Remove", true, remove);
            EXPECT_EQ(publications, 1u);
            const auto removed = current();
            expectStyle(removed);
            EXPECT_FALSE(removed.Scalar.AutoRange);
            EXPECT_EQ(removed.Scalar.Isolines.ValueCount, 2u);
            for (int i = 0; i < 2; ++i)
                EXPECT_FLOAT_EQ(removed.Scalar.Isolines.Values[i],
                                model.IsolineValues[i < remove ? i : i + 1]);
        }
        model.IsolineValueCount = model.IsolineValues.size();
        reset();
        activate("Add isovalue", true);
        EXPECT_EQ(publications, 0u);
        EXPECT_EQ(current().Scalar.Isolines.ValueCount, model.IsolineValues.size());
        model.IsolineValueCount = 3u;
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

// UI automation for the Appearance-level Vector fields section: domain first,
// then a vec3 property, then edit and close the field. The mesh has no visible
// lane, which the section must not depend on.
TEST(SandboxDomainPanels, VectorFieldSectionAddsEditsAndRemovesFieldsWithoutVisibleLanes)
{
    namespace GS = Extrinsic::ECS::Components::GeometrySources;
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
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    const auto b = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    const auto c = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    (void)mesh.AddTriangle(a, b, c);
    GS::PopulateFromMesh(raw, entity, mesh);
    raw.get<GS::Faces>(entity).Properties.GetOrAdd<glm::vec3>("f:flow", glm::vec3{1.0f, 0.0f, 0.0f});
    auto* selection = engine.Services().Find<Runtime::SelectionController>();
    ASSERT_NE(selection, nullptr);
    ASSERT_TRUE(selection->SetSelectedEntity(scene, entity));

    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::DomainPanels panels;
    panels.Register(shell);
    ASSERT_TRUE(shell.SetEditorWindowOpen("mesh.appearance", true));
    const std::string title = "Mesh / Appearance";

    const auto layer = [&]() -> const Runtime::GeometryVectorFieldLayerRecipe* {
        const auto* recipe = raw.try_get<Runtime::GeometryPresentationRecipe>(entity);
        return recipe != nullptr
            ? Runtime::FindGeometryVectorFieldLayer(*recipe, Runtime::GeometryElementDomain::MeshFace, "f:flow")
            : nullptr;
    };
    const auto selectInPopup = [](const char* label) {
        const auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
        if (popups.empty() || popups.back().Window == nullptr)
            return false;
        ImGui::ActivateItemByID(popups.back().Window->GetID(label));
        return true;
    };

    int frame = 0;
    int step = 0;
    driver->OnFrame = [&](Runtime::Engine& kernel) {
        ++frame;
        auto* window = ImGui::FindWindowByName(title.c_str());
        if (frame < 3)
            return;
        if (window == nullptr || frame > 200)
        {
            ADD_FAILURE() << "Appearance automation stalled at step " << step;
            kernel.RequestExit();
            return;
        }
        ImGui::SetWindowSize(window, ImVec2{600.0f, 1100.0f});
        if (frame % 3 != 0)
            return;
        const ImGuiID section = ImHashStr("vector-fields", 0, window->ID);
        const ImGuiID row = ImHashStr("Facesf:flow", 0, section);
        const ImGuiID header = ImHashStr("Faces: f:flow###Facesf:flow", 0, row);
        switch (step)
        {
        case 0:
            // Focus once: refocusing later would close the combo popups the
            // following steps open. The collapsed section header is opened
            // through ImGui's own tree state; every control inside it is
            // driven through the widget itself.
            ImGui::FocusWindow(window);
            window->DC.StateStorage->SetInt(ImHashStr("Vector fields", 0, window->ID), 1);
            break;
        case 1: ImGui::ActivateItemByID(ImHashStr("Domain", 0, section)); break;
        case 2: EXPECT_TRUE(selectInPopup("Faces (1)")) << "Domain dropdown did not open"; break;
        case 3: ImGui::ActivateItemByID(ImHashStr("Property", 0, section)); break;
        case 4: EXPECT_TRUE(selectInPopup("f:flow")) << "Property dropdown did not open"; break;
        case 5:
            ASSERT_NE(layer(), nullptr) << "selecting the property must add the field";
            EXPECT_TRUE(layer()->Enabled);
            EXPECT_FALSE(raw.all_of<Extrinsic::Graphics::Components::RenderSurface>(entity));
            ImGui::ActivateItemByID(ImHashStr("Visible", 0, row));
            break;
        case 6:
            ASSERT_NE(layer(), nullptr);
            EXPECT_FALSE(layer()->Enabled) << "the Visible checkbox must update the field";
            ImGui::ActivateItemByID(ImHashStr("#CLOSE", 0, header));
            break;
        case 7:
            EXPECT_EQ(layer(), nullptr) << "the close button must remove the field";
            kernel.RequestExit();
            break;
        default: break;
        }
        ++step;
    };
    engine.Run();
    EXPECT_EQ(step, 8);
    panels.Unregister();
    shell.Detach();
    engine.Shutdown();
}
