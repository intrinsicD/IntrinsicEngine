#include <unordered_map>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include "RuntimeTestModule.hpp"
#include "MockRHI.hpp"

import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Core.Tasks;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EngineConfigControl;
// Config accessors this test round-trips come from their owning config modules,
// not from the operation families that consume them.
import Extrinsic.Runtime.CurvatureSegmentationConfig;
import Extrinsic.Runtime.GeodesicsConfig;
import Extrinsic.Runtime.MeshCurvatureConfig;
import Extrinsic.Runtime.RegistrationConfig;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.CoherentPointDriftConfig;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.CameraModule;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Sandbox.ConfigSections;
import Extrinsic.Sandbox.Editor.MeshProcessingPanels;
import Extrinsic.Sandbox.Editor.MethodPanels;
import Extrinsic.Sandbox.Editor.Shell;
import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.Curvature;
import Geometry.PointCloud;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.PointCloudConsolidationTypes;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.SpatialIndexCache;

#include "TestImGuiFrameScope.hpp"
#include "../../../src/app/Sandbox/Editor/Sandbox.PanelSupport.hpp"

namespace R = Extrinsic::Runtime;
namespace Editor = Extrinsic::Sandbox::Editor;
namespace Config = Extrinsic::Core::Config;
namespace G = Extrinsic::Graphics::Components;
namespace GS = Extrinsic::ECS::Components::GeometrySources;

namespace
{
    class PanelDriver final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        std::function<void(R::Engine&)> OnFrame{};
        void Frame(double, double) override { OnFrame(Kernel()); }
    };

    struct PanelHarness
    {
        PanelDriver* Driver{};
        std::unique_ptr<Intrinsic::Tests::RuntimeTestKernel> Engine;
        Editor::EditorShell Shell;
        Editor::MeshProcessingPanels Panels;
        Editor::MethodPanels Methods;

        explicit PanelHarness(Config::EngineConfigSectionRegistry sections =
            Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry(),
            bool clustering = false, bool consolidation = false, bool camera = false)
        {
            Config::EngineConfig config{};
            Config::PopulateEngineConfigSectionDefaults(config, sections);
            config.Simulation.WorkerThreadCount = 1u;
            config.ReferenceScene.Enabled = false;
            config.Camera.Enabled = camera;
            config.Window.Backend = Config::WindowBackend::Null;
            auto driver = std::make_unique<PanelDriver>();
            Driver = driver.get();
            Engine = std::make_unique<Intrinsic::Tests::RuntimeTestKernel>(
                config, std::move(driver));
            Engine->EmplaceModule<R::EngineConfigControl>(std::move(sections));
            if (camera) Engine->EmplaceModule<R::CameraModule>();
            Engine->EmplaceModule<R::SceneInteractionModule>();
            Engine->EmplaceModule<R::AsyncWorkModule>();
            if (clustering) Engine->EmplaceModule<R::ClusteringModule>();
            if (consolidation)
            {
                Engine->EmplaceModule<R::SpatialIndexCache>();
                Engine->EmplaceModule<R::PointCloudConsolidationModule>();
            }
            Engine->EmplaceModule<R::EditorUiModule>();
            Engine->Initialize();
            Shell.Attach(Engine->Worlds(), Engine->Services());
            Panels.Register(Shell);
            Methods.Register(Shell);
            EXPECT_TRUE(Shell.SetEditorWindowOpen("scene.selection", false));
        }

        ~PanelHarness()
        {
            Panels.Unregister();
            Methods.Unregister();
            Shell.Detach();
            Engine->Shutdown();
        }

        auto& Scene() { return *Engine->Worlds().Get(Engine->ActiveWorld()); }
        auto& Selection() { return *Engine->Services().Find<R::SelectionController>(); }
        auto& Control() { return *Engine->Services().Find<R::EngineConfigControl>(); }

        bool Apply(const Config::EngineConfig& config)
        {
            return Control().ApplyEngineConfigHotSubset(
                Control().PreviewEngineConfigControlDocument(
                    Config::SerializeEngineConfig(config))).Succeeded();
        }
    };

    Config::EngineConfigSectionRegistry RejectableConfigRegistry(
        std::string_view section, const bool& reject, unsigned& rejections)
    {
        Config::EngineConfigSectionRegistry registry;
        const auto defaults = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
        for (auto entry : defaults.Entries())
        {
            if (entry.DefaultSection.Name == section)
            {
                entry.Validate = [validate = entry.Validate, &reject, &rejections](auto payload, auto reference, auto subject) {
                    auto result = validate(payload, reference, subject);
                    if (reject)
                    {
                        ++rejections;
                        result.State = Config::EngineConfigState::Invalid;
                        result.Diagnostics.push_back({.Code = Config::EngineConfigDiagnosticCode::InvalidValue,
                            .Subject = std::string{subject}, .Message = "Test config rejection"});
                    }
                    return result;
                };
            }
            EXPECT_TRUE(registry.Register(std::move(entry)));
        }
        return registry;
    }

    void EditScalarControl(ImGuiWindow* window, const char* label, int step, const char* value)
    {
        if (step == 0)
        {
            ImGui::FocusWindow(window);
            ImGui::ActivateItemByID(window->GetID(label));
            ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
        }
        if (step == 2)
        {
            EXPECT_EQ(ImGui::GetActiveID(), window->GetID(label));
            ImGui::GetIO().AddInputCharactersUTF8(value);
        }
        if (step == 4) ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
        if (step == 5) ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    }

    constexpr std::array kInputWindows{
        "view.normal_estimation", "view.outlier_analysis", "view.keypoint_analysis",
        "view.descriptor_analysis", "view.kernel_density", "view.density_weights",
        "view.point_construction", "view.point_spacing", "view.bilateral_filter",
        "view.registration"};

    void ExpectInputEntities(const Config::EngineConfig& config, std::uint32_t expected,
                             std::uint32_t target = 0u)
    {
        EXPECT_EQ(R::GetNormalEstimationConfig(config)->StableEntityId, expected);
        EXPECT_EQ(R::GetOutlierAnalysisConfig(config)->StableEntityId, expected);
        EXPECT_EQ(R::GetKeypointAnalysisConfig(config)->StableEntityId, expected);
        EXPECT_EQ(R::GetDescriptorAnalysisConfig(config)->StableEntityId, expected);
        EXPECT_EQ(R::GetKernelDensityConfig(config)->StableEntityId, expected);
        EXPECT_EQ(R::GetDensityWeightConfig(config)->StableEntityId, expected);
        EXPECT_EQ(R::GetPointConstructionConfig(config)->StableEntityId, expected);
        EXPECT_EQ(R::GetPointSpacingConfig(config)->StableEntityId, expected);
        EXPECT_EQ(R::GetBilateralFilterConfig(config)->StableEntityId, expected);
        EXPECT_EQ(R::GetRegistrationConfig(config)->SourceStableEntityId, expected);
        EXPECT_EQ(R::GetRegistrationConfig(config)->TargetStableEntityId, target);
    }

    void PopulateSamples(auto& raw, auto entity, R::GeometryElementDomain domain)
    {
        if (domain == R::GeometryElementDomain::MeshVertex)
        {
            Geometry::HalfedgeMesh::Mesh mesh;
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x)
                    (void)mesh.AddVertex({float(x), float(y), 0});
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x)
                {
                    const auto a = Geometry::VertexHandle(y * 3 + x);
                    const auto b = Geometry::VertexHandle(y * 3 + x + 1);
                    const auto c = Geometry::VertexHandle((y + 1) * 3 + x);
                    const auto d = Geometry::VertexHandle((y + 1) * 3 + x + 1);
                    EXPECT_TRUE(mesh.AddTriangle(a, b, c));
                    EXPECT_TRUE(mesh.AddTriangle(b, d, c));
                }
            GS::PopulateFromMesh(raw, entity, mesh);
            raw.template emplace<G::RenderSurface>(entity);
        }
        else if (domain == R::GeometryElementDomain::GraphNode)
        {
            Geometry::Graph::Graph graph;
            for (int i = 0; i < 9; ++i)
            {
                const auto vertex = graph.AddVertex({float(i % 3), float(i / 3), 0});
                if (i > 0)
                    (void)graph.AddEdge(Geometry::VertexHandle(i - 1), vertex);
            }
            GS::PopulateFromGraph(raw, entity, graph);
            raw.template emplace<G::RenderEdges>(entity);
        }
        else
        {
            Geometry::PointCloud::Cloud cloud;
            for (int i = 0; i < 9; ++i)
                (void)cloud.AddPoint({float(i % 3), float(i / 3), 0});
            GS::PopulateFromCloud(raw, entity, cloud);
            raw.template emplace<G::RenderPoints>(entity);
        }
    }
}

TEST(SandboxProcessingPanels, EveryEntityInputFollowsSelectionWithSelectionDetailsClosed)
{
    PanelHarness harness;
    auto& scene = harness.Scene();
    const auto first = scene.Create();
    const auto second = scene.Create();
    PopulateSamples(scene.Raw(), first, R::GeometryElementDomain::MeshVertex);
    PopulateSamples(scene.Raw(), second, R::GeometryElementDomain::PointCloudPoint);
    scene.Raw().emplace<Extrinsic::ECS::Components::Selection::SelectableTag>(second);
    // A stale persisted input must not become the initial UI selection.
    auto config = harness.Control().GetEngineConfigControlState().ActiveConfig;
    auto normals = *R::GetNormalEstimationConfig(config);
    normals.StableEntityId = R::SelectionController::ToStableEntityId(second);
    normals.KNeighbors = 7u;
    R::SetNormalEstimationConfig(config, normals);
    ASSERT_TRUE(harness.Apply(config));
    for (const auto id : kInputWindows)
        ASSERT_TRUE(harness.Shell.SetEditorWindowOpen(id, true));
    int frame = 0;
    harness.Driver->OnFrame = [&](R::Engine& engine) {
        ++frame;
        const auto& active = harness.Control().GetEngineConfigControlState().ActiveConfig;
        // The registration target set at frame 9 follows the selection only when two entities
        // are selected; clearing or single selection keeps it (a destroyed one reads as stale).
        const auto firstId = R::SelectionController::ToStableEntityId(first);
        if (frame == 3)
            ExpectInputEntities(active, 0u);
        if (frame == 12 || frame == 18)
            ExpectInputEntities(active, 0u, firstId);
        if (frame == 6)
            ExpectInputEntities(active, firstId);
        if (frame == 15)
            ExpectInputEntities(active, firstId, firstId);
        if (frame == 9)
            ExpectInputEntities(active, R::SelectionController::ToStableEntityId(second));
        if (frame == 3 || frame == 12)
            EXPECT_TRUE(harness.Selection().SetSelectedEntity(scene, first));
        if (frame == 6)
            EXPECT_TRUE(harness.Selection().SetSelectedEntity(scene, second));
        if (frame == 9)
        {
            auto edited = active;
            auto registration = *R::GetRegistrationConfig(edited);
            registration.TargetStableEntityId = R::SelectionController::ToStableEntityId(first);
            R::SetRegistrationConfig(edited, registration);
            EXPECT_TRUE(harness.Apply(edited));
            harness.Selection().ClearSelection(scene);
        }
        if (frame == 15)
            scene.Destroy(first);
        if (frame == 18)
        {
            EXPECT_EQ(R::GetNormalEstimationConfig(active)->KNeighbors, 7u);
            const auto third = scene.Create();
            PopulateSamples(scene.Raw(), third, R::GeometryElementDomain::MeshVertex);
            EXPECT_TRUE(harness.Selection().SetSelectedEntity(scene, third));
            harness.Selection().RequestClickPick(0u, 0u, R::SelectionPickMode::Add);
            (void)harness.Selection().ConsumePendingPick();
            harness.Selection().ConsumeHit(scene, R::SelectionController::ToStableEntityId(second));
        }
        if (frame == 21)
        {
            const auto selected = harness.Selection().SelectedStableIds();
            EXPECT_EQ(selected.size(), 2u);
            if (selected.size() == 2u)
                ExpectInputEntities(active, selected[0], selected[1]);
            engine.RequestExit();
        }
    };
    harness.Engine->Run();
    EXPECT_EQ(frame, 21);
}

TEST(SandboxProcessingPanels, CameraPanelPresetAndFocusButtonsDriveTheMainCamera)
{
    PanelHarness harness(Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry(), false, false, true);
    auto& scene = harness.Scene();
    const auto addBounded = [&](const glm::vec3 center)
    {
        const auto entity = scene.Create();
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
        Extrinsic::ECS::Components::Culling::World::Bounds bounds{};
        bounds.WorldBoundingSphere.Center = center;
        bounds.WorldBoundingSphere.Radius = 2.0f;
        scene.Raw().emplace_or_replace<Extrinsic::ECS::Components::Culling::World::Bounds>(entity, bounds);
        return entity;
    };
    const glm::vec3 centerA{100.0f, 0.0f, 0.0f}, centerB{100.0f, 30.0f, 40.0f};
    const auto a = addBounded(centerA);
    const auto b = addBounded(centerB);
    auto* cameras = harness.Engine->Services().Find<R::CameraControllerRegistry>();
    ASSERT_NE(cameras, nullptr);
    ASSERT_TRUE(harness.Shell.SetEditorWindowOpen("view.camera_render", true));
    const Extrinsic::Core::Extent2D extent{640, 480};
    const auto view = [&] { return cameras->Resolve(R::CameraControllerSlot::Main).GetView(extent); };
    // Distance of `point` from the camera's view axis (negative when behind the camera).
    const auto offAxis = [&](const glm::vec3 point)
    {
        const auto v = view();
        const glm::vec3 toPoint = point - v.Position;
        return glm::dot(toPoint, v.Forward) > 0.0f ? glm::length(glm::cross(v.Forward, toPoint)) : -1.0f;
    };

    int frame = 0;
    harness.Driver->OnFrame = [&](R::Engine& engine) {
        ++frame;
        auto* window = ImGui::FindWindowByName("Camera / Render");
        if (window == nullptr)
        {
            if (frame > 40) { ADD_FAILURE() << "camera window never drew"; engine.RequestExit(); }
            return;
        }
        ImGui::SetWindowSize(window, {900, 400});
        ImGui::SetWindowPos(window, {0, 0});
        ImGui::FocusWindow(window);
        if (frame == 3)
            ImGui::ActivateItemByID(window->GetID("Top"));
        if (frame == 5)
        {
            // Nothing selected: the whole scene is framed from above (both are far from the origin).
            EXPECT_NEAR(glm::dot(view().Forward, glm::vec3(0, -1, 0)), 1.0f, 1e-3f);
            EXPECT_NEAR(view().Position.x, 100.0f, 1e-2f);
            EXPECT_NEAR(view().Position.z, 20.0f, 1e-2f) << "centered between both entities";
            EXPECT_TRUE(harness.Selection().SetSelectedEntity(scene, a));
        }
        if (frame == 7)
            ImGui::ActivateItemByID(window->GetID("Left"));
        if (frame == 9)
        {
            // Selected: the preset frames A along +X.
            EXPECT_NEAR(glm::dot(view().Forward, glm::vec3(1, 0, 0)), 1.0f, 1e-3f);
            EXPECT_NEAR(offAxis(centerA), 0.0f, 1e-2f);
            EXPECT_TRUE(harness.Selection().SetSelectedEntity(scene, b));
        }
        if (frame == 11)
        {
            EXPECT_GT(offAxis(centerB), 1.0f) << "B is off the view axis before focusing";
            ImGui::ActivateItemByID(window->GetID("Focus selection"));
        }
        if (frame == 13)
        {
            EXPECT_NEAR(glm::dot(view().Forward, glm::vec3(1, 0, 0)), 1.0f, 1e-3f) << "focus keeps the direction";
            EXPECT_NEAR(offAxis(centerB), 0.0f, 1e-2f) << "the selection is now on the view axis";
            engine.RequestExit();
        }
    };
    harness.Engine->Run();
    EXPECT_GE(frame, 13);
}

TEST(SandboxProcessingPanels, CameraViewFocusButtonIsDisabledWithoutASelection)
{
    TestSupport::ImGuiFrameScope gui;
    Editor::SandboxEditorContext context{};
    Editor::CameraViewUiState state{};
    const std::array<std::uint32_t, 1> selected{7u};
    for (const bool hasSelection : {false, true})
    {
        gui.NextFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({800, 200});
        ImGui::Begin("Camera view test", nullptr, ImGuiWindowFlags_NoSavedSettings);
        Editor::DrawCameraViewControls(context, hasSelection ? std::span<const std::uint32_t>{selected}
                                                             : std::span<const std::uint32_t>{},
                                       Config::CameraControllerKind::Orbit, state);
        const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
        EXPECT_EQ(disabled, !hasSelection) << "the Focus selection button is the last item drawn";
        ImGui::End();
    }
}

TEST(SandboxProcessingPanels, ShowButtonsApplyAppearancePropertiesOnMeshGraphAndCloud)
{
    for (const auto domain : {R::GeometryElementDomain::MeshVertex,
                              R::GeometryElementDomain::GraphNode,
                              R::GeometryElementDomain::PointCloudPoint})
    {
        SCOPED_TRACE(R::ToString(domain));
        PanelHarness harness;
        auto& scene = harness.Scene();
        const auto entity = scene.Create();
        PopulateSamples(scene.Raw(), entity, domain);
        auto& properties = scene.Raw().get<GS::Vertices>(entity).Properties;
        for (const auto* name : {"outlier_score", "keypoint_saliency", "density", "density_weight", "radii"})
            (void)properties.GetOrAdd<float>(name, 0.5f);
        for (const auto* name : {"outlier_mask", "keypoint_mask"})
            (void)properties.GetOrAdd<std::uint32_t>(name, 1u);
        const auto descriptors = R::MakeDescriptorOutputProperties(domain);
        for (const auto& output : descriptors)
            (void)properties.GetOrAdd<float>(output.Name, 1.0f);
        ASSERT_TRUE(harness.Selection().SetSelectedEntity(scene, entity));
        auto config = harness.Control().GetEngineConfigControlState().ActiveConfig;
        auto normals = *R::GetNormalEstimationConfig(config);
        normals.Method = domain == R::GeometryElementDomain::MeshVertex
            ? R::NormalEstimationMethod::MeshFaceWeighted : R::NormalEstimationMethod::PointSetPCA;
        normals.KNeighbors = 8u;
        R::SetNormalEstimationConfig(config, normals);
        ASSERT_TRUE(harness.Apply(config));

        struct ShowAction { const char* Window; const char* Title; const char* Button; std::string Property; bool Scalar; };
        const std::array actions{
            ShowAction{"view.normal_estimation", "Normal Estimation", "Show normals", "v:normal", false},
            ShowAction{"view.outlier_analysis", "Outlier Analysis", "Show mask", "outlier_mask", true},
            ShowAction{"view.outlier_analysis", "Outlier Analysis", "Show score", "outlier_score", true},
            ShowAction{"view.keypoint_analysis", "ISS Keypoint Analysis", "Show mask", "keypoint_mask", true},
            ShowAction{"view.keypoint_analysis", "ISS Keypoint Analysis", "Show saliency", "keypoint_saliency", true},
            ShowAction{"view.descriptor_analysis", "FPFH Descriptor Analysis", "Show histogram bin", descriptors[0].Name, true},
            ShowAction{"view.kernel_density", "Kernel Density", "Show density", "density", true},
            ShowAction{"view.density_weights", "Compact Density Weights", "Show weights", "density_weight", true},
            ShowAction{"view.point_spacing", "Point Spacing and Radii", "Show radii", "radii", true}};
        ASSERT_TRUE(harness.Shell.SetEditorWindowOpen(actions[0].Window, true));
        int frame = 0;
        std::size_t action = 0;
        int step = 0;
        harness.Driver->OnFrame = [&](R::Engine& engine) {
            ++frame;
            if (frame > 100)
            {
                ADD_FAILURE() << "Show actions did not finish";
                engine.RequestExit();
                return;
            }
            const auto& show = actions[action];
            auto* window = ImGui::FindWindowByName(show.Title);
            if (window == nullptr)
                return;
            ImGui::SetWindowSize(window, {700, 1000});
            ImGui::SetWindowPos(window, {0, 0});
            ImGui::FocusWindow(window);
            if (action == 0 && step == 2)
                ImGui::ActivateItemByID(window->GetID("Estimate normals"));
            if (step == 3)
            {
                // Point-set normals run asynchronously: hold this step until they are
                // published, so Show never races the completion under parallel CTest load.
                if (action == 0 && !std::as_const(properties).Exists("v:normal")) return;
                ImGui::SetScrollY(window, window->ScrollMax.y);
            }
            if (step == 5)
            {
                if (action == 1 || action == 2)
                {
                    const auto input = *R::GetOutlierAnalysisConfig(
                        harness.Control().GetEngineConfigControlState().ActiveConfig);
                    const auto readiness = R::PreviewEditorOutlierAnalysisCommand(
                        R::BindEditorProcessingCommands({.Scene = &scene}), input);
                    EXPECT_FALSE(readiness.Enabled);
                    EXPECT_NE(readiness.DisabledReason.find("more live samples than k"), std::string::npos);
                }
                ImGui::ActivateItemByID(window->GetID(show.Button));
            }
            if (++step != 8)
                return;
            SCOPED_TRACE(show.Button);
            const auto* overrides = scene.Raw().try_get<G::VisualizationLaneOverrides>(entity);
            EXPECT_NE(overrides, nullptr);
            if (overrides)
            {
                const auto& lane = domain == R::GeometryElementDomain::MeshVertex ? overrides->Surface
                    : domain == R::GeometryElementDomain::GraphNode ? overrides->Edges : overrides->Points;
                EXPECT_TRUE(lane);
                if (lane)
                {
                    EXPECT_EQ(lane->Source, show.Scalar ? G::VisualizationConfig::ColorSource::ScalarField
                                                      : G::VisualizationConfig::ColorSource::PerVertexBuffer);
                    EXPECT_EQ(show.Scalar ? lane->ScalarFieldName : lane->ColorBufferName, show.Property);
                }
            }
            EXPECT_TRUE(harness.Shell.SetEditorWindowOpen(show.Window, false));
            if (++action == actions.size())
                engine.RequestExit();
            else
            {
                EXPECT_TRUE(harness.Shell.SetEditorWindowOpen(actions[action].Window, true));
                step = 0;
            }
        };
        harness.Engine->Run();
        EXPECT_EQ(action, actions.size());
        EXPECT_TRUE(properties.Exists("v:normal"));
    }
}

TEST(SandboxProcessingPanels, ShowPropertyHelperForwardsNormalDirectionAndPropertyName)
{
    TestSupport::ImGuiFrameScope gui;
    Extrinsic::ECS::Scene::Registry scene;
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
    auto& properties = scene.Raw().get<GS::Vertices>(entity).Properties;
    (void)properties.GetOrAdd<glm::vec3>("direction", glm::vec3{0.0f, 0.0f, 1.0f});
    Editor::SandboxEditorContext context;
    context.VisualizationCommands = R::BindEditorVisualizationEditingCommands({
        .Scene = &scene, .VisualizationCommandsAvailable = true});
    const auto stableId = R::SelectionController::ToStableEntityId(entity);
    const R::GeometryPropertyRef property{R::GeometryElementDomain::PointCloudPoint, "direction",
                                          Geometry::PropertyValueKind::Vec3};
    std::string diagnostic;
    ImGui::Begin("Show helper");
    for (const bool normalDirection : {false, true})
    {
        SCOPED_TRACE(normalDirection);
        const auto status = Editor::DrawProcessingPropertyShowButton(
            context, stableId, property, diagnostic, "Show direction as normal", normalDirection, true);
        ASSERT_TRUE(status.has_value());
        EXPECT_EQ(diagnostic, R::DebugNameForEditorCommandStatus(*status));
        const auto* overrides = scene.Raw().try_get<G::VisualizationLaneOverrides>(entity);
        ASSERT_NE(overrides, nullptr);
        ASSERT_TRUE(overrides->Points);
        EXPECT_EQ(overrides->Points->ColorBufferName, "direction");
        EXPECT_EQ(overrides->Points->Interpretation,
                  normalDirection ? decltype(overrides->Points->Interpretation)::NormalDirection
                                  : decltype(overrides->Points->Interpretation)::Components);
    }
    ImGui::End();
}

TEST(SandboxProcessingPanels, FaceOutputsDisplayWithTheirCanonicalDomain)
{
    for (const bool normals : {true, false})
    {
        SCOPED_TRACE(normals ? "face normals" : "face score");
        PanelHarness harness;
        auto& scene = harness.Scene();
        const auto entity = scene.Create();
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
        auto& faces = scene.Raw().get<GS::Faces>(entity).Properties;
        (void)faces.GetOrAdd<glm::vec3>("f:centroid", {0.5f, 0.5f, 0});
        (void)faces.GetOrAdd<float>("f:score", 0.5f);
        ASSERT_TRUE(harness.Selection().SetSelectedEntity(scene, entity));
        auto config = harness.Control().GetEngineConfigControlState().ActiveConfig;
        auto outliers = *R::GetOutlierAnalysisConfig(config);
        outliers.Positions.Domain = outliers.Mask.Domain = outliers.Score.Domain =
            R::GeometryElementDomain::MeshFace;
        outliers.Positions.Name = "f:centroid";
        outliers.Score.Name = "f:score";
        // Already-published face scores remain displayable when k is too large to run.
        R::SetOutlierAnalysisConfig(config, outliers);
        ASSERT_TRUE(harness.Apply(config));
        ASSERT_EQ(R::GetOutlierAnalysisConfig(harness.Control().GetEngineConfigControlState().ActiveConfig)->Score.Name,
                  "f:score");
        ASSERT_TRUE(harness.Shell.SetEditorWindowOpen(
            normals ? "mesh.processing.faces.normals" : "view.outlier_analysis", true));
        int frame = 0;
        harness.Driver->OnFrame = [&](R::Engine& engine) {
            ++frame;
            auto* window = ImGui::FindWindowByName(normals ? "Normal Estimation" : "Outlier Analysis");
            if (window)
            {
                ImGui::SetWindowSize(window, {700, 1000});
                ImGui::SetWindowPos(window, {0, 0});
                ImGui::FocusWindow(window);
                if (frame == 3 && normals)
                    ImGui::ActivateItemByID(window->GetID("Estimate normals"));
                if (frame == 4)
                    ImGui::SetScrollY(window, window->ScrollMax.y);
                if (frame == 6)
                    ImGui::ActivateItemByID(window->GetID(normals ? "Show face normals" : "Show score"));
            }
            if (frame == 9)
            {
                EXPECT_NE(window, nullptr);
                const auto* overrides = scene.Raw().try_get<G::VisualizationLaneOverrides>(entity);
                EXPECT_TRUE(overrides && overrides->Surface);
                if (overrides && overrides->Surface)
                {
                    const auto& surface = *overrides->Surface;
                    if (normals)
                    {
                        EXPECT_EQ(surface.Source, G::VisualizationConfig::ColorSource::PerFaceBuffer);
                        EXPECT_EQ(surface.ColorBufferName, "f:normal");
                        EXPECT_EQ(surface.Interpretation, decltype(surface.Interpretation)::NormalDirection);
                        EXPECT_TRUE(faces.Exists("f:normal"));
                    }
                    else
                    {
                        EXPECT_EQ(surface.Source, G::VisualizationConfig::ColorSource::ScalarField);
                        EXPECT_EQ(surface.ScalarFieldName, "f:score");
                        EXPECT_EQ(surface.ScalarDomain, G::VisualizationConfig::Domain::Face);
                    }
                }
                engine.RequestExit();
            }
        };
        harness.Engine->Run();
        EXPECT_EQ(frame, 9);
    }
}

TEST(SandboxProcessingPanels, NamedMeshOutputsUseAppearanceWithoutRecomputing)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
    const auto sceneEntity = scene.Create();
    PopulateSamples(scene.Raw(), sceneEntity, R::GeometryElementDomain::MeshVertex);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, sceneEntity));
    const auto stableId = R::SelectionController::ToStableEntityId(entity);
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto curvature = *R::GetMeshCurvatureConfig(config);
    auto segmentation = *R::GetCurvatureSegmentationConfig(config);
    auto geodesics = *R::GetGeodesicsConfig(config);
    auto parameterization = *R::GetParameterizationConfig(config);
    auto poisson = *R::GetProgressivePoissonPlaygroundConfig(config);
    poisson.AutoRunOnEdit = false;
    R::ClusteringConfig clustering;
    clustering.Properties = R::MakeKMeansPropertyRefs(R::GeometryElementDomain::MeshVertex);
    struct Action { const char* Window; const char* Title; R::GeometryPropertyRef Property; };
    std::vector<Action> actions;
    auto add = [&](const char* window, const char* title, R::GeometryPropertyRef& property) {
        property.Name += "_ui_custom";
        actions.push_back({window, title, property});
        auto& props = property.Domain == R::GeometryElementDomain::MeshFace
            ? scene.Raw().get<GS::Faces>(entity).Properties
            : property.Domain == R::GeometryElementDomain::MeshEdge
                ? scene.Raw().get<GS::Edges>(entity).Properties
                : scene.Raw().get<GS::Vertices>(entity).Properties;
        using Kind = Geometry::PropertyValueKind;
        switch (property.ValueKind)
        {
        case Kind::Double: (void)props.GetOrAdd<double>(property.Name, 0.5); break;
        case Kind::Float: (void)props.GetOrAdd<float>(property.Name, 0.5f); break;
        case Kind::Bool: (void)props.GetOrAdd<bool>(property.Name, true); break;
        case Kind::UInt32: (void)props.GetOrAdd<std::uint32_t>(property.Name, 1); break;
        case Kind::Vec2: (void)props.GetOrAdd<glm::vec2>(property.Name, {0.2f, 0.7f}); break;
        case Kind::Vec3: (void)props.GetOrAdd<glm::vec3>(property.Name, {0, 0, 1}); break;
        case Kind::Vec4: (void)props.GetOrAdd<glm::vec4>(property.Name, {0, 0, 1, 1}); break;
        default: FAIL() << "Unexpected output kind";
        }
    };
    for (auto* property : {&curvature.Mean, &curvature.Gaussian, &curvature.MinPrincipal,
                          &curvature.MaxPrincipal, &curvature.Direction1, &curvature.Direction2})
        add("mesh.processing.curvature", "Mesh / Processing / Curvature", *property);
    for (auto* property : {&segmentation.Components, &segmentation.Regions, &segmentation.RegionColors,
                          &segmentation.Boundaries, &segmentation.BoundaryColors, &segmentation.HardFeatures,
                          &segmentation.FeatureConfidence, &segmentation.BoundaryRoles, &segmentation.FeatureColors})
        add("mesh.processing.segmentation", "Mesh / Processing / Curvature Segmentation", *property);
    add("mesh.processing.geodesics", "Mesh / Geodesics / Virtual Source Propagation", geodesics.DistanceProperty);
    add("mesh.processing.geodesics", "Mesh / Geodesics / Virtual Source Propagation", geodesics.SourceMaskProperty);
    for (auto* property : {&clustering.Properties->OutputLabels, &clustering.Properties->OutputColors})
        add("mesh.processing.kmeans", "Mesh / Processing / K-Means", *property);
    add("mesh.processing.parameterize_uv", "Mesh / Processing / Parameterize (UV)", parameterization.Texcoords);
    R::SetParameterizationConfig(config, parameterization);
    for (auto* property : {&poisson.Level, &poisson.Rank, &poisson.SplatRadius, &poisson.PrefixVisible})
    {
        property->Domain = R::GeometryElementDomain::MeshVertex;
        add("mesh.processing.progressive_poisson", "Mesh / Processing / Progressive Poisson", *property);
    }
    poisson.Positions.Domain = R::GeometryElementDomain::MeshVertex;
    R::SetProgressivePoissonPlaygroundConfig(config, poisson);
    R::SetMeshCurvatureConfig(config, curvature);
    R::SetCurvatureSegmentationConfig(config, segmentation);
    R::SetGeodesicsConfig(config, geodesics);
    R::SetClusteringConfig(config, clustering);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen(actions.front().Window, true));
    std::size_t action = 0;
    int step = 0, frame = 0;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frame > 500) { ADD_FAILURE() << "Show actions did not finish: " << actions[action].Title; engine.RequestExit(); return; }
        const auto& show = actions[action];
        auto* window = ImGui::FindWindowByName(show.Title);
        if (!window) return;
        ImGui::SetWindowSize(window, {750, 1600});
        ImGui::SetWindowPos(window, {0, 0});
        if (step == 1)
        {
            ImGui::SetScrollY(window, 0);
            ImGui::FocusWindow(window);
        }
        if (step == 3)
            ImGui::ActivateItemByID(window->GetID(std::string_view{show.Window} == "mesh.processing.curvature"
                ? "Entity##MeshCurvature" : std::string_view{show.Window} == "mesh.processing.geodesics"
                    ? "Entity##Geodesics" : "Entity##Processing"));
        if (step == 5)
        {
            auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
            EXPECT_FALSE(popups.empty()) << show.Title;
            if (!popups.empty() && popups.back().Window)
            {
                const auto title = "Entity " + std::to_string(static_cast<std::uint32_t>(entity)) +
                    " (" + std::to_string(stableId) + ")";
                ImGui::ActivateItemByID(popups.back().Window->GetID(title.c_str()));
            }
        }
        if (show.Property.ValueKind == Geometry::PropertyValueKind::Vec2)
        {
            auto* parent = window;
            for (auto* child : ImGui::GetCurrentContext()->Windows)
                if (child->ParentWindow == parent && std::string_view{child->Name}.find("ParameterizationControls") != std::string_view::npos)
                { window = child; break; }
        }
        ImGui::SetWindowSize(window, {750, 1600});
        ImGui::SetWindowPos(window, {0, 0});
        if (step == 8) { ImGui::FocusWindow(window); ImGui::SetScrollY(window, window->ScrollMax.y); }
        if (step == 10) ImGui::ActivateItemByID(window->GetID(("Show " + show.Property.Name).c_str()));
        if (++step != 13) return;
        SCOPED_TRACE(show.Property.Name);
        const auto* lanes = scene.Raw().try_get<G::VisualizationLaneOverrides>(entity);
        EXPECT_NE(lanes, nullptr);
        if (lanes)
        {
            const auto& lane = show.Property.Domain == R::GeometryElementDomain::MeshEdge ? lanes->Edges : lanes->Surface;
            EXPECT_TRUE(lane);
            if (lane)
            {
                const bool scalar = show.Property.ValueKind == Geometry::PropertyValueKind::Double || show.Property.ValueKind == Geometry::PropertyValueKind::Float;
                EXPECT_EQ(scalar ? lane->ScalarFieldName : lane->ColorBufferName, show.Property.Name);
            }
        }
        EXPECT_TRUE(h.Shell.SetEditorWindowOpen(show.Window, false));
        if (++action == actions.size()) { engine.RequestExit(); return; }
        EXPECT_TRUE(h.Shell.SetEditorWindowOpen(actions[action].Window, true));
        step = 0;
    };
    h.Engine->Run();
    EXPECT_EQ(action, actions.size());
    ASSERT_EQ(h.Selection().SelectedStableIds().size(), 1u);
    EXPECT_EQ(h.Selection().SelectedStableIds().front(), R::SelectionController::ToStableEntityId(sceneEntity));
    EXPECT_FALSE(scene.Raw().all_of<G::VisualizationLaneOverrides>(sceneEntity));
    EXPECT_EQ(R::GetMeshCurvatureConfig(h.Control().GetEngineConfigControlState().ActiveConfig)->StableEntityId, stableId);
}

TEST(SandboxProcessingPanels, GeodesicsFollowsEntityAndResetsMeshLocalSources)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto first = scene.Create(), second = scene.Create();
    for (const auto entity : {first, second})
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto geodesics = *R::GetGeodesicsConfig(config);
    geodesics.SourceVertices = {0u};
    R::SetGeodesicsConfig(config, geodesics);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("mesh.processing.geodesics", true));
    int frame = 0;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        auto* window = ImGui::FindWindowByName("Mesh / Geodesics / Virtual Source Propagation");
        if (!window) return;
        ImGui::SetWindowSize(window, {750, 1600});
        ImGui::SetWindowPos(window, {0, 0});
        ImGui::FocusWindow(window);
        // Opening without a mesh must not consume the initial configured sources.
        if (frame == 1) EXPECT_TRUE(h.Selection().SetSelectedEntity(scene, first));
        if (frame == 3 || frame == 12)
            ImGui::ActivateItemByID(window->GetID("Compute geodesics"));
        if (frame == 6)
        {
            EXPECT_TRUE(scene.Raw().get<GS::Vertices>(first).Properties.Exists(geodesics.DistanceProperty.Name));
            EXPECT_FALSE(scene.Raw().get<GS::Vertices>(second).Properties.Exists(geodesics.DistanceProperty.Name));
            EXPECT_TRUE(h.Selection().SetSelectedEntity(scene, second));
        }
        if (frame == 9)
        {
            EXPECT_TRUE(R::GetGeodesicsConfig(h.Control().GetEngineConfigControlState().ActiveConfig)->SourceVertices.empty());
            ImGui::ActivateItemByID(window->GetID("Add source"));
        }
        if (frame == 15)
        {
            EXPECT_TRUE(scene.Raw().get<GS::Vertices>(second).Properties.Exists(geodesics.DistanceProperty.Name));
            h.Selection().ClearSelection(scene);
        }
        if (++frame == 19) engine.RequestExit();
    };
    h.Engine->Run();
    EXPECT_EQ(frame, 19);
    EXPECT_TRUE(h.Selection().SelectedStableIds().empty());
    EXPECT_TRUE(R::GetGeodesicsConfig(h.Control().GetEngineConfigControlState().ActiveConfig)->SourceVertices.empty());
}

TEST(SandboxProcessingPanels, EntityDefaultsFollowSelectionChangesAndPreserveExplicitChoices)
{
    R::EditorSelectionModel selection;
    Editor::ProcessingEntityInput source, target;
    auto sync = [&] {
        Editor::SynchronizeProcessingEntity(selection, source.PreviousSelection, source.Entity);
        Editor::SynchronizeProcessingEntity(selection, target.PreviousSelection, target.Entity, 1u);
    };
    sync();
    source.Entity = 7; target.Entity = 9;
    sync();
    EXPECT_EQ(source.Entity, 7u);
    EXPECT_EQ(target.Entity, 9u);
    // A later slot (the registration target) follows only a selection that reaches it:
    // selecting just the source, or nothing, keeps the chosen target.
    selection.SelectedEntities = {{.StableEntityId=3}};
    sync();
    EXPECT_EQ(source.Entity, 3u);
    EXPECT_EQ(target.Entity, 9u);
    target.Entity = 4;
    sync();
    EXPECT_EQ(target.Entity, 4u);
    selection.SelectedEntities.clear();
    sync();
    EXPECT_EQ(source.Entity, 0u);
    EXPECT_EQ(target.Entity, 4u);
    selection.SelectedEntities = {{.StableEntityId=3}, {.StableEntityId=5}};
    sync();
    EXPECT_EQ(source.Entity, 3u);
    EXPECT_EQ(target.Entity, 5u);
}

TEST(SandboxProcessingPanels, ExplicitEntityModelsLeaveSceneSelectionAndCachesUntouched)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto selected = scene.Create(), input = scene.Create();
    PopulateSamples(scene.Raw(), selected, R::GeometryElementDomain::MeshVertex);
    PopulateSamples(scene.Raw(), input, R::GeometryElementDomain::MeshVertex);
    const auto selectedId = R::SelectionController::ToStableEntityId(selected);
    const auto inputId = R::SelectionController::ToStableEntityId(input);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, selected));
    auto& props = scene.Raw().get<GS::Vertices>(input).Properties;
    (void)props.GetOrAdd<double>("v:input_only", 2.0);
    (void)props.GetOrAdd<glm::vec2>("v:texcoord", {0.3f, 0.8f});
    int checks = 0;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id="test.explicit_entity_models", .MenuPath={"View"}, .Title="Explicit entity model observer",
        .OpenByDefault=true,
        .Draw=[&](bool&, const Editor::SandboxEditorContext& context) {
            const auto chosen = R::BuildEditorDomainWindowModel(context.SnapshotQueries,
                R::EditorDomainWindowKind::Mesh, nullptr, inputId);
            const auto inspector = R::BuildEditorInspectorModel(context.SnapshotQueries, nullptr, inputId);
            const auto uv = R::BuildEditorParameterizationViewModel(
                context.Parameterization.Commands, context.Parameterization.Results, inputId);
            const auto global = R::BuildEditorDomainWindowModel(context.SnapshotQueries, R::EditorDomainWindowKind::Mesh);
            EXPECT_EQ(chosen.SelectedStableId, inputId);
            EXPECT_EQ(inspector.Entity.StableEntityId, inputId);
            EXPECT_EQ(uv.SelectedStableEntityId, inputId);
            EXPECT_TRUE(uv.HasUvCoordinates);
            EXPECT_EQ(global.SelectedStableId, selectedId);
            auto hasInput = [](const auto& catalog) {
                return std::ranges::any_of(catalog.Rows, [](const auto& row) { return row.Name == "v:input_only"; });
            };
            EXPECT_TRUE(hasInput(chosen.PropertyCatalog));
            EXPECT_TRUE(hasInput(inspector.PropertyCatalog));
            EXPECT_FALSE(hasInput(global.PropertyCatalog));
            EXPECT_FALSE(R::BuildEditorDomainWindowModel(context.SnapshotQueries,
                R::EditorDomainWindowKind::Mesh, nullptr, 0u).HasSelectedEntity);
            EXPECT_FALSE(R::BuildEditorInspectorModel(context.SnapshotQueries, nullptr, 0u).HasEntity);
            EXPECT_EQ(h.Selection().SelectedStableIds().front(), selectedId);
            ++checks;
        }});
    int frames = 0;
    h.Driver->OnFrame = [&](R::Engine& engine) { if (++frames == 4) engine.RequestExit(); };
    h.Engine->Run();
    EXPECT_GE(checks, 2);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

TEST(SandboxProcessingPanels, NormalInputsPreserveDomainFiltersAndPersistSelections)
{
    struct Control
    {
        const char* Window;
        const char* Title;
        const char* Combo;
        std::function<R::GeometryPropertyRef(const Config::EngineConfig&)> Normal;
        bool AllowsUnresolvedDomain{};
    };
    const std::array controls{
        Control{"view.descriptor_analysis", "FPFH Descriptor Analysis", "Normals##Descriptors",
            [](const auto& c) { return R::GetDescriptorAnalysisConfig(c)->Normals; }},
        Control{"view.bilateral_filter", "Bilateral Point Filter", "Normals##Bilateral",
            [](const auto& c) { return R::GetBilateralFilterConfig(c)->Normals; }},
        Control{"view.point_construction", "Construct from Points", "Normals",
            [](const auto& c) { return R::GetPointConstructionConfig(c)->Normals; }, true}};
    for (const auto& control : controls)
    for (const bool unresolved : {false, true})
    {
        SCOPED_TRACE(control.Title);
        SCOPED_TRACE(unresolved);
        PanelHarness h;
        auto& scene = h.Scene();
        const auto entity = scene.Create();
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
        ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
        const auto id = R::SelectionController::ToStableEntityId(entity);
        (void)scene.Raw().get<GS::Vertices>(entity).Properties.GetOrAdd<glm::vec3>(
            "v:alternate_normal", {0, 0, 1});
        (void)scene.Raw().get<GS::Faces>(entity).Properties.GetOrAdd<glm::vec3>(
            "f:alternate_normal", {0, 0, 1});
        const auto domain = unresolved ? R::GeometryElementDomain::Unknown
                                       : R::GeometryElementDomain::MeshVertex;
        auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
        auto descriptors = *R::GetDescriptorAnalysisConfig(config);
        descriptors.StableEntityId = id;
        descriptors.Positions.Domain = descriptors.Normals.Domain = domain;
        for (auto& output : descriptors.Outputs) output.Domain = domain;
        R::SetDescriptorAnalysisConfig(config, descriptors);
        auto bilateral = *R::GetBilateralFilterConfig(config);
        bilateral.StableEntityId = id;
        bilateral.Positions.Domain = bilateral.Normals.Domain = bilateral.Output.Domain = domain;
        R::SetBilateralFilterConfig(config, bilateral);
        auto construction = *R::GetPointConstructionConfig(config);
        construction.StableEntityId = id;
        construction.Positions.Domain = construction.Normals.Domain = domain;
        construction.EstimateNormals = false;
        R::SetPointConstructionConfig(config, construction);
        ASSERT_TRUE(h.Apply(config));
        ASSERT_TRUE(h.Shell.SetEditorWindowOpen(control.Window, true));
        const auto jobsBefore = h.Engine->Jobs().Stats().SubmittedJobs;
        int step = 0, frames = 0;
        bool completed = false;
        h.Driver->OnFrame = [&](R::Engine& engine) {
            if (++frames > 80) { ADD_FAILURE() << "Normal selector did not finish"; engine.RequestExit(); return; }
            auto* window = ImGui::FindWindowByName(control.Title);
            if (!window) return;
            ImGui::SetWindowSize(window, {750, 1800});
            ImGui::SetWindowPos(window, {0, 0});
            if (step == 1) { ImGui::FocusWindow(window); ImGui::SetScrollY(window, 0); }
            if (step == 3) ImGui::ActivateItemByID(window->GetID(control.Combo));
            if (step == 5)
            {
                ImGui::GetCurrentContext()->LogBuffer.clear();
                ImGui::LogToBuffer();
                ImGui::GetCurrentContext()->LogWindow = nullptr;
            }
            if (step == 7)
            {
                const std::string log{ImGui::GetCurrentContext()->LogBuffer.c_str()};
                ImGui::LogFinish();
                EXPECT_EQ(log.find("v:alternate_normal") != std::string::npos,
                    !unresolved || control.AllowsUnresolvedDomain);
                EXPECT_EQ(log.find("f:alternate_normal") != std::string::npos,
                    unresolved && control.AllowsUnresolvedDomain);
                auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
                ASSERT_FALSE(popups.empty());
                ASSERT_NE(popups.back().Window, nullptr);
                if (!unresolved)
                {
                    const auto label = std::string{R::ToString(domain)} + ": v:alternate_normal (9)";
                    ImGui::ActivateItemByID(popups.back().Window->GetID(label.c_str()));
                }
            }
            if (++step != 11) return;
            const auto normal = control.Normal(h.Control().GetEngineConfigControlState().ActiveConfig);
            EXPECT_EQ(normal.Domain, domain);
            EXPECT_EQ(normal.Name, unresolved ? "v:normal" : "v:alternate_normal");
            EXPECT_EQ(h.Selection().SelectedStableIds().front(), id);
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
            completed = true;
            engine.RequestExit();
        };
        h.Engine->Run();
        if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
        EXPECT_TRUE(completed);
    }
}

TEST(SandboxProcessingPanels, BackendControlsPersistTheRequestedExecutionPath)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto normals = *R::GetNormalEstimationConfig(config);
    normals.Method = R::NormalEstimationMethod::PointSetPCA;
    R::SetNormalEstimationConfig(config, normals);
    ASSERT_TRUE(h.Apply(config));
    struct Control
    {
        const char* Window;
        const char* Title;
        const char* Combo;
        const char* GpuChoice;
        std::function<int(const Config::EngineConfig&)> Backend;
        int ChoiceIndex{2}, ExpectedBackend{2};
    };
    const std::array controls{
        Control{"view.normal_estimation", "Normal Estimation", "Backend##Normals", "Vulkan LBVH (resident PCA)",
            [](const auto& c) { return int(R::GetNormalEstimationConfig(c)->Backend); }},
        Control{"view.outlier_analysis", "Outlier Analysis", "Backend", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetOutlierAnalysisConfig(c)->Backend); }},
        Control{"view.keypoint_analysis", "ISS Keypoint Analysis", "Acceleration", "Vulkan LBVH neighborhoods",
            [](const auto& c) { return int(R::GetKeypointAnalysisConfig(c)->Backend); }},
        Control{"view.keypoint_analysis", "ISS Keypoint Analysis", "Backend", "Vulkan",
            [](const auto& c) { return int(R::GetKeypointAnalysisConfig(c)->Backend); },1,3},
        Control{"view.descriptor_analysis", "FPFH Descriptor Analysis", "Backend", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetDescriptorAnalysisConfig(c)->Backend); }},
        Control{"view.kernel_density", "Kernel Density", "Backend", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetKernelDensityConfig(c)->Backend); }},
        Control{"view.density_weights", "Compact Density Weights", "Backend", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetDensityWeightConfig(c)->Backend); }},
        Control{"view.point_construction", "Construct from Points", "Backend", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetPointConstructionConfig(c)->Backend); }},
        Control{"view.point_spacing", "Point Spacing and Radii", "Backend", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetPointSpacingConfig(c)->Backend); }},
        Control{"view.bilateral_filter", "Bilateral Point Filter", "Backend", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetBilateralFilterConfig(c)->Backend); }},
        Control{"view.registration", "ICP Registration", "Backend##ICP", "Vulkan LBVH (CPU solve)",
            [](const auto& c) { return int(R::GetRegistrationConfig(c)->Backend); }},
        Control{"view.coherent_point_drift", "Coherent Point Drift", "Backend (E-step)##CPD",
            "Vulkan (dense on the GPU while wide, fp32 terms)",
            [](const auto& c) { return int(R::GetCoherentPointDriftConfig(c)->EStep); }, 6, 6},
    };
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen(controls.front().Window, true));
    std::size_t action = 0;
    int step = 0, frames = 0;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frames > 180) { ADD_FAILURE() << "Backend controls did not finish"; engine.RequestExit(); return; }
        const auto& control = controls[action];
        auto* window = ImGui::FindWindowByName(control.Title);
        if (!window) return;
        ImGui::SetWindowSize(window, {750, 2200});
        ImGui::SetWindowPos(window, {0, 0});
        if (step == 1) { ImGui::FocusWindow(window); ImGui::SetScrollY(window, 0); }
        if (step == 3) ImGui::ActivateItemByID(window->GetID(control.Combo));
        if (step == 5)
        {
            auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
            EXPECT_FALSE(popups.empty()) << control.Title;
            if (!popups.empty() && popups.back().Window)
            {
                const auto seed = popups.back().Window->GetID(control.ChoiceIndex);
                ImGui::ActivateItemByID(ImHashStr(control.GpuChoice, 0, seed));
            }
        }
        if (++step != 9) return;
        EXPECT_EQ(control.Backend(h.Control().GetEngineConfigControlState().ActiveConfig), control.ExpectedBackend) << control.Title;
        EXPECT_TRUE(h.Shell.SetEditorWindowOpen(control.Window, false));
        if (++action == controls.size()) { engine.RequestExit(); return; }
        EXPECT_TRUE(h.Shell.SetEditorWindowOpen(controls[action].Window, true));
        step = 0;
    };
    h.Engine->Run();
    EXPECT_EQ(action, controls.size());
}

TEST(SandboxProcessingPanels, MeshOperationUsesChosenEntityWithoutChangingSceneSelection)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto selected = scene.Create(), target = scene.Create();
    PopulateSamples(scene.Raw(), selected, R::GeometryElementDomain::MeshVertex);
    PopulateSamples(scene.Raw(), target, R::GeometryElementDomain::MeshVertex);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, selected));
    const auto targetId = R::SelectionController::ToStableEntityId(target);
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("mesh.processing.subdivide", true));
    int step = 0, frames = 0;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frames > 30) { ADD_FAILURE() << "Subdivision UI did not complete"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("Mesh / Processing / Subdivide");
        if (!window) return;
        ImGui::SetWindowSize(window, {750, 1200});
        ImGui::SetWindowPos(window, {0, 0});
        if (step == 1) ImGui::FocusWindow(window);
        if (step == 3) ImGui::ActivateItemByID(window->GetID("Entity##Processing"));
        if (step == 5)
        {
            auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
            EXPECT_FALSE(popups.empty());
            if (!popups.empty() && popups.back().Window)
            {
                const auto title = "Entity " + std::to_string(static_cast<std::uint32_t>(target)) +
                    " (" + std::to_string(targetId) + ")";
                ImGui::ActivateItemByID(popups.back().Window->GetID(title.c_str()));
            }
        }
        if (step == 8) ImGui::ActivateItemByID(window->GetID("Subdivide##MeshSubdivide"));
        if (++step != 12) return;
        EXPECT_EQ(scene.Raw().get<GS::Faces>(selected).Properties.Size(), 8u);
        EXPECT_EQ(scene.Raw().get<GS::Faces>(target).Properties.Size(), 32u);
        EXPECT_EQ(h.Selection().SelectedStableIds().front(), R::SelectionController::ToStableEntityId(selected));
        engine.RequestExit();
    };
    h.Engine->Run();
    EXPECT_EQ(step, 12);
}

TEST(SandboxProcessingPanels, SharedScalarPanelsRunConfiguredOutputAndShowWithoutRecomputing)
{
    for (const bool spacing : {false, true})
    {
        SCOPED_TRACE(spacing ? "spacing" : "density");
        PanelHarness h;
        auto& scene = h.Scene();
        const auto entity = scene.Create();
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
        ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
        auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
        const std::string output = "shared_test_scalar";
        R::EditorProcessingContext referenceContext{.Scene=&scene};
        if (spacing)
        {
            auto c = *R::GetPointSpacingConfig(config);
            c.KNeighbors = 3; c.ScaleFactor = 1.5f; c.Radii.Name = output;
            R::SetPointSpacingConfig(config, c);
            c.StableEntityId = R::SelectionController::ToStableEntityId(entity);
            c.Radii.Name = "reference_scalar";
            ASSERT_TRUE(R::ApplyEditorPointSpacingCommand(R::BindEditorProcessingCommands(referenceContext), c).Succeeded());
        }
        else
        {
            auto c = *R::GetKernelDensityConfig(config);
            c.KNeighbors = 3; c.Bandwidth = 0.5f; c.Density.Name = output;
            R::SetKernelDensityConfig(config, c);
            c.StableEntityId = R::SelectionController::ToStableEntityId(entity);
            c.Density.Name = "reference_scalar";
            ASSERT_TRUE(R::ApplyEditorKernelDensityCommand(R::BindEditorProcessingCommands(referenceContext), c).Succeeded());
        }
        ASSERT_TRUE(h.Apply(config));
        ASSERT_TRUE(h.Shell.SetEditorWindowOpen(spacing ? "view.point_spacing" : "view.kernel_density", true));
        auto& properties = scene.Raw().get<GS::Vertices>(entity).Properties;
        std::optional<Geometry::PropertyRevision> revision;
        std::uint64_t submittedBeforeShow = 0;
        int frame = 0, step = 0, showFrame = 0;
        h.Driver->OnFrame = [&](R::Engine& engine) {
            if (++frame > 400)
            { ADD_FAILURE() << "Scalar panel did not publish/show output"; engine.RequestExit(); return; }
            auto* window = ImGui::FindWindowByName(spacing ? "Point Spacing and Radii" : "Kernel Density");
            if (!window) return;
            ImGui::SetWindowSize(window, {700, 1000});
            ImGui::SetWindowPos(window, {0, 0});
            if (++step == 3)
                ImGui::ActivateItemByID(window->GetID(spacing ? "Estimate radii" : "Estimate density"));
            if (!properties.Exists(output)) return;
            if (!showFrame)
            {
                EXPECT_EQ(std::as_const(properties).Get<float>(output).Vector(),
                          std::as_const(properties).Get<float>("reference_scalar").Vector());
                revision = properties.FindPropertyRevision(output);
                submittedBeforeShow = engine.Jobs().Stats().SubmittedJobs;
                ImGui::ActivateItemByID(window->GetID(spacing ? "Show radii" : "Show density"));
                showFrame = frame;
            }
            if (frame < showFrame + 3) return;
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, submittedBeforeShow);
            EXPECT_EQ(properties.FindPropertyRevision(output), revision);
            const auto* overrides = scene.Raw().try_get<G::VisualizationLaneOverrides>(entity);
            EXPECT_NE(overrides, nullptr);
            if (overrides)
            {
                EXPECT_TRUE(overrides->Points);
                if (overrides->Points) EXPECT_EQ(overrides->Points->ScalarFieldName, output);
            }
            engine.RequestExit();
        };
        h.Engine->Run();
        EXPECT_GT(showFrame, 0);
    }
}

TEST(SandboxProcessingPanels, ReusedExecutionPanelsRejectInvalidRequestsBeforePublishing)
{
    struct Method
    {
        const char* Window;
        const char* Title;
        const char* Run;
        const char* Parameter;
    };
    constexpr std::array methods{
        Method{"view.keypoint_analysis", "ISS Keypoint Analysis", "Detect keypoints", "Salient radius (0 = automatic)"},
        Method{"view.descriptor_analysis", "FPFH Descriptor Analysis", "Compute FPFH descriptors", "Feature radius (0 = automatic)"},
        Method{"view.density_weights", "Compact Density Weights", "Compute compact weights", "Support radius"},
        Method{"view.bilateral_filter", "Bilateral Point Filter", "Filter positions", "Normal sigma"},
        Method{"view.normal_estimation", "Normal Estimation", "Estimate normals", "Radius##Normals"}};
    for (std::size_t method = 0; method < methods.size(); ++method)
    {
        SCOPED_TRACE(methods[method].Title);
        PanelHarness h;
        auto& scene = h.Scene();
        const auto entity = scene.Create();
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
        const auto other = scene.Create();
        PopulateSamples(scene.Raw(), other, R::GeometryElementDomain::PointCloudPoint);
        ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
        const auto id = R::SelectionController::ToStableEntityId(entity);
        auto& props = scene.Raw().get<GS::Vertices>(entity).Properties;
        (void)props.GetOrAdd<glm::vec3>("input_normals", {0, 0, 1});
        auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
        auto missingInputConfig = config;
        R::EditorProcessingContext referenceContext{.Scene = &scene};
        const std::string output = "configured_output", reference = "reference_output";
        if (method == 0)
        {
            auto c = *R::GetKeypointAnalysisConfig(config);
            c.StableEntityId = id; c.SalientRadius = 2; c.NonMaxRadius = 1;
            c.Score.Name = output; c.Mask.Name = "configured_mask";
            R::SetKeypointAnalysisConfig(config, c);
            c.Positions.Name = "missing_positions";
            R::SetKeypointAnalysisConfig(missingInputConfig, c);
            c.Positions.Name = "v:position";
            c.Score.Name = reference; c.Mask.Name = "reference_mask";
            ASSERT_TRUE(R::ApplyEditorKeypointAnalysisCommand(R::BindEditorProcessingCommands(referenceContext), c).Succeeded());
        }
        else if (method == 1)
        {
            auto c = *R::GetDescriptorAnalysisConfig(config);
            c.StableEntityId = id; c.FeatureRadius = 2; c.Normals.Name = "input_normals";
            c.Outputs = R::MakeDescriptorOutputProperties(c.Positions.Domain, "configured");
            c.Outputs[0].Name = output;
            R::SetDescriptorAnalysisConfig(config, c);
            c.Positions.Name = "missing_positions";
            R::SetDescriptorAnalysisConfig(missingInputConfig, c);
            c.Positions.Name = "v:position";
            c.Outputs = R::MakeDescriptorOutputProperties(c.Positions.Domain, "reference");
            c.Outputs[0].Name = reference;
            ASSERT_TRUE(R::ApplyEditorDescriptorAnalysisCommand(R::BindEditorProcessingCommands(referenceContext), c).Succeeded());
        }
        else if (method == 2)
        {
            auto c = *R::GetDensityWeightConfig(config);
            c.StableEntityId = id; c.SupportRadius = 2; c.Weights.Name = output;
            R::SetDensityWeightConfig(config, c);
            c.Positions.Name = "missing_positions";
            R::SetDensityWeightConfig(missingInputConfig, c);
            c.Positions.Name = "v:position";
            c.Weights.Name = reference;
            ASSERT_TRUE(R::ApplyEditorDensityWeightCommand(R::BindEditorProcessingCommands(referenceContext), c).Succeeded());
        }
        else if (method == 3)
        {
            auto c = *R::GetBilateralFilterConfig(config);
            c.StableEntityId = id; c.KNeighbors = 3; c.SpatialSigma = 1; c.NormalSigma = 2;
            c.Normals.Name = "input_normals"; c.Output.Name = output;
            R::SetBilateralFilterConfig(config, c);
            c.Positions.Name = "missing_positions";
            R::SetBilateralFilterConfig(missingInputConfig, c);
            c.Positions.Name = "v:position";
            c.Output.Name = reference;
            ASSERT_TRUE(R::ApplyEditorBilateralFilterCommand(R::BindEditorProcessingCommands(referenceContext), c).Succeeded());
        }
        else
        {
            auto c = *R::GetNormalEstimationConfig(config);
            c.StableEntityId = id; c.UseRadiusSearch = true; c.Radius = 2;
            c.Output.Name = output;
            R::SetNormalEstimationConfig(config, c);
            c.Positions.Name = "missing_positions";
            R::SetNormalEstimationConfig(missingInputConfig, c);
            c.Positions.Name = "v:position";
            c.Output.Name = reference;
            ASSERT_TRUE(R::ApplyEditorNormalEstimationCommand(R::BindEditorProcessingCommands(referenceContext), c).Succeeded());
        }
        ASSERT_TRUE(h.Apply(config));
        ASSERT_TRUE(h.Shell.SetEditorWindowOpen(methods[method].Window, true));
        int frame = 0, step = 0, shownPhase = 0, shownAt = 0;
        bool completed = false;
        std::uint64_t jobsBefore = 0;
        std::string acceptedConfig;
        h.Driver->OnFrame = [&](R::Engine& engine) {
            if (++frame > 400)
            { ADD_FAILURE() << "Processing panel did not publish"; engine.RequestExit(); return; }
            auto* window = ImGui::FindWindowByName(methods[method].Title);
            if (!window) return;
            ImGui::SetWindowSize(window, {750, 1400});
            ImGui::SetWindowPos(window, {0, 0});
            ++step;
            if (step == 2)
            {
                jobsBefore = engine.Jobs().Stats().SubmittedJobs;
                acceptedConfig = Config::SerializeEngineConfig(h.Control().GetEngineConfigControlState().ActiveConfig);
            }
            EditScalarControl(window, methods[method].Parameter, step - 3, "-1");
            if (step == 10) ImGui::ActivateItemByID(window->GetID(methods[method].Run));
            if (step == 13)
            {
                EXPECT_FALSE(props.Exists(output));
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                EXPECT_EQ(Config::SerializeEngineConfig(h.Control().GetEngineConfigControlState().ActiveConfig),
                          acceptedConfig) << "Rejected controls must leave the accepted configuration intact";
            }
            EditScalarControl(window, methods[method].Parameter, step - 14, "2");
            if (step == 21) EXPECT_TRUE(h.Apply(missingInputConfig));
            if (step == 24) ImGui::ActivateItemByID(window->GetID(methods[method].Run));
            if (step == 27)
            {
                EXPECT_FALSE(props.Exists(output));
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                EXPECT_TRUE(h.Apply(config));
            }
            if (step == 30) ImGui::ActivateItemByID(window->GetID(methods[method].Run));
            if (step < 30) return;
            if (!props.Exists(output)) return;
            const auto& values = std::as_const(props);
            if (method >= 3)
                EXPECT_EQ(values.Get<glm::vec3>(output).Vector(), values.Get<glm::vec3>(reference).Vector());
            else
                EXPECT_EQ(values.Get<float>(output).Vector(), values.Get<float>(reference).Vector());
            if (method == 0)
                EXPECT_EQ(values.Get<std::uint32_t>("configured_mask").Vector(),
                          values.Get<std::uint32_t>("reference_mask").Vector());
            // The run is on screen, and only while its own entity is the panel's entity.
            const auto capture = [] {
                ImGui::GetCurrentContext()->LogBuffer.clear();
                ImGui::LogToBuffer();
                ImGui::GetCurrentContext()->LogWindow = nullptr;
            };
            const auto read = [] {
                std::string text{ImGui::GetCurrentContext()->LogBuffer.c_str()};
                ImGui::LogFinish();
                return text;
            };
            if (shownPhase == 0) { shownPhase = 1; shownAt = frame; capture(); }
            if (shownPhase == 1 && frame == shownAt + 5)
            {
                EXPECT_NE(read().find("done"), std::string::npos) << "the finished run stays visible";
                EXPECT_TRUE(h.Selection().SetSelectedEntity(scene, other));
                shownPhase = 2; shownAt = frame;
            }
            if (shownPhase == 2 && frame == shownAt + 3) capture();
            if (shownPhase == 2 && frame == shownAt + 8)
            {
                EXPECT_EQ(read().find("done"), std::string::npos) << "another entity must not show this run";
                completed = true;
                engine.RequestExit();
            }
        };
        h.Engine->Run();
        if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
        EXPECT_TRUE(completed);
    }
}

TEST(SandboxProcessingPanels, ParameterizationRejectsLostDraftAndRetriesWithoutFurtherEdits)
{
    bool reject = false;
    unsigned rejections = 0;
    PanelHarness h(RejectableConfigRegistry(R::kParameterizationConfigSectionName, reject, rejections));
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    auto& props = scene.Raw().get<GS::Vertices>(entity).Properties;
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto parameters = *R::GetParameterizationConfig(config);
    parameters.Texcoords.Name = "v:panel_uv";
    parameters.Lscm.MaxSolverIterations = 1000;
    R::SetParameterizationConfig(config, parameters);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("mesh.processing.parameterize_uv", true));
    std::optional<R::EditorParameterizationResult> result;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.parameterization_execution", .MenuPath = {"View"}, .Title = "Parameterization observer",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            result = context.Parameterization.Results.LastParameterizationResult;
        }});
    int frames = 0, step = 0;
    unsigned rejectedApplyCount = 0;
    glm::vec2 firstUv{};
    bool completed = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frames > 100) { ADD_FAILURE() << "Parameterization panel did not complete"; engine.RequestExit(); return; }
        auto* parent = ImGui::FindWindowByName("Mesh / Processing / Parameterize (UV)");
        if (!parent) return;
        ImGui::SetWindowSize(parent, {1500, 1800});
        ImGui::SetWindowPos(parent, {0, 0});
        ImGuiWindow* window = nullptr;
        for (auto* child : ImGui::GetCurrentContext()->Windows)
            if (child->ParentWindow == parent && std::string_view{child->Name}.find("ParameterizationControls") != std::string_view::npos)
            { window = child; break; }
        if (!window) return;
        ++step;
        EditScalarControl(window, "Maximum iterations##Parameterization", step - 3, "1234");
        const auto active = [&] { return *R::GetParameterizationConfig(h.Control().GetEngineConfigControlState().ActiveConfig); };
        const auto run = [&] { ImGui::ActivateItemByID(window->GetID("Parameterize selected mesh##Parameterization")); };
        if (step == 12)
        {
            EXPECT_EQ(active().Lscm.MaxSolverIterations, 1000u);
            reject = true;
            ImGui::ActivateItemByID(window->GetID("Apply configuration##Parameterization"));
        }
        if (step == 16)
        {
            EXPECT_GT(rejections, 0u);
            rejectedApplyCount = rejections;
            EXPECT_EQ(active().Lscm.MaxSolverIterations, 1000u);
            EXPECT_FALSE(props.Exists(parameters.Texcoords.Name));
            EXPECT_FALSE(result);
            run();
        }
        if (step == 20)
        {
            EXPECT_GT(rejections, rejectedApplyCount);
            EXPECT_EQ(active().Lscm.MaxSolverIterations, 1000u);
            EXPECT_FALSE(props.Exists(parameters.Texcoords.Name));
            EXPECT_FALSE(result);
            reject = false;
            run();
        }
        if (step == 24)
        {
            EXPECT_EQ(active().Lscm.MaxSolverIterations, 1234u);
            EXPECT_TRUE(result && result->Succeeded()) << (result ? result->Message : "No result");
            auto uv = props.Get<glm::vec2>(parameters.Texcoords.Name);
            EXPECT_TRUE(uv);
            if (!uv) { engine.RequestExit(); return; }
            EXPECT_EQ(uv.Vector().size(), 9u);
            firstUv = uv[0];
            // Change only the output to prove a NoChange config still executes.
            uv[0] = {123.f, 456.f};
            run();
        }
        if (step == 28)
        {
            const auto uv = props.Get<glm::vec2>(parameters.Texcoords.Name);
            EXPECT_TRUE(uv);
            if (uv) EXPECT_EQ(uv[0], firstUv);
            EXPECT_EQ(active().Lscm.MaxSolverIterations, 1234u);
            EXPECT_TRUE(result && result->Succeeded());
            EXPECT_FALSE(props.Exists("v:texcoord"));
            completed = true;
            engine.RequestExit();
        }
    };
    h.Engine->Run();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

TEST(SandboxProcessingPanels, ProgressivePoissonManualAndDebouncedRunsShareConfigApply)
{
    bool reject = false;
    unsigned rejections = 0;
    PanelHarness h(RejectableConfigRegistry(R::kProgressivePoissonConfigSectionName, reject, rejections));
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    auto& props = scene.Raw().get<GS::Vertices>(entity).Properties;
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto poisson = *R::GetProgressivePoissonPlaygroundConfig(config);
    poisson.Dimension = 2; poisson.GridWidth = 3; poisson.MaxLevels = 5;
    // The float widget's widened request differs from this serialized double,
    // so an injected fallback really loses an edit even on a manual Run.
    poisson.RadiusAlpha = .4;
    poisson.AutoRunOnEdit = true; poisson.DebounceSeconds = .25;
    R::SetProgressivePoissonPlaygroundConfig(config, poisson);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("pointcloud.processing.progressive_poisson", true));
    std::optional<R::EditorProgressivePoissonResult> result;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.poisson_execution", .MenuPath = {"View"}, .Title = "Poisson execution observer",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            result = context.PointSet.Results.LastProgressivePoissonResult;
        }});
    int frame = 0, step = 0, phase = 0;
    std::uint64_t jobsBefore = 0;
    bool completed = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frame > 400) { ADD_FAILURE() << "Poisson panel did not complete"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("PointCloud / Processing / Progressive Poisson");
        if (!window) return;
        ImGui::SetWindowSize(window, {850, 1400});
        ImGui::SetWindowPos(window, {0, 0});
        ++step;
        const auto run = [&] { ImGui::ActivateItemByID(window->GetID("Run Progressive Poisson##ProgressivePoisson")); };
        const auto active = [&] { return *R::GetProgressivePoissonPlaygroundConfig(h.Control().GetEngineConfigControlState().ActiveConfig); };
        if (phase == 0)
        {
            if (step == 2) jobsBefore = engine.Jobs().Stats().SubmittedJobs;
            if (step == 3) { reject = true; run(); }
            if (step == 7)
            {
                EXPECT_GT(rejections, 0u);
                EXPECT_FALSE(result);
                EXPECT_FALSE(props.Exists(poisson.Rank.Name));
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                EXPECT_DOUBLE_EQ(active().RadiusAlpha, .4);
                reject = false;
                run();
                phase = 1; step = 0;
            }
        }
        else if (phase == 1 && result && result->Succeeded())
        {
            EXPECT_TRUE(props.Exists(poisson.Rank.Name));
            EXPECT_EQ(props.Size(), 9u);
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore + 1);
            EXPECT_DOUBLE_EQ(active().RadiusAlpha, double(float(.4)));
            jobsBefore = engine.Jobs().Stats().SubmittedJobs;
            phase = 2; step = 0;
        }
        else if (phase == 2)
        {
            EditScalarControl(window, "Grid width##ProgressivePoisson", step - 3, "5");
            if (step == 10)
            {
                EXPECT_EQ(active().GridWidth, 5u);
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                // Change the accepted config during debounce, then make the
                // widget's widened request fail. Advance UI time without sleep.
                auto updated = h.Control().GetEngineConfigControlState().ActiveConfig;
                auto value = active(); value.RadiusAlpha = .41;
                R::SetProgressivePoissonPlaygroundConfig(updated, value);
                EXPECT_TRUE(h.Apply(updated));
                rejections = 0; reject = true;
                ImGui::GetCurrentContext()->Time += 1.;
            }
            if (step == 14)
            {
                EXPECT_GT(rejections, 0u);
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                EXPECT_DOUBLE_EQ(active().RadiusAlpha, .41);
                reject = false;
                ImGui::GetCurrentContext()->Time += 1.;
            }
            if (step == 18)
            {
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore)
                    << "A rejected auto-run must not retry every frame";
                run();
                phase = 3; step = 0;
            }
        }
        else if (phase == 3 && result && result->Succeeded() &&
                 engine.Jobs().Stats().SubmittedJobs > jobsBefore)
        {
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore + 1);
            EXPECT_DOUBLE_EQ(active().RadiusAlpha, double(float(.41)));
            jobsBefore = engine.Jobs().Stats().SubmittedJobs;
            phase = 4; step = 0;
        }
        else if (phase == 4)
        {
            EditScalarControl(window, "Grid width##ProgressivePoisson", step - 3, "7");
            if (step == 10) ImGui::GetCurrentContext()->Time += 1.;
            if (step > 12 && result && result->Succeeded() &&
                engine.Jobs().Stats().SubmittedJobs > jobsBefore)
            {
                EXPECT_EQ(active().GridWidth, 7u);
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore + 1);
                EXPECT_EQ(props.Size(), 9u);
                completed = true;
                engine.RequestExit();
            }
        }
    };
    h.Engine->Run();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

TEST(SandboxProcessingPanels, OutlierActionsApplyTheirOwnRequestAndRetryRejectedConfig)
{
    bool reject = false;
    unsigned rejections = 0;
    PanelHarness h(RejectableConfigRegistry(R::kOutlierAnalysisConfigSectionName, reject, rejections));
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    auto& props = scene.Raw().get<GS::Vertices>(entity).Properties;
    props.Get<glm::vec3>("v:position")[8] = {100, 100, 100};
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto outliers = *R::GetOutlierAnalysisConfig(config);
    outliers.StableEntityId = R::SelectionController::ToStableEntityId(entity);
    outliers.Method = R::OutlierAnalysisMethod::Radius;
    outliers.Radius = 1.1f; outliers.MinimumNeighbors = 1;
    R::SetOutlierAnalysisConfig(config, outliers);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.outlier_analysis", true));
    std::optional<R::EditorOutlierAnalysisResult> result;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.outlier_execution", .MenuPath = {"View"}, .Title = "Outlier execution observer",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            result = context.PointAnalysis.Results.LastOutlierAnalysisResult;
        }});
    int frame = 0, step = 0, phase = 0;
    std::uint64_t jobsBefore = 0;
    bool completed = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frame > 400) { ADD_FAILURE() << "Outlier panel did not complete"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("Outlier Analysis");
        if (!window) return;
        ImGui::SetWindowSize(window, {750, 1400});
        ImGui::SetWindowPos(window, {0, 0});
        ++step;
        const auto click = [&](const char* label) { ImGui::ActivateItemByID(window->GetID(label)); };
        const auto active = [&] { return *R::GetOutlierAnalysisConfig(h.Control().GetEngineConfigControlState().ActiveConfig); };
        if (phase == 0)
        {
            if (step == 2) jobsBefore = engine.Jobs().Stats().SubmittedJobs;
            if (step == 3) click("Remove marked points");
            if (step == 6)
            {
                EXPECT_FALSE(result);
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                EXPECT_EQ(props.Size(), 9u);
                rejections = 0; reject = true;
            }
            EditScalarControl(window, "Radius", step - 6, "1.25");
            if (step == 14) click("Detect outliers");
            if (step == 18)
            {
                EXPECT_GT(rejections, 0u);
                EXPECT_FALSE(result);
                EXPECT_FALSE(props.Exists(outliers.Mask.Name));
                EXPECT_EQ(active().Radius, outliers.Radius);
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                reject = false;
                click("Detect outliers");
                phase = 1; step = 0;
            }
        }
        else if (phase == 1 && result && result->Succeeded())
        {
            EXPECT_EQ(result->Operation, R::OutlierAnalysisOperation::Analyze);
            EXPECT_EQ(result->RejectedCount, 1u);
            EXPECT_EQ(active().Operation, R::OutlierAnalysisOperation::Analyze);
            EXPECT_EQ(props.Size(), 9u);
            rejections = 0; reject = true;
            click("Remove marked points");
            phase = 2; step = 0;
        }
        else if (phase == 2 && step == 4)
        {
            EXPECT_GT(rejections, 0u);
            EXPECT_EQ(props.Size(), 9u);
            EXPECT_EQ(result->Operation, R::OutlierAnalysisOperation::Analyze);
            EXPECT_EQ(active().Operation, R::OutlierAnalysisOperation::Analyze);
            reject = false;
            click("Remove marked points");
            phase = 3; step = 0;
        }
        else if (phase == 3 && result && result->Operation == R::OutlierAnalysisOperation::RemoveMarked)
        {
            EXPECT_TRUE(result->Succeeded()) << result->Message;
            EXPECT_EQ(props.Size(), 8u);
            EXPECT_EQ(active().Operation, R::OutlierAnalysisOperation::RemoveMarked);
            click("Detect outliers");
            phase = 4; step = 0;
        }
        else if (phase == 4 && result && result->Succeeded() && result->Operation == R::OutlierAnalysisOperation::Analyze)
        {
            EXPECT_EQ(active().Operation, R::OutlierAnalysisOperation::Analyze);
            EXPECT_EQ(result->LiveCount, 8u);
            EXPECT_EQ(props.Size(), 8u);
            completed = true;
            engine.RequestExit();
        }
    };
    h.Engine->Run();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

TEST(SandboxProcessingPanels, RegistrationRetriesConfigBeforeRunningAndPreservesTrajectoryChoice)
{
    bool reject = false;
    unsigned rejections = 0;
    PanelHarness h(RejectableConfigRegistry(R::kRegistrationConfigSectionName, reject, rejections));
    auto& scene = h.Scene();
    const auto source = scene.Create(), target = scene.Create();
    using Transform = Extrinsic::ECS::Components::Transform::Component;
    for (const auto entity : {source, target})
    {
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
        scene.Raw().emplace<Transform>(entity);
    }
    scene.Raw().get<Transform>(target).Position = {0.25f, 0.1f, 0.2f};
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, source));
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto registration = *R::GetRegistrationConfig(config);
    registration.SourceStableEntityId = R::SelectionController::ToStableEntityId(source);
    registration.TargetStableEntityId = R::SelectionController::ToStableEntityId(target);
    registration.TrajectoryStep = 1;
    registration.InlierRatio = 1;
    R::SetRegistrationConfig(config, registration);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.registration", true));
    std::optional<R::EditorRegistrationResult> result;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.registration_execution", .MenuPath = {"View"}, .Title = "Registration execution observer",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            result = context.Registration.Results.LastRegistrationResult;
        }});
    int frame = 0, step = 0, phase = 0;
    std::uint64_t jobsBefore = 0;
    bool completed = false;
    glm::vec3 finalPosition{};
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frame > 400) { ADD_FAILURE() << "ICP panel did not complete"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("ICP Registration");
        if (!window) return;
        ImGui::SetWindowSize(window, {750, 1400});
        ImGui::SetWindowPos(window, {0, 0});
        ++step;
        const auto run = [&] { ImGui::ActivateItemByID(window->GetID("Run ICP##ICP")); };
        const auto active = [&] { return *R::GetRegistrationConfig(h.Control().GetEngineConfigControlState().ActiveConfig); };
        if (phase == 0)
        {
            if (step == 2) jobsBefore = engine.Jobs().Stats().SubmittedJobs;
            if (step == 2) EXPECT_TRUE(h.Apply(config));
            if (step == 3) { rejections = 0; reject = true; run(); }
            if (step == 7)
            {
                EXPECT_GT(rejections, 0u);
                EXPECT_FALSE(result);
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                EXPECT_EQ(active().TrajectoryStep, 1u);
                EXPECT_EQ(scene.Raw().get<Transform>(source).Position, glm::vec3(0));
                reject = false;
                run();
                phase = 1; step = 0;
            }
        }
        else if (phase == 1 && result && result->Succeeded())
        {
            EXPECT_TRUE(result->HasResult);
            EXPECT_GT(result->AppliedStep, 0u);
            EXPECT_EQ(result->AppliedStep, std::min(std::size_t(registration.MaxIterations), result->TrajectoryLength));
            EXPECT_EQ(active().TrajectoryStep, registration.MaxIterations);
            finalPosition = scene.Raw().get<Transform>(source).Position;
            EXPECT_NE(finalPosition, glm::vec3(0));
            jobsBefore = engine.Jobs().Stats().SubmittedJobs;
            phase = 2; step = 0;
        }
        else if (phase == 2)
        {
            EditScalarControl(window, "Apply trajectory step (0 = start)##ICP", step - 3, "0");
            if (step > 9 && result && result->Succeeded() && result->AppliedStep == 0)
            {
                EXPECT_EQ(active().TrajectoryStep, 0u);
                EXPECT_EQ(scene.Raw().get<Transform>(source).Position, finalPosition);
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore + 1);
                jobsBefore = engine.Jobs().Stats().SubmittedJobs;
                phase = 3; step = 0;
            }
        }
        else if (phase == 3)
        {
            EditScalarControl(window, "Max iterations##ICP", step - 3, "0");
            if (step == 10) run();
            if (step == 14)
            {
                EXPECT_EQ(active().MaxIterations, registration.MaxIterations);
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                registration.SourcePositions.Name = "missing_positions";
                R::SetRegistrationConfig(config, registration);
                EXPECT_TRUE(h.Apply(config));
                phase = 4; step = 0;
            }
        }
        else if (phase == 4)
        {
            EditScalarControl(window, "Apply trajectory step (0 = start)##ICP", step - 3, "0");
            if (step == 14)
            {
                EXPECT_EQ(active().TrajectoryStep, 0u);
                EXPECT_EQ(active().SourcePositions.Name, "missing_positions");
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                EXPECT_EQ(scene.Raw().get<Transform>(source).Position, finalPosition);
                completed = true;
                engine.RequestExit();
            }
        }
    };
    h.Engine->Run();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

TEST(SandboxProcessingPanels, UvAtlasAdoptionTracksNewExtentsAndPreservesManualSizing)
{
    PanelHarness h;
    std::optional<R::EditorUvRegenerationCommandResult> result{
        R::EditorUvRegenerationCommandResult{
            .Status = R::EditorCommandStatus::Applied,
            .AtlasWidth = 256u, .AtlasHeight = 128u}};
    std::optional<R::EditorUvRegenerationCommandResult> adopted;
    // Without a context the block submits default atlas config (padding 2).
    std::int32_t width = 1, height = 1, padding = 0;
    bool force = true, preserve = false;
    const Editor::SandboxUvRegenerationControls controls{
        .LastResult = &result, .LastExtentAdoption = &adopted,
        .BakeWidth = &width, .BakeHeight = &height, .BakePadding = &padding,
        .UvForceRegenerate = &force, .UvPreserveAuthored = &preserve};
    int checks = 0;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.uv_extent_adoption", .MenuPath = {"View"}, .Title = "UV extent adoption",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext&) {
            if (checks >= 3) return;
            if (checks == 1) width = 73;
            if (checks == 2) { result->AtlasWidth = 512u; result->AtlasHeight = 256u; }
            Editor::DrawSandboxUvRegenerationControls({}, nullptr, controls);
            EXPECT_EQ(width, checks == 0 ? 256 : checks == 1 ? 73 : 512);
            EXPECT_EQ(height, checks == 2 ? 256 : 128);
            EXPECT_EQ(padding, 2);
            ++checks;
        }});
    int frames = 0;
    h.Driver->OnFrame = [&](R::Engine& engine) { if (++frames == 5) engine.RequestExit(); };
    h.Engine->Run();
    EXPECT_EQ(checks, 3);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

TEST(SandboxProcessingPanels, UvRegenerationControlBlocksUnavailableAndPublishesQueuedResult)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
    R::EditorTextureBakeControlsModel model;
    std::optional<R::EditorUvRegenerationCommandResult> result, adoption;
    // The control submits the applied atlas config, whose default padding is 2.
    const std::int32_t padding = 2;
    std::int32_t bakeWidth = 1024, bakeHeight = 1024, bakePadding = 0;
    bool force = true, preserve = false;
    const Editor::SandboxUvRegenerationControls controls{
        &result, &adoption, &bakeWidth, &bakeHeight, &bakePadding, &force, &preserve};
    const auto windowHandle = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.uv_regeneration", .MenuPath = {"View"}, .Title = "UV controls test",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            if (context.Parameterization.Results.LastUvRegenerationResult)
                result = context.Parameterization.Results.LastUvRegenerationResult;
            if (ImGui::Begin("UV controls test"))
                Editor::DrawSandboxUvRegenerationControls(model, &context, controls);
            ImGui::End();
        }});
    int frames = 0, step = 0;
    std::uint64_t jobsBefore = 0u;
    bool completed = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frames > 400) { ADD_FAILURE() << "UV controls did not complete"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("UV controls test");
        if (!window) return;
        ImGui::SetWindowSize(window, {700, 700});
        ImGui::SetWindowPos(window, {0, 0});
        ++step;
        if (step == 2) jobsBefore = engine.Jobs().Stats().SubmittedJobs;
        if (step == 3) ImGui::ActivateItemByID(window->GetID("Regenerate UVs"));
        if (step == 6)
        {
            EXPECT_FALSE(result);
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
            model.SelectedStableId = R::SelectionController::ToStableEntityId(entity);
            ImGui::ActivateItemByID(window->GetID("Regenerate UVs"));
        }
        if (step > 8 && result && result->Status != R::EditorCommandStatus::Pending)
        {
            EXPECT_TRUE(result->Succeeded()) << result->Diagnostic;
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore + 1u);
            EXPECT_EQ(bakeWidth, static_cast<std::int32_t>(result->AtlasWidth));
            EXPECT_EQ(bakeHeight, static_cast<std::int32_t>(result->AtlasHeight));
            EXPECT_EQ(bakePadding, padding);
            EXPECT_TRUE(adoption);
            EXPECT_TRUE(scene.Raw().get<GS::Vertices>(entity).Properties.Exists("v:texcoord") ||
                        scene.Raw().get<GS::Halfedges>(entity).Properties.Exists("h:texcoord"));
            completed = true;
            engine.RequestExit();
        }
    };
    h.Engine->Run();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(windowHandle));
}

TEST(SandboxProcessingPanels, CurvatureRetriesRejectedDraftAndReexecutesUnchangedConfig)
{
    bool reject = false;
    unsigned rejections = 0;
    PanelHarness h(RejectableConfigRegistry(R::kMeshCurvatureConfigSectionName, reject, rejections));
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    auto& props = scene.Raw().get<GS::Vertices>(entity).Properties;
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto curvature = *R::GetMeshCurvatureConfig(config);
    curvature.Mean.Name = "v:curvature_retry";
    R::SetMeshCurvatureConfig(config, curvature);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("mesh.processing.curvature", true));
    std::optional<R::EditorMeshCurvatureResult> result;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.curvature_execution", .MenuPath = {"View"}, .Title = "Curvature observer",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            result = context.MeshFields.Results.LastMeshCurvatureResult;
        }});
    int frames = 0, step = 0, phase = 0;
    unsigned firstRejections = 0;
    std::uint64_t jobsBefore = 0;
    double expectedMean = 0;
    bool completed = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frames > 400) { ADD_FAILURE() << "Curvature retry did not complete"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("Mesh / Processing / Curvature");
        if (!window) return;
        ImGui::SetWindowSize(window, {800, 1500});
        ImGui::SetWindowPos(window, {0, 0});
        ++step;
        const auto run = [&] { ImGui::ActivateItemByID(window->GetID("Compute##MeshCurvature")); };
        const auto active = [&] { return *R::GetMeshCurvatureConfig(h.Control().GetEngineConfigControlState().ActiveConfig); };
        if (phase == 0)
        {
            if (step == 3)
            {
                reject = true;
                jobsBefore = engine.Jobs().Stats().SubmittedJobs;
                ImGui::ActivateItemByID(window->GetID("Principal directions##MeshCurvature"));
            }
            if (step == 6)
            {
                EXPECT_GT(rejections, 0u);
                firstRejections = rejections;
                EXPECT_TRUE(active().PublishPrincipalDirections);
                run();
            }
            if (step == 10)
            {
                EXPECT_GT(rejections, firstRejections);
                EXPECT_FALSE(result);
                EXPECT_FALSE(props.Exists(curvature.Mean.Name));
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                reject = false;
                run();
                phase = 1; step = 0;
            }
        }
        else if (phase == 1 && result && result->Succeeded())
        {
            EXPECT_FALSE(active().PublishPrincipalDirections);
            auto mean = props.Get<double>(curvature.Mean.Name);
            ASSERT_TRUE(mean);
            expectedMean = mean[0];
            mean[0] = 123.0;
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore + 1);
            run();
            phase = 2; step = 0;
        }
        else if (phase == 2 && step > 2 && props.Get<double>(curvature.Mean.Name)[0] == expectedMean)
        {
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore + 2);
            auto external = h.Control().GetEngineConfigControlState().ActiveConfig;
            auto updated = active(); updated.Mean.Name = "v:external_curvature";
            R::SetMeshCurvatureConfig(external, updated);
            ASSERT_TRUE(h.Apply(external));
            phase = 3; step = 0;
        }
        else if (phase == 3)
        {
            if (step == 3) run();
            if (props.Exists("v:external_curvature"))
            {
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore + 3);
                EXPECT_EQ(active().Mean.Name, "v:external_curvature");
                completed = true;
                engine.RequestExit();
            }
        }
    };
    h.Engine->Run();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

TEST(SandboxProcessingPanels, SegmentationPreservesExplicitDraftAndRefreshesActiveConfig)
{
    bool reject = false;
    unsigned rejections = 0;
    PanelHarness h(RejectableConfigRegistry(R::kCurvatureSegmentationConfigSectionName, reject, rejections));
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto segmentation = *R::GetCurvatureSegmentationConfig(config);
    segmentation.SelectionMode = R::CurvatureSegmentationSelectionMode::FixedCount;
    segmentation.FixedComponentCount = 1u;
    segmentation.Regions.Name = "f:segmentation_initial";
    R::SetCurvatureSegmentationConfig(config, segmentation);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("mesh.processing.segmentation", true));
    unsigned applyRejections = 0;
    int frames = 0, step = 0;
    bool completed = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frames > 300) { ADD_FAILURE() << "Segmentation draft test did not complete"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("Mesh / Processing / Curvature Segmentation");
        if (!window) return;
        ImGui::SetWindowSize(window, {900, 1500});
        ImGui::SetWindowPos(window, {0, 0});
        auto& props = scene.Raw().get<GS::Faces>(entity).Properties;
        ++step;
        const auto run = [&] { ImGui::ActivateItemByID(window->GetID("Run segmentation##CurvatureSegmentation")); };
        const auto active = [&] { return *R::GetCurvatureSegmentationConfig(h.Control().GetEngineConfigControlState().ActiveConfig); };
        if (step == 3)
        {
            ImGui::GetCurrentContext()->LogBuffer.clear();
            ImGui::LogToBuffer();
            // Capture across window End() calls until the deferred activation settles.
            ImGui::GetCurrentContext()->LogWindow = nullptr;
            run();
        }
        if (step == 6)
        {
            EXPECT_NE(std::string_view{ImGui::GetCurrentContext()->LogBuffer.c_str()}.find("Run segmentation"), std::string_view::npos);
            EXPECT_NE(std::string_view{ImGui::GetCurrentContext()->LogBuffer.c_str()}.find("Choose a mesh entity to run segmentation."), std::string_view::npos);
            ImGui::LogFinish();
            EXPECT_FALSE(props.Exists(segmentation.Regions.Name));
            ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
        }
        if (step >= 7 && step <= 12)
            EditScalarControl(window, "Components##CurvatureSegmentation", step - 7, "2");
        if (step == 14)
        {
            // Explicit Apply: editing alone must not publish configuration.
            EXPECT_EQ(active().FixedComponentCount, 1u);
            reject = true;
            ImGui::ActivateItemByID(window->GetID("Apply configuration##CurvatureSegmentation"));
        }
        if (step == 17)
        {
            EXPECT_GT(rejections, 0u);
            applyRejections = rejections;
            EXPECT_EQ(active().FixedComponentCount, 1u);
            run();
        }
        if (step == 20)
        {
            EXPECT_GT(rejections, applyRejections);
            EXPECT_EQ(active().FixedComponentCount, 1u);
            EXPECT_FALSE(props.Exists(segmentation.Regions.Name));
            reject = false;
            run();
        }
        if (step == 23)
        {
            EXPECT_EQ(active().FixedComponentCount, 2u);
            auto regions = props.Get<std::uint32_t>(segmentation.Regions.Name);
            ASSERT_TRUE(regions);
            regions[0] = 777u;
            run();
        }
        if (step == 26)
        {
            // NoChange configuration must still execute, not redisplay the old result.
            const auto regions = props.Get<std::uint32_t>(segmentation.Regions.Name);
            ASSERT_TRUE(regions);
            EXPECT_NE(regions[0], 777u);
            auto external = h.Control().GetEngineConfigControlState().ActiveConfig;
            auto updated = active(); updated.Regions.Name = "f:segmentation_external";
            R::SetCurvatureSegmentationConfig(external, updated);
            ASSERT_TRUE(h.Apply(external));
        }
        if (step == 29) run();
        if (step == 32)
        {
            EXPECT_EQ(active().Regions.Name, "f:segmentation_external");
            EXPECT_TRUE(props.Exists("f:segmentation_external"));
        }
        if (step >= 34 && step <= 39)
            EditScalarControl(window, "Components##CurvatureSegmentation", step - 34, "3");
        if (step == 41)
        {
            auto external = h.Control().GetEngineConfigControlState().ActiveConfig;
            auto updated = active(); updated.FixedComponentCount = 4u;
            R::SetCurvatureSegmentationConfig(external, updated);
            ASSERT_TRUE(h.Apply(external));
        }
        if (step == 44) run();
        if (step == 47)
        {
            // External writes cannot silently discard an explicit dirty draft.
            EXPECT_EQ(active().FixedComponentCount, 3u);
            EXPECT_EQ(active().Regions.Name, "f:segmentation_external");
        }
        if (step >= 49 && step <= 54)
            EditScalarControl(window, "Components##CurvatureSegmentation", step - 49, "5");
        if (step == 56)
            ImGui::ActivateItemByID(window->GetID("Reload active##CurvatureSegmentation"));
        if (step == 59)
        {
            auto regions = props.Get<std::uint32_t>("f:segmentation_external");
            ASSERT_TRUE(regions);
            regions[0] = 777u;
            run();
        }
        if (step == 62)
        {
            EXPECT_EQ(active().FixedComponentCount, 3u);
            const auto regions = props.Get<std::uint32_t>("f:segmentation_external");
            ASSERT_TRUE(regions);
            EXPECT_NE(regions[0], 777u);
            completed = true;
            engine.RequestExit();
        }
    };
    h.Engine->Run();
    EXPECT_TRUE(completed);
}

TEST(SandboxProcessingPanels, GeodesicsRetriesDraftAndKeepsRejectedEntityReset)
{
    bool reject = false;
    unsigned rejections = 0;
    PanelHarness h(RejectableConfigRegistry(R::kGeodesicsConfigSectionName, reject, rejections));
    auto& scene = h.Scene();
    const auto first = scene.Create(), second = scene.Create();
    for (const auto entity : {first, second})
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, first));
    auto& firstProps = scene.Raw().get<GS::Vertices>(first).Properties;
    auto& secondProps = scene.Raw().get<GS::Vertices>(second).Properties;
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto geodesics = *R::GetGeodesicsConfig(config);
    geodesics.SourceVertices = {0u};
    geodesics.MaxHalfedgeExpansions = 1000;
    R::SetGeodesicsConfig(config, geodesics);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("mesh.processing.geodesics", true));
    int frames = 0, step = 0;
    unsigned firstRejections = 0;
    double expectedDistance = 0;
    bool completed = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frames > 100) { ADD_FAILURE() << "Geodesics retry did not complete"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("Mesh / Geodesics / Virtual Source Propagation");
        if (!window) return;
        ImGui::SetWindowSize(window, {800, 1500});
        ImGui::SetWindowPos(window, {0, 0});
        ++step;
        const auto run = [&] { ImGui::ActivateItemByID(window->GetID("Compute geodesics")); };
        const auto active = [&] { return *R::GetGeodesicsConfig(h.Control().GetEngineConfigControlState().ActiveConfig); };
        if (step == 2) reject = true;
        EditScalarControl(window, "Expansion budget", step - 3, "1234");
        if (step == 12)
        {
            EXPECT_GT(rejections, 0u);
            firstRejections = rejections;
            EXPECT_EQ(active().MaxHalfedgeExpansions, 1000u);
            run();
        }
        if (step == 16)
        {
            EXPECT_GT(rejections, firstRejections);
            EXPECT_FALSE(firstProps.Exists(geodesics.DistanceProperty.Name));
            reject = false;
            run();
        }
        if (step == 20)
        {
            EXPECT_EQ(active().MaxHalfedgeExpansions, 1234u);
            auto distance = firstProps.Get<double>(geodesics.DistanceProperty.Name);
            ASSERT_TRUE(distance);
            expectedDistance = distance[0]; distance[0] = 123.0;
            run();
        }
        if (step == 24)
        {
            EXPECT_EQ(firstProps.Get<double>(geodesics.DistanceProperty.Name)[0], expectedDistance);
            reject = true;
            ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, second));
        }
        if (step == 28)
        {
            EXPECT_EQ(active().SourceVertices, (std::vector<std::uint32_t>{0u}));
            run();
        }
        if (step == 32)
        {
            EXPECT_FALSE(secondProps.Exists(geodesics.DistanceProperty.Name));
            reject = false;
            ImGui::ActivateItemByID(window->GetID("Add source"));
        }
        if (step == 36) run();
        if (step == 40)
        {
            EXPECT_TRUE(secondProps.Exists(geodesics.DistanceProperty.Name));
            auto external = h.Control().GetEngineConfigControlState().ActiveConfig;
            auto updated = active(); updated.DistanceProperty.Name = "v:external_distance";
            R::SetGeodesicsConfig(external, updated);
            ASSERT_TRUE(h.Apply(external));
        }
        if (step == 44) run();
        if (step == 48)
        {
            EXPECT_TRUE(secondProps.Exists("v:external_distance"));
            EXPECT_EQ(active().DistanceProperty.Name, "v:external_distance");
            completed = true;
            engine.RequestExit();
        }
    };
    h.Engine->Run();
    EXPECT_TRUE(completed);
}


TEST(SandboxProcessingPanels, TopologyAdmissionKeepsBlockedActionsVisibleAndRunsValidCommands)
{
    const std::array<std::pair<const char*, const char*>, 4> methods{{
        {"Denoise", "mesh.processing.denoise"}, {"Simplify", "mesh.processing.simplify"},
        {"Remesh", "mesh.processing.remesh"}, {"Subdivide", "mesh.processing.subdivide"}}};
    for (const auto& [name, windowId] : methods)
    {
        SCOPED_TRACE(name);
        const bool simplify = std::string_view{name} == "Simplify";
        PanelHarness h;
        auto& scene = h.Scene();
        const auto entity = scene.Create();
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
        const std::string title = std::string{"Mesh / Processing / "} + name;
        const std::string label = std::string{name} + "##Mesh" + name;
        ASSERT_TRUE(h.Shell.SetEditorWindowOpen(windowId, true));
        std::optional<R::EditorCommandStatus> status;
        std::string message;
        const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
            .Id = "test.topology_admission", .MenuPath = {"View"}, .Title = "Topology admission observer",
            .OpenByDefault = true,
            .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
                const auto copy = [&](const auto& result) {
                    if (result) { status = result->Status; message = result->Message; }
                };
                if (simplify) copy(context.MeshTopology.Results.LastMeshSimplifyResult);
                else if (std::string_view{name} == "Remesh") copy(context.MeshTopology.Results.LastMeshRemeshResult);
                else if (std::string_view{name} == "Subdivide") copy(context.MeshTopology.Results.LastMeshSubdivideResult);
                else copy(context.MeshTopology.Results.LastMeshDenoiseResult);
            }});
        int frames = 0, step = 0;
        std::uint64_t jobsBefore = 0u;
        bool completed = false;
        h.Driver->OnFrame = [&](R::Engine& engine) {
            if (++frames > 400) { ADD_FAILURE() << "Topology control did not finish"; engine.RequestExit(); return; }
            auto* window = ImGui::FindWindowByName(title.c_str());
            if (!window) return;
            ImGui::SetWindowSize(window, {900, 1500});
            ImGui::SetWindowPos(window, {0, 0});
            ++step;
            const auto run = [&] { ImGui::ActivateItemByID(window->GetID(label.c_str())); };
            if (step == 3)
            {
                jobsBefore = engine.Jobs().Stats().SubmittedJobs;
                ImGui::GetCurrentContext()->LogBuffer.clear();
                ImGui::LogToBuffer();
                ImGui::GetCurrentContext()->LogWindow = nullptr;
                run();
            }
            if (step == 6)
            {
                // The button itself was submitted even without a chosen entity.
                EXPECT_NE(std::string_view{ImGui::GetCurrentContext()->LogBuffer.c_str()}.find(
                    std::string{"[ "} + name + " ]"), std::string_view::npos);
                ImGui::LogFinish();
                EXPECT_FALSE(status);
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                EXPECT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
            }
            if (simplify && step >= 7 && step <= 12)
                EditScalarControl(window, "Target faces##MeshSimplify", step - 7, "0");
            if (step == 14) run();
            if (simplify && step == 17)
            {
                EXPECT_FALSE(status); // A selected mesh alone is not sufficient.
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
            }
            if (simplify && step >= 18 && step <= 23)
                EditScalarControl(window, "Target faces##MeshSimplify", step - 18, "2");
            if (simplify && step == 25) run();
            if (step > (simplify ? 27 : 16) && status && *status != R::EditorCommandStatus::Pending)
            {
                EXPECT_TRUE(*status == R::EditorCommandStatus::Applied ||
                            *status == R::EditorCommandStatus::NoChange) << message;
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore + 1u);
                completed = true;
                engine.RequestExit();
            }
        };
        h.Engine->Run();
        if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
        EXPECT_TRUE(completed);
        EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
    }
}


TEST(SandboxProcessingPanels, TopologyVariantWidgetsReachCommandsAndClearLoopOnlyFeatures)
{
    for (const bool remesh : {true, false})
    {
        SCOPED_TRACE(remesh ? "remesh" : "subdivide");
        PanelHarness h;
        auto& scene = h.Scene();
        const auto entity = scene.Create();
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
        (void)scene.Raw().get<GS::Edges>(entity).Properties.GetOrAdd<bool>("e:feature", false);
        ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
        ASSERT_TRUE(h.Shell.SetEditorWindowOpen(
            remesh ? "mesh.processing.remesh" : "mesh.processing.subdivide", true));
        std::optional<R::EditorMeshRemeshResult> remeshResult;
        std::optional<R::EditorMeshSubdivideResult> subdivideResult;
        const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
            .Id = "test.topology_variants", .MenuPath = {"View"}, .Title = "Topology variants observer",
            .OpenByDefault = true,
            .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
                remeshResult = context.MeshTopology.Results.LastMeshRemeshResult;
                subdivideResult = context.MeshTopology.Results.LastMeshSubdivideResult;
            }});
        int step = 0, frames = 0;
        bool completed = false;
        h.Driver->OnFrame = [&](R::Engine& engine) {
            if (++frames > 400) { ADD_FAILURE() << "Topology variants did not finish"; engine.RequestExit(); return; }
            auto* window = ImGui::FindWindowByName(remesh ? "Mesh / Processing / Remesh" : "Mesh / Processing / Subdivide");
            if (!window) return;
            ImGui::SetWindowSize(window, {900, 1500});
            ImGui::SetWindowPos(window, {0, 0});
            ++step;
            const auto click = [&](const char* label) { ImGui::ActivateItemByID(window->GetID(label)); };
            const auto choose = [&](const char* label) {
                auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
                ASSERT_FALSE(popups.empty());
                ASSERT_NE(popups.back().Window, nullptr);
                ImGui::ActivateItemByID(popups.back().Window->GetID(label));
            };
            if (step == 3)
            {
                if (!remesh)
                {
                    ImGui::GetCurrentContext()->LogBuffer.clear();
                    ImGui::LogToBuffer();
                    ImGui::GetCurrentContext()->LogWindow = nullptr;
                }
                click(remesh ? "Project to surface##MeshRemesh" : "Preserve Loop features##MeshSubdivide");
            }
            if (step == 6)
            {
                if (!remesh)
                {
                    EXPECT_NE(std::string_view{ImGui::GetCurrentContext()->LogBuffer.c_str()}.find(
                        "[x] Preserve Loop features"), std::string_view::npos);
                    ImGui::LogFinish();
                }
                click(remesh ? "Mode##MeshRemesh" : "Operator##MeshSubdivide");
            }
            if (step == 8) choose(remesh
                ? R::DebugNameForEditorMeshRemeshMode(R::EditorMeshRemeshMode::Adaptive)
                : R::DebugNameForEditorMeshSubdivideOperator(R::EditorMeshSubdivideOperator::CatmullClark));
            if (remesh && step == 11) click("Sizing law##MeshRemesh");
            if (remesh && step == 13) choose(R::DebugNameForEditorMeshRemeshSizingLaw(
                R::EditorMeshRemeshSizingLaw::ErrorBoundedTaubin));
            if (step == 16) click(remesh ? "Remesh##MeshRemesh" : "Subdivide##MeshSubdivide");
            if (step <= 18) return;
            if (remesh)
            {
                if (!remeshResult || remeshResult->Status == R::EditorCommandStatus::Pending) return;
                EXPECT_TRUE(remeshResult->Succeeded() || remeshResult->Status == R::EditorCommandStatus::NoChange)
                    << remeshResult->Message;
                EXPECT_EQ(remeshResult->Mode, R::EditorMeshRemeshMode::Adaptive);
                EXPECT_EQ(remeshResult->SizingLaw, R::EditorMeshRemeshSizingLaw::ErrorBoundedTaubin);
                EXPECT_TRUE(remeshResult->ProjectToSurface);
            }
            else
            {
                if (!subdivideResult || subdivideResult->Status == R::EditorCommandStatus::Pending) return;
                EXPECT_TRUE(subdivideResult->Succeeded()) << subdivideResult->Message;
                EXPECT_EQ(subdivideResult->Operator, R::EditorMeshSubdivideOperator::CatmullClark);
                EXPECT_FALSE(subdivideResult->PreserveLoopFeatureEdges);
            }
            completed = true;
            engine.RequestExit();
        };
        h.Engine->Run();
        if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
        EXPECT_TRUE(completed);
        EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
    }
}

TEST(SandboxProcessingPanels, ProgressivePoissonKeepsInputChooserVisibleAndRetriesRejectedBinding)
{
    bool reject = false;
    unsigned rejections = 0;
    PanelHarness h(RejectableConfigRegistry(R::kProgressivePoissonConfigSectionName, reject, rejections));
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
    auto& props = scene.Raw().get<GS::Vertices>(entity).Properties;
    auto positions = props.Get<glm::vec3>("v:position");
    auto custom = props.GetOrAdd<glm::vec3>("p:samples", {});
    custom.Vector() = positions.Vector();
    props.Remove(positions);
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto poisson = *R::GetProgressivePoissonPlaygroundConfig(config);
    poisson.AutoRunOnEdit = false;
    R::SetProgressivePoissonPlaygroundConfig(config, poisson);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("pointcloud.processing.progressive_poisson", true));
    std::optional<R::EditorProgressivePoissonResult> result;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.poisson_admission", .MenuPath = {"View"}, .Title = "Poisson admission observer",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            result = context.PointSet.Results.LastProgressivePoissonResult;
        }});
    int frame = 0, step = 0;
    std::uint64_t jobsBefore = 0;
    bool completed = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frame > 400) { ADD_FAILURE() << "Poisson custom input did not finish"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("PointCloud / Processing / Progressive Poisson");
        if (!window) return;
        ImGui::SetWindowSize(window, {850, 1500});
        ImGui::SetWindowPos(window, {0, 0});
        ++step;
        const auto click = [&](const char* label) { ImGui::ActivateItemByID(window->GetID(label)); };
        if (step == 3)
        {
            jobsBefore = engine.Jobs().Stats().SubmittedJobs;
            ImGui::GetCurrentContext()->LogBuffer.clear();
            ImGui::LogToBuffer();
            ImGui::GetCurrentContext()->LogWindow = nullptr;
            click("Run Progressive Poisson##ProgressivePoisson");
        }
        if (step == 6)
        {
            const std::string_view log{ImGui::GetCurrentContext()->LogBuffer.c_str()};
            EXPECT_NE(log.find("Positions"), std::string_view::npos);
            EXPECT_NE(log.find("Run Progressive Poisson"), std::string_view::npos);
            ImGui::LogFinish();
            EXPECT_FALSE(result);
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
            EXPECT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
        }
        if (step == 9) click("Run Progressive Poisson##ProgressivePoisson");
        if (step == 12)
        {
            EXPECT_FALSE(result); // Missing default input, but the chooser remains usable.
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
            reject = true;
            click("Positions##ProgressivePoisson");
        }
        if (step == 14)
        {
            auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
            ASSERT_FALSE(popups.empty());
            ASSERT_NE(popups.back().Window, nullptr);
            const std::string label = std::string{R::DebugNameForEditorPropertyCatalogDomain(
                R::EditorPropertyCatalogDomain::PointCloudPoints)} + " / p:samples (" +
                std::to_string(props.Size()) + ")";
            ImGui::ActivateItemByID(popups.back().Window->GetID(label.c_str()));
        }
        if (step == 17)
        {
            const auto active = R::GetProgressivePoissonPlaygroundConfig(
                h.Control().GetEngineConfigControlState().ActiveConfig);
            ASSERT_TRUE(active);
            EXPECT_EQ(active->Positions.Name, "v:position");
            EXPECT_GT(rejections, 0u);
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
            reject = false;
            // Retry without editing: the local binding must survive rejection.
            click("Run Progressive Poisson##ProgressivePoisson");
        }
        if (step > 19 && result && result->Status != R::EditorCommandStatus::Pending)
        {
            EXPECT_TRUE(result->Succeeded()) << result->Message;
            const auto active = R::GetProgressivePoissonPlaygroundConfig(
                h.Control().GetEngineConfigControlState().ActiveConfig);
            ASSERT_TRUE(active);
            EXPECT_EQ(active->Positions.Name, "p:samples");
            EXPECT_EQ(active->Positions.Domain, R::GeometryElementDomain::PointCloudPoint);
            EXPECT_TRUE(props.Exists(poisson.Level.Name));
            EXPECT_FALSE(props.Exists("v:position"));
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore + 1u);
            completed = true;
            engine.RequestExit();
        }
    };
    h.Engine->Run();
    if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

TEST(SandboxProcessingPanels, MeshFieldAdmissionBlocksMissingInputsAndRunsChosenProperty)
{
    for (const bool segmentation : {false, true})
    {
        SCOPED_TRACE(segmentation);
        PanelHarness h;
        auto& scene = h.Scene();
        const auto entity = scene.Create();
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
        auto& vertices = scene.Raw().get<GS::Vertices>(entity).Properties;
        auto positions = vertices.GetOrAdd<glm::vec3>("v:custom", {});
        positions.Vector() = vertices.Get<glm::vec3>("v:position").Vector();
        auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
        auto curvature = *R::GetMeshCurvatureConfig(config);
        auto segments = *R::GetCurvatureSegmentationConfig(config);
        curvature.Positions.Name = segments.Positions.Name = "v:missing";
        segments.SelectionMode = R::CurvatureSegmentationSelectionMode::FixedCount;
        segments.FixedComponentCount = 1u;
        R::SetMeshCurvatureConfig(config, curvature);
        R::SetCurvatureSegmentationConfig(config, segments);
        ASSERT_TRUE(h.Apply(config));
        ASSERT_TRUE(h.Shell.SetEditorWindowOpen(segmentation
            ? "mesh.processing.segmentation" : "mesh.processing.curvature", true));
        std::optional<R::EditorCommandStatus> status;
        std::string message;
        const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
            .Id = "test.mesh_field_admission", .MenuPath = {"View"}, .Title = "Mesh field admission observer",
            .OpenByDefault = true,
            .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
                const auto copy = [&](const auto& result) {
                    if (result) { status = result->Status; message = result->Message; }
                };
                if (!segmentation) copy(context.MeshFields.Results.LastMeshCurvatureResult);
            }});
        int frames = 0, step = 0;
        std::uint64_t jobsBefore = 0;
        bool completed = false;
        h.Driver->OnFrame = [&](R::Engine& engine) {
            if (++frames > 400) { ADD_FAILURE() << "Mesh field admission did not finish"; engine.RequestExit(); return; }
            auto* window = ImGui::FindWindowByName(segmentation
                ? "Mesh / Processing / Curvature Segmentation" : "Mesh / Processing / Curvature");
            if (!window) return;
            ImGui::SetWindowSize(window, {900, 2200});
            ImGui::SetWindowPos(window, {0, 0});
            ++step;
            const char* button = segmentation ? "Run segmentation##CurvatureSegmentation" : "Compute##MeshCurvature";
            const auto run = [&] { ImGui::ActivateItemByID(window->GetID(button)); };
            if (step == 3)
            {
                jobsBefore = engine.Jobs().Stats().SubmittedJobs;
                ImGui::GetCurrentContext()->LogBuffer.clear();
                ImGui::LogToBuffer();
                ImGui::GetCurrentContext()->LogWindow = nullptr;
                run();
            }
            if (step == 6)
            {
                const std::string_view log{ImGui::GetCurrentContext()->LogBuffer.c_str()};
                EXPECT_NE(log.find(segmentation ? "Run segmentation" : "Compute"), std::string_view::npos);
                ImGui::LogFinish();
                EXPECT_FALSE(status);
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                EXPECT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
            }
            if (step == 9) run();
            if (step == 12)
            {
                EXPECT_FALSE(status); // A valid mesh with a missing named input cannot run.
                EXPECT_FALSE(vertices.Exists(curvature.Mean.Name));
                EXPECT_FALSE(scene.Raw().get<GS::Faces>(entity).Properties.Exists(segments.Regions.Name));
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
                ImGui::ActivateItemByID(window->GetID(segmentation ? "Positions##Segmentation" : "Positions##MeshCurvature"));
            }
            if (step == 14)
            {
                auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
                ASSERT_FALSE(popups.empty());
                ASSERT_NE(popups.back().Window, nullptr);
                ImGui::ActivateItemByID(popups.back().Window->GetID("v:custom"));
            }
            if (step == 17) run();
            const bool published = segmentation
                ? scene.Raw().get<GS::Faces>(entity).Properties.Exists(segments.Regions.Name)
                : status && *status != R::EditorCommandStatus::Pending;
            if (step > 19 && published)
            {
                if (!segmentation) EXPECT_EQ(*status, R::EditorCommandStatus::Applied) << message;
                const auto& active = h.Control().GetEngineConfigControlState().ActiveConfig;
                EXPECT_EQ(segmentation ? R::GetCurvatureSegmentationConfig(active)->Positions.Name
                                       : R::GetMeshCurvatureConfig(active)->Positions.Name, "v:custom");
                if (segmentation) EXPECT_TRUE(scene.Raw().get<GS::Faces>(entity).Properties.Exists(segments.Regions.Name));
                else EXPECT_TRUE(vertices.Exists(curvature.Mean.Name));
                EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore + (segmentation ? 0u : 1u));
                completed = true;
                engine.RequestExit();
            }
        };
        h.Engine->Run();
        if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
        EXPECT_TRUE(completed);
        EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
    }
}

TEST(SandboxProcessingPanels, SegmentationFeaturePickerReusesPreparedLargeCatalog)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
    auto& faces = scene.Raw().get<GS::Faces>(entity).Properties;
    for (unsigned i = 0; i < 512; ++i)
        (void)faces.GetOrAdd<float>("f:feature_" + std::to_string(i), 1.0f);
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto segmentation = *R::GetCurvatureSegmentationConfig(config);
    segmentation.Features = {{R::GeometryElementDomain::MeshFace, "f:feature_0", Geometry::PropertyValueKind::Float}};
    R::SetCurvatureSegmentationConfig(config, segmentation);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("mesh.processing.segmentation", true));
    R::EditorPointInputReadinessStats readiness{};
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.segmentation_picker_budget", .MenuPath = {"View"}, .Title = "Segmentation picker budget",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            readiness = R::GetEditorPointInputReadinessStats(context.MeshFields.Commands);
        }});
    int frames = 0, step = 0;
    bool completed = false;
    R::EditorPointInputReadinessStats baseline{};
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frames > 100) { ADD_FAILURE() << "Segmentation picker did not finish"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("Mesh / Processing / Curvature Segmentation");
        if (!window) return;
        ImGui::SetWindowSize(window, {900, 2200});
        ImGui::SetWindowPos(window, {0, 0});
        ++step;
        if (step == 10)
        {
            ASSERT_GT(readiness.PropertyScans, 0u);
            baseline = readiness;
            const int featureIndex = 0;
            const auto featureScope = ImHashData(&featureIndex, sizeof(featureIndex), window->IDStack.back());
            ImGui::ActivateItemByID(ImHashStr("Feature##Segmentation", 0, featureScope));
        }
        if (step >= 13 && step <= 20)
        {
            const auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
            ASSERT_FALSE(popups.empty());
            ASSERT_NE(popups.back().Window, nullptr);
            EXPECT_EQ(readiness.PropertyScans, baseline.PropertyScans);
            EXPECT_EQ(readiness.ChecksQueued, baseline.ChecksQueued);
            const auto& stats = h.Shell.GetLastFrame().ModelBuildStats;
            EXPECT_EQ(stats.PropertyCatalogModelBuilds, 0u);
            // The panel owns a fresh domain wrapper each frame; its catalog
            // metadata comes from the persistent selected-analysis cache.
            EXPECT_EQ(stats.DomainWindowModelBuilds, 1u);
            EXPECT_EQ(stats.SelectedAnalysisCacheMisses, 0u);
            EXPECT_GT(stats.SelectedAnalysisCacheHits, 0u);
        }
        if (step == 20) { completed = true; engine.RequestExit(); }
    };
    h.Engine->Run();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

TEST(SandboxProcessingPanels, KMeansAdmissionKeepsControlsVisibleAndRetriesRejectedDraft)
{
    bool reject = false;
    unsigned rejections = 0;
    PanelHarness h(RejectableConfigRegistry(R::kClusteringConfigSectionName, reject, rejections), true);
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
    auto& props = scene.Raw().get<GS::Vertices>(entity).Properties;
    auto positions = props.Get<glm::vec3>("v:position");
    auto custom = props.GetOrAdd<glm::vec3>("p:samples", {});
    custom.Vector() = positions.Vector();
    props.Remove(positions);
    auto conflict = props.GetOrAdd<float>("p:kmeans_color", 0);
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("pointcloud.processing.kmeans", true));
    std::optional<R::KMeansRunCompleted> result;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.kmeans_admission", .MenuPath = {"View"}, .Title = "K-Means admission observer",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            result = context.PointCloudService->Results.LastKMeansResult;
            if (!h.Selection().SelectedStableIds().empty())
            {
                const auto request = R::MakeConfiguredKMeansRequest(
                    R::SelectionController::ToStableEntityId(entity),
                    *R::GetClusteringConfig(h.Control().GetEngineConfigControlState().ActiveConfig));
                auto customRequest = request;
                customRequest.Properties.InputPositions.Name = "p:samples";
                const auto readiness = R::PreviewEditorKMeansRun(context.PointCloudService->Commands,
                    context.PointCloudService->Clustering, customRequest);
                if (props.Exists("p:kmeans_color") && !props.Get<glm::vec4>("p:kmeans_color"))
                {
                    EXPECT_FALSE(readiness.Enabled);
                    const auto rejected = R::SubmitKMeansRun(context.PointCloudService->Commands,
                        context.PointCloudService->Clustering, customRequest);
                    EXPECT_EQ(rejected.Message, readiness.DisabledReason);
                    EXPECT_EQ(rejected.World, h.Engine->ActiveWorld());
                    EXPECT_FALSE(rejected.Correlation.IsValid());
                }
            }
        }});
    int frame = 0, step = 0;
    bool completed = false;
    std::uint64_t jobsBefore = 0;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frame > 400) { ADD_FAILURE() << "K-Means did not finish"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("PointCloud / Processing / K-Means");
        if (!window) return;
        ImGui::SetWindowSize(window, {850, 1500});
        ImGui::SetWindowPos(window, {0, 0});
        ++step;
        const auto click = [&](const char* label) { ImGui::ActivateItemByID(window->GetID(label)); };
        if (step == 3)
        {
            jobsBefore = engine.Jobs().Stats().SubmittedJobs;
            ImGui::GetCurrentContext()->LogBuffer.clear();
            ImGui::LogToBuffer();
            ImGui::GetCurrentContext()->LogWindow = nullptr;
            click("Run K-Means##KMeans");
        }
        if (step == 6)
        {
            const std::string_view log{ImGui::GetCurrentContext()->LogBuffer.c_str()};
            EXPECT_NE(log.find("Positions"), std::string_view::npos);
            EXPECT_NE(log.find("Run K-Means"), std::string_view::npos);
            ImGui::LogFinish();
            EXPECT_FALSE(result);
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
            EXPECT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
        }
        if (step == 9) click("Run K-Means##KMeans");
        if (step == 12)
        {
            EXPECT_FALSE(result);
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
            props.Remove(conflict);
            reject = true;
            click("Positions##KMeans");
        }
        if (step == 14)
        {
            auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
            ASSERT_FALSE(popups.empty());
            ASSERT_NE(popups.back().Window, nullptr);
            const std::string label = std::string{R::DebugNameForEditorPropertyCatalogDomain(
                R::EditorPropertyCatalogDomain::PointCloudPoints)} + " / p:samples (" +
                std::to_string(props.Size()) + ")";
            ImGui::ActivateItemByID(popups.back().Window->GetID(label.c_str()));
        }
        if (step == 17) click("Run K-Means##KMeans");
        if (step == 21)
        {
            EXPECT_GT(rejections, 0u);
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore);
            EXPECT_FALSE(R::GetClusteringConfig(h.Control().GetEngineConfigControlState().ActiveConfig)->Properties);
            reject = false;
            click("Run K-Means##KMeans");
        }
        if (step > 23 && result && result->Status != R::KMeansRunStatus::Queued)
        {
            EXPECT_TRUE(result->Succeeded()) << result->Message;
            EXPECT_EQ(result->Properties.InputPositions.Name, "p:samples");
            EXPECT_TRUE(props.Get<std::uint32_t>("p:kmeans_label"));
            EXPECT_TRUE(props.Get<glm::vec4>("p:kmeans_color"));
            EXPECT_FALSE(props.Exists("v:position"));
            EXPECT_EQ(engine.Jobs().Stats().SubmittedJobs, jobsBefore + 1u);
            completed = true;
            engine.RequestExit();
        }
    };
    h.Engine->Run();
    if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

// The run's progress belongs to the entity it ran on: after the selection moves to
// another entity nothing of it shows, and it comes back with the original selection.
TEST(SandboxProcessingPanels, KMeansProgressShowsOnlyForTheEntityItRanOn)
{
    PanelHarness h(Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry(), true);
    auto& scene = h.Scene();
    const auto a = scene.Create(), b = scene.Create();
    PopulateSamples(scene.Raw(), a, R::GeometryElementDomain::PointCloudPoint);
    PopulateSamples(scene.Raw(), b, R::GeometryElementDomain::PointCloudPoint);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, a));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("pointcloud.processing.kmeans", true));
    std::optional<R::KMeansRunCompleted> result;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.kmeans_progress_observer", .MenuPath = {"View"}, .Title = "K-Means progress observer",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            result = context.PointCloudService->Results.LastKMeansResult;
        }});
    int frame = 0, step = 0, phase = 0;
    float heightWithRun = 0.0f, heightOtherEntity = 0.0f;
    bool completed = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frame > 600) { ADD_FAILURE() << "K-Means progress test did not finish"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("PointCloud / Processing / K-Means");
        if (!window) return;
        ImGui::SetWindowSize(window, {850, 1500});
        ImGui::SetWindowPos(window, {0, 0});
        ++step;
        if (phase == 0 && step == 3) ImGui::ActivateItemByID(window->GetID("Run K-Means##KMeans"));
        if (phase == 0 && step > 5 && result && result->Status != R::KMeansRunStatus::Queued)
        {
            EXPECT_TRUE(result->Succeeded()) << result->Message;
            phase = 1; step = 0;
        }
        if (phase == 1 && step == 4)
        {
            heightWithRun = window->ContentSize.y; // "done" bar of the run on A
            EXPECT_TRUE(h.Selection().SetSelectedEntity(scene, b));
        }
        if (phase == 1 && step == 8)
        {
            heightOtherEntity = window->ContentSize.y;
            EXPECT_LT(heightOtherEntity, heightWithRun) << "B must not show A's run";
            EXPECT_TRUE(h.Selection().SetSelectedEntity(scene, a));
        }
        if (phase == 1 && step == 12)
        {
            EXPECT_FLOAT_EQ(window->ContentSize.y, heightWithRun) << "A's outcome is remembered";
            completed = true;
            engine.RequestExit();
        }
    };
    h.Engine->Run();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

// A point-family panel finds its run by entity and output name through the shared helper
// and keeps the finished run visible for that output.
TEST(SandboxProcessingPanels, KernelDensityPanelShowsItsRunForTheOutputItWrites)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto density = *R::GetKernelDensityConfig(config);
    density.KNeighbors = 3;
    density.Bandwidth = 0.5f;
    density.Density.Name = "progress_density";
    R::SetKernelDensityConfig(config, density);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.kernel_density", true));
    auto& properties = scene.Raw().get<GS::Vertices>(entity).Properties;
    int frame = 0, step = 0, publishedAt = 0;
    std::string text;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frame > 400) { ADD_FAILURE() << "density progress test did not finish"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("Kernel Density");
        if (!window) return;
        ImGui::SetWindowSize(window, {700, 1000});
        ImGui::SetWindowPos(window, {0, 0});
        if (++step == 3) ImGui::ActivateItemByID(window->GetID("Estimate density"));
        if (!publishedAt && properties.Exists("progress_density"))
        {
            publishedAt = frame;
            ImGui::GetCurrentContext()->LogBuffer.clear();
            ImGui::LogToBuffer();
            ImGui::GetCurrentContext()->LogWindow = nullptr;
        }
        if (publishedAt && frame == publishedAt + 5)
        {
            text = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
            engine.RequestExit();
        }
    };
    h.Engine->Run();
    if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
    EXPECT_GT(publishedAt, 0);
    EXPECT_NE(text.find("done"), std::string::npos) << text;
}

// Progressive Poisson names its run by the channel it writes; the finished run stays visible.
TEST(SandboxProcessingPanels, ProgressivePoissonPanelShowsItsFinishedRun)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto poisson = *R::GetProgressivePoissonPlaygroundConfig(config);
    poisson.AutoRunOnEdit = false;
    R::SetProgressivePoissonPlaygroundConfig(config, poisson);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("pointcloud.processing.progressive_poisson", true));
    std::optional<R::EditorProgressivePoissonResult> result;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.poisson_progress_observer", .MenuPath = {"View"}, .Title = "Poisson progress observer",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            result = context.PointSet.Results.LastProgressivePoissonResult;
        }});
    int frame = 0, step = 0, finishedAt = 0;
    std::string text;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frame > 400) { ADD_FAILURE() << "Poisson progress test did not finish"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("PointCloud / Processing / Progressive Poisson");
        if (!window) return;
        ImGui::SetWindowSize(window, {850, 1500});
        ImGui::SetWindowPos(window, {0, 0});
        if (++step == 3) ImGui::ActivateItemByID(window->GetID("Run Progressive Poisson##ProgressivePoisson"));
        if (!finishedAt && result && result->Status != R::EditorCommandStatus::Pending)
        {
            EXPECT_TRUE(result->Succeeded()) << result->Message;
            finishedAt = frame;
            ImGui::GetCurrentContext()->LogBuffer.clear();
            ImGui::LogToBuffer();
            ImGui::GetCurrentContext()->LogWindow = nullptr;
        }
        if (finishedAt && frame == finishedAt + 5)
        {
            text = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
            engine.RequestExit();
        }
    };
    h.Engine->Run();
    if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
    EXPECT_GT(finishedAt, 0);
    EXPECT_NE(text.find("done"), std::string::npos) << text;
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

TEST(SandboxProcessingPanels, ConsolidationNormalsRequireExplicitSelectionAndSurvivePositionChanges)
{
    for (const std::string normalName : {"v:normal", "directions"})
    {
        SCOPED_TRACE(normalName);
        PanelHarness h;
        auto& scene = h.Scene();
        const auto entity = scene.Create();
        auto& props = scene.Raw().emplace<GS::Vertices>(entity).Properties;
        props.Resize(3);
        props.GetOrAdd<glm::vec3>("v:position", {}).Vector() = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        props.GetOrAdd<glm::vec3>("alternate_samples", {}).Vector() = props.Get<glm::vec3>("v:position").Vector();
        (void)props.GetOrAdd<glm::vec3>(normalName, {0, 0, 1});
        ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
        ASSERT_TRUE(h.Shell.SetEditorWindowOpen("pointcloud.processing.consolidation", true));
        const auto label = [](const std::string& name) {
            return std::string{R::DebugNameForEditorPropertyCatalogDomain(R::EditorPropertyCatalogDomain::PointCloudPoints)} +
                " / " + name + " (3)";
        };
        unsigned step = 0, frames = 0;
        bool completed = false;
        h.Driver->OnFrame = [&](R::Engine& engine) {
            if (++frames > 80) { ADD_FAILURE() << "Consolidation binding test timed out"; engine.RequestExit(); return; }
            auto* window = ImGui::FindWindowByName("PointCloud / Processing / Consolidate (LOP/WLOP/CLOP/EAR)");
            if (!window) return;
            ImGui::SetWindowSize(window, {850, 2000});
            ImGui::SetWindowPos(window, {0, 0});
            if (step == 1) { ImGui::FocusWindow(window); ImGui::SetScrollY(window, 0); }
            if (step == 3 || step == 23)
            {
                ImGui::GetCurrentContext()->LogBuffer.clear();
                ImGui::LogToBuffer();
                ImGui::GetCurrentContext()->LogWindow = nullptr;
            }
            if (step == 5)
            {
                const std::string log{ImGui::GetCurrentContext()->LogBuffer.c_str()};
                ImGui::LogFinish();
                EXPECT_NE(log.find("None (estimate when needed)"), std::string::npos);
                EXPECT_EQ(log.find("Output Normal"), std::string::npos);
            }
            if (step == 7)
                ImGui::ActivateItemByID(window->GetID("Normal (optional)##PointCloudConsolidation"));
            if (step == 9 || step == 19)
            {
                auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
                ASSERT_FALSE(popups.empty());
                ASSERT_NE(popups.back().Window, nullptr);
                const auto option = label(step == 9 ? normalName : "alternate_samples");
                ImGui::ActivateItemByID(popups.back().Window->GetID(option.c_str()));
            }
            if (step == 13)
                ImGui::ActivateItemByID(window->GetID("Publish normals##PointCloudConsolidation"));
            if (step == 17)
                ImGui::ActivateItemByID(window->GetID("Position##PointCloudConsolidation"));
            if (step == 25)
            {
                const std::string log{ImGui::GetCurrentContext()->LogBuffer.c_str()};
                ImGui::LogFinish();
                EXPECT_EQ(log.find("None (estimate when needed)"), std::string::npos);
                EXPECT_NE(log.find(label(normalName)), std::string::npos);
                EXPECT_NE(log.find(label("alternate_samples")), std::string::npos);
                EXPECT_NE(log.find("Output Normal"), std::string::npos);
                completed = true;
                engine.RequestExit();
            }
            ++step;
        };
        h.Engine->Run();
        EXPECT_TRUE(completed);
    }
}

// METHOD-047: a successful atlas for the still-selected mesh opens the mesh |
// UV workspace, which claims the scene rectangle beside the atlas pane; with
// auto-open disabled in the persisted config, a new atlas leaves it closed.
TEST(SandboxProcessingPanels, AtlasWorkspaceAutoOpensAndClaimsSceneRectangle)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    const std::uint32_t stableId = R::SelectionController::ToStableEntityId(entity);
    constexpr std::string_view kWorkspace = "mesh.uv_atlas_workspace";
    const auto workspaceOpen = [&h, kWorkspace] {
        for (const auto& entry : h.Shell.BuildEditorWindowMenuModel())
            if (entry.Id == kWorkspace)
                return entry.Open;
        ADD_FAILURE() << "workspace window is not registered";
        return false;
    };
    ASSERT_FALSE(workspaceOpen());

    std::optional<R::EditorUvRegenerationCommandResult> immediate{};
    std::optional<R::EditorUvRegenerationCommandResult> terminal{};
    bool submit = false;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.atlas_workspace", .MenuPath = {"View"}, .Title = "Atlas workspace observer",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            terminal = context.Parameterization.Results.LastUvRegenerationResult;
            if (!submit)
                return;
            submit = false;
            const auto active = R::GetEditorParameterizationConfig(context.Parameterization.Commands);
            ASSERT_TRUE(active.has_value());
            if (context.Parameterization.ResultSinks.DismissUvRegenerationResult)
                context.Parameterization.ResultSinks.DismissUvRegenerationResult();
            terminal.reset();
            immediate = R::ApplyEditorUvRegenerationCommand(
                context.Parameterization.Commands,
                R::EditorUvRegenerationCommand{.StableEntityId = stableId, .Atlas = active->Atlas},
                context.Parameterization.ResultSinks.UvRegeneration);
        }});

    const auto* host = h.Engine->Services().Find<R::EditorUiHost>();
    ASSERT_NE(host, nullptr);
    enum class Step { FirstAtlas, Claim, LeftClaim, SecondAtlas, Done };
    Step step = Step::FirstAtlas;
    bool claimChecked = false;
    int frames = 0, waited = 0;
    submit = true;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frames > 800) { ADD_FAILURE() << "atlas workspace scenario stalled"; engine.RequestExit(); return; }
        const bool finished = !submit && terminal.has_value() &&
                              terminal->Status != R::EditorCommandStatus::Pending;
        if (!finished)
            waited = 0;
        switch (step)
        {
        case Step::FirstAtlas:
            // Let the observer and workspace see the terminal result first.
            if (!finished || ++waited < 3)
                return;
            EXPECT_TRUE(terminal->Succeeded()) << terminal->Diagnostic;
            EXPECT_EQ(terminal->StableEntityId, stableId);
            EXPECT_TRUE(workspaceOpen());
            step = Step::Claim;
            return;
        case Step::Claim:
        {
            const auto claim = host->SceneViewport().has_value() ? host->SceneViewport()
                                                                   : host->PresentedSceneViewport();
            ASSERT_TRUE(claim.has_value());
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            // Default layout: mesh on the left taking half of the work area.
            EXPECT_FLOAT_EQ(claim->X, viewport->WorkPos.x);
            EXPECT_FLOAT_EQ(claim->Y, viewport->WorkPos.y);
            EXPECT_NEAR(claim->Width, viewport->WorkSize.x * 0.5f, 1.0f);
            EXPECT_FLOAT_EQ(claim->Height, viewport->WorkSize.y);
            claimChecked = true;
            auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
            auto parameterization = *R::GetParameterizationConfig(config);
            parameterization.View.AtlasOnLeft = true;
            parameterization.View.SplitRatio = 0.6f;
            R::SetParameterizationConfig(config, parameterization);
            EXPECT_TRUE(h.Apply(config));
            waited = 0;
            step = Step::LeftClaim;
            return;
        }
        case Step::LeftClaim:
        {
            if (++waited < 3) return;
            const auto claim = host->SceneViewport().has_value() ? host->SceneViewport()
                                                                   : host->PresentedSceneViewport();
            ASSERT_TRUE(claim.has_value());
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            EXPECT_NEAR(claim->X, viewport->WorkPos.x + viewport->WorkSize.x * 0.4f, 1.0f);
            EXPECT_NEAR(claim->Width, viewport->WorkSize.x * 0.6f, 1.0f);
            // Close it and disable auto-open through the persisted config.
            EXPECT_TRUE(h.Shell.SetEditorWindowOpen(kWorkspace, false));
            auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
            auto parameterization = *R::GetParameterizationConfig(config);
            parameterization.View.SplitEnabled = false;
            // A different layout so the second run publishes new UVs.
            parameterization.Atlas.Resolution = 512u;
            parameterization.Atlas.Padding = 6u;
            R::SetParameterizationConfig(config, parameterization);
            EXPECT_TRUE(h.Apply(config));
            terminal.reset();
            immediate.reset();
            waited = 0;
            submit = true;
            step = Step::SecondAtlas;
            return;
        }
        case Step::SecondAtlas:
            if (!finished || ++waited < 3)
                return;
            EXPECT_TRUE(terminal->Succeeded()) << terminal->Diagnostic;
            EXPECT_FALSE(workspaceOpen());
            step = Step::Done;
            engine.RequestExit();
            return;
        case Step::Done:
            return;
        }
    };
    h.Engine->Run();
    EXPECT_EQ(step, Step::Done);
    EXPECT_TRUE(claimChecked);

    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

TEST(SandboxProcessingPanels, FaceScalarGradientComputesAndShowsVectorfield)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0,0,0}), b = mesh.AddVertex({1,0,0}), c = mesh.AddVertex({0,1,0});
    ASSERT_TRUE(mesh.AddTriangle(a,b,c));
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    auto& vertices = scene.Raw().get<GS::Vertices>(entity).Properties;
    vertices.GetOrAdd<double>("v:mean_curvature", 0.).Vector() = {0,2,3};
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("mesh.processing.faces.scalar_gradient", true));
    auto& initialRecipe = scene.Raw().get_or_emplace<R::GeometryPresentationRecipe>(entity);
    initialRecipe.VectorFields.push_back({
        .Vector = {R::GeometryElementDomain::MeshFace, "f:scalar_gradient", Geometry::PropertyValueKind::Vec3},
        .Length = 0.37f, .Enabled = false});
    int frames = 0;
    bool requested = false, shown = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        ++frames;
        auto* window = ImGui::FindWindowByName("Mesh / Processing / Faces / Scalar Field Gradient");
        if (window)
        {
            ImGui::SetWindowSize(window, {700, 900});
            ImGui::SetWindowPos(window, {0,0});
            ImGui::FocusWindow(window);
        }
        const auto& faces = std::as_const(scene.Raw().get<GS::Faces>(entity).Properties);
        if (window && frames == 10)
        {
            ImGui::ActivateItemByID(window->GetID("Compute Gradient"));
            requested = true;
        }
        if (window && requested && faces.Exists("f:scalar_gradient") && !shown)
        {
            EXPECT_EQ(faces.Get<glm::vec3>("f:scalar_gradient")[0], glm::vec3(2,3,0));
            ImGui::ActivateItemByID(window->GetID("Show Gradient Vectorfield"));
            shown = true;
        }
        const auto* recipe = scene.Raw().try_get<R::GeometryPresentationRecipe>(entity);
        if (recipe && !recipe->VectorFields.empty() && recipe->VectorFields[0].Enabled)
        {
            ASSERT_EQ(recipe->VectorFields.size(), 1u);
            EXPECT_EQ(recipe->VectorFields[0].Vector.Domain, R::GeometryElementDomain::MeshFace);
            EXPECT_EQ(recipe->VectorFields[0].Vector.Name, "f:scalar_gradient");
            EXPECT_TRUE(recipe->VectorFields[0].Enabled);
            EXPECT_FLOAT_EQ(recipe->VectorFields[0].Length, 0.37f);
            engine.RequestExit();
        }
        if (frames > 60)
        {
            ADD_FAILURE() << "Gradient compute/show did not complete: " << requested << "/" << shown;
            engine.RequestExit();
        }
    };
    h.Engine->Run();
}

TEST(SandboxProcessingPanels, LaplacianEigenbasisComputesAndPublishesEigenvectors)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    Geometry::HalfedgeMesh::Mesh mesh;
    std::vector<Geometry::VertexHandle> v;
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) v.push_back(mesh.AddVertex({float(x), float(y), 0.1f * float((x + y) % 2)}));
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            ASSERT_TRUE(mesh.AddTriangle(v[std::size_t(y * 5 + x)], v[std::size_t(y * 5 + x + 1)], v[std::size_t((y + 1) * 5 + x + 1)]));
            ASSERT_TRUE(mesh.AddTriangle(v[std::size_t(y * 5 + x)], v[std::size_t((y + 1) * 5 + x + 1)], v[std::size_t((y + 1) * 5 + x)]));
        }
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    auto& vertices = scene.Raw().get<GS::Vertices>(entity).Properties;
    R::LaplacianEigenbasisConfig eigenbasis;
    eigenbasis.Count = 4;
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto section = R::MakeLaplacianEigenbasisConfigSectionRegistration().DefaultSection;
    section.PayloadJson = R::SerializeLaplacianEigenbasisConfig(eigenbasis);
    Config::UpsertEngineConfigSection(config.AppSections, section);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.laplacian_eigenbasis", true));
    int frames = 0;
    bool requested = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        ++frames;
        auto* window = ImGui::FindWindowByName("Spectral Modes");
        if (window) { ImGui::SetWindowSize(window, {750, 1200}); ImGui::SetWindowPos(window, {0, 0}); ImGui::FocusWindow(window); }
        if (window && frames == 10) { ImGui::ActivateItemByID(window->GetID("Compute eigenbasis")); requested = true; }
        if (std::as_const(vertices).Get<float>("eigen_3"))
        {
            EXPECT_TRUE(requested);
            const auto first = std::as_const(vertices).Get<float>("eigen_0").Vector();
            for (const float value : first) EXPECT_NEAR(value, first.front(), 1e-4f) << "first eigenvector spans the constants";
            EXPECT_FALSE(std::as_const(vertices).Get<float>("eigen_4")) << "exactly the requested count";
            engine.RequestExit();
        }
        if (frames > 60) { ADD_FAILURE() << "Eigenbasis panel did not publish"; engine.RequestExit(); }
    };
    h.Engine->Run();
}

TEST(SandboxProcessingPanels, SpectralModesPublishesThinShellVibrationModesAndDistance)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    Geometry::HalfedgeMesh::Mesh mesh;
    std::vector<Geometry::VertexHandle> v;
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) v.push_back(mesh.AddVertex({float(x), float(y), 0.3f * float((x * y) % 3)}));
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            ASSERT_TRUE(mesh.AddTriangle(v[std::size_t(y * 5 + x)], v[std::size_t(y * 5 + x + 1)], v[std::size_t((y + 1) * 5 + x + 1)]));
            ASSERT_TRUE(mesh.AddTriangle(v[std::size_t(y * 5 + x)], v[std::size_t((y + 1) * 5 + x + 1)], v[std::size_t((y + 1) * 5 + x)]));
        }
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    auto& vertices = scene.Raw().get<GS::Vertices>(entity).Properties;
    R::LaplacianEigenbasisConfig modes;
    modes.Operator = R::ModalOperator::ThinShell;
    modes.Count = 9;
    modes.SkipModes = 6;
    modes.OutputPrefix = "vibration_";
    modes.DistanceSource = 12;
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto section = R::MakeLaplacianEigenbasisConfigSectionRegistration().DefaultSection;
    section.PayloadJson = R::SerializeLaplacianEigenbasisConfig(modes);
    Config::UpsertEngineConfigSection(config.AppSections, section);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.laplacian_eigenbasis", true));
    int frames = 0;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        ++frames;
        auto* window = ImGui::FindWindowByName("Spectral Modes");
        if (window) { ImGui::SetWindowSize(window, {750, 1200}); ImGui::SetWindowPos(window, {0, 0}); ImGui::FocusWindow(window); }
        if (window && frames == 10) ImGui::ActivateItemByID(window->GetID("Compute eigenbasis"));
        if (std::as_const(vertices).Get<float>("modal_distance"))
        {
            EXPECT_TRUE(std::as_const(vertices).Get<glm::vec3>("vibration_8")) << "vibration modes are vec3 displacements";
            const auto distance = std::as_const(vertices).Get<float>("modal_distance").Vector();
            EXPECT_EQ(distance[12], 0.0f);
            EXPECT_GT(*std::ranges::max_element(distance), 0.0f);
            engine.RequestExit();
        }
        if (frames > 60) { ADD_FAILURE() << "Spectral modes panel did not publish"; engine.RequestExit(); }
    };
    h.Engine->Run();
}

TEST(SandboxProcessingPanels, PropertySmoothingVariationalFitHonorsPerRowBounds)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a=mesh.AddVertex({0,0,0}), b=mesh.AddVertex({1,0,0}), c=mesh.AddVertex({0,1,0});
    ASSERT_TRUE(mesh.AddTriangle(a,b,c));
    GS::PopulateFromMesh(scene.Raw(),entity,mesh);
    auto& vertices=scene.Raw().get<GS::Vertices>(entity).Properties;
    vertices.GetOrAdd<double>("curvature",0.).Vector()={0,3,6};
    vertices.GetOrAdd<float>("tolerance",0.f).Vector()={0.f,0.5f,0.5f};
    R::PropertySmoothingConfig smoothing;
    smoothing.Input={R::GeometryElementDomain::MeshVertex,"curvature",Geometry::PropertyValueKind::Double};
    smoothing.Output={R::GeometryElementDomain::MeshVertex,"fitted",Geometry::PropertyValueKind::Double};
    smoothing.Filter.Method=Geometry::Smoothing::PropertyFilter::VariationalFit;
    smoothing.Filter.Bound=Geometry::Smoothing::FitBound::PerRow;
    smoothing.BoundRadii={R::GeometryElementDomain::MeshVertex,"tolerance",Geometry::PropertyValueKind::Float};
    auto config=h.Control().GetEngineConfigControlState().ActiveConfig;
    auto section=R::MakePropertySmoothingConfigSectionRegistration().DefaultSection;
    section.PayloadJson=R::SerializePropertySmoothingConfig(smoothing);
    Config::UpsertEngineConfigSection(config.AppSections,section);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene,entity));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.property_smoothing",true));
    int frames=0;
    bool requested=false;
    h.Driver->OnFrame=[&](R::Engine& engine) {
        ++frames;
        auto* window=ImGui::FindWindowByName("Smooth Property");
        if(window) { ImGui::SetWindowSize(window,{750,1200}); ImGui::SetWindowPos(window,{0,0}); ImGui::FocusWindow(window); }
        if(window && frames==10) { ImGui::ActivateItemByID(window->GetID("Smooth property")); requested=true; }
        const auto output=std::as_const(vertices).Get<double>("fitted");
        if(output)
        {
            EXPECT_TRUE(requested);
            EXPECT_EQ(output[0],0.0);
            EXPECT_NEAR(output[1],3.0,0.5+1e-12);
            EXPECT_NE(output[1],3.0);
            EXPECT_NEAR(output[2],5.5,1e-12) << "the upper row is held at its bound";
            engine.RequestExit();
        }
        if(frames>60) { ADD_FAILURE()<<"Variational fit did not publish"; engine.RequestExit(); }
    };
    h.Engine->Run();
}

// Drives the Smooth Property panel's own combos and button through one session on a real
// mean-curvature field: method, penalty, solver and order switches, then vector and face inputs.
TEST(SandboxProcessingPanels, PropertySmoothingSessionSwitchesMethodsSolversAndInputs)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    Geometry::HalfedgeMesh::Mesh mesh;
    std::vector<Geometry::VertexHandle> v;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            v.push_back(mesh.AddVertex({0.3f * float(x), 0.3f * float(y), 0.3f * std::sin(float(x)) * std::cos(0.7f * float(y)) + 0.02f * float((x * 7 + y * 3) % 5)}));
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 7; ++x)
        {
            ASSERT_TRUE(mesh.AddTriangle(v[std::size_t(y * 8 + x)], v[std::size_t(y * 8 + x + 1)], v[std::size_t((y + 1) * 8 + x + 1)]));
            ASSERT_TRUE(mesh.AddTriangle(v[std::size_t(y * 8 + x)], v[std::size_t((y + 1) * 8 + x + 1)], v[std::size_t((y + 1) * 8 + x)]));
        }
    ASSERT_TRUE(Geometry::Curvature::ComputeMeanCurvature(mesh).has_value());
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    auto& vertices = scene.Raw().get<GS::Vertices>(entity).Properties;
    auto& faces = scene.Raw().get<GS::Faces>(entity).Properties;
    ASSERT_TRUE(vertices.Exists("v:mean_curvature"));
    {
        auto directions = vertices.GetOrAdd<glm::vec3>("v:direction", glm::vec3(0));
        for (std::size_t i = 0; i < vertices.Size(); ++i) directions[i] = {float(i % 3), float(i % 5), 1.0f};
        auto roughness = faces.GetOrAdd<double>("f:roughness", 0.0);
        for (std::size_t f = 0; f < faces.Size(); ++f) roughness[f] = f % 2 ? 1.0 : -1.0;
    }
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto section = R::MakePropertySmoothingConfigSectionRegistration().DefaultSection;
    section.PayloadJson = R::SerializePropertySmoothingConfig({});
    Config::UpsertEngineConfigSection(config.AppSections, section);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.property_smoothing", true));

    using Action = std::function<void(ImGuiWindow*)>;
    const auto combo = [](const char* label, int index, const char* item) {
        return std::vector<Action>{
            [=](ImGuiWindow* w) { ImGui::ActivateItemByID(w->GetID(label)); },
            [=](ImGuiWindow*) {
                const auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
                ASSERT_FALSE(popups.empty()) << label;
                auto* popup = popups.back().Window;
                ASSERT_NE(popup, nullptr);
                // ImGui::Combo and the property chooser both push an integer scope per item.
                const auto scope = ImHashData(&index, sizeof(index), popup->IDStack.back());
                ImGui::ActivateItemByID(ImHashStr(item, 0, scope));
            }};
    };
    std::vector<double> lastScalar;
    std::string failure;
    const auto snapshotChanged = [&](const char* step) {
        return Action{[&, step](ImGuiWindow*) {
            const auto out = std::as_const(vertices).Get<double>("smoothed");
            ASSERT_TRUE(out) << step;
            const auto values = out.Vector();
            EXPECT_NE(values, lastScalar) << step << ": the button did not publish a new result";
            lastScalar = values;
        }};
    };
    const auto click = Action{[](ImGuiWindow* w) { ImGui::ActivateItemByID(w->GetID("Smooth property")); }};
    const auto idle = Action{[](ImGuiWindow*) {}};
    std::vector<Action> script;
    const auto add = [&](std::vector<Action> actions) { for (auto& a : actions) { script.push_back(std::move(a)); script.push_back(idle); script.push_back(idle); } };
    const auto run = [&](const char* step) { add({click, idle, snapshotChanged(step)}); };
    run("averaging");
    add(combo("Method", 5, "Variational fit (robust / TV / bounded)"));
    add(combo("Smoothness penalty", 2, "L1 (total variation)"));
    add(combo("Fit solver", 1, "ADMM (one factorization, delta 0 allowed)"));
    run("total variation ADMM");
    add(combo("Smoothness order", 1, "Second (non-local TGV, ADMM)"));
    run("TGV");
    add(combo("Fit solver", 0, "Reweighted least squares (reference)"));
    run("reweighted after TGV");
    add(combo("Method", 4, "Implicit (backward Euler)"));
    add(combo("Laplacian", 2, "Lumped mesh area (implicit, fit)"));
    run("implicit lumped");
    add(combo("Method", 0, "Averaging"));
    run("averaging after lumped implicit");
    add(combo("Input property##Smoothing", int(R::GeometryElementDomain::MeshVertex), "MeshVertex: v:direction"));
    add({click, idle, Action{[&](ImGuiWindow*) {
        EXPECT_TRUE(std::as_const(vertices).Get<glm::vec3>("v:direction_smoothed")) << "vector input publishes a vec3 output";
    }}});
    add(combo("Weights", 3, "Nonnegative mesh cotangent"));
    add(combo("Input property##Smoothing", int(R::GeometryElementDomain::MeshFace), "MeshFace: f:roughness"));
    add({click, idle, Action{[&](ImGuiWindow*) {
        EXPECT_TRUE(std::as_const(faces).Get<double>("f:roughness_smoothed")) << "face input with mesh weights left selected";
    }}});

    int frames = 0;
    std::size_t next = 0;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        ++frames;
        auto* window = ImGui::FindWindowByName("Smooth Property");
        // Focusing the panel closes open combo popups, so only focus it while none is open.
        if (window) { ImGui::SetWindowSize(window, {750, 1400}); ImGui::SetWindowPos(window, {0, 0}); }
        if (window && ImGui::GetCurrentContext()->OpenPopupStack.empty()) ImGui::FocusWindow(window);
        if (window && frames >= 10 && next < script.size()) script[next++](window);
        if (next == script.size() || frames > 600) engine.RequestExit();
    };
    h.Engine->Run();
    EXPECT_EQ(next, script.size()) << "the scripted session did not finish";
}

TEST(SandboxProcessingPanels, PropertySmoothingExecutesConfiguredPropertyAndPublishes)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a=mesh.AddVertex({0,0,0}), b=mesh.AddVertex({1,0,0}), c=mesh.AddVertex({0,1,0});
    ASSERT_TRUE(mesh.AddTriangle(a,b,c));
    GS::PopulateFromMesh(scene.Raw(),entity,mesh);
    auto& vertices=scene.Raw().get<GS::Vertices>(entity).Properties;
    vertices.GetOrAdd<glm::vec2>("custom_uv",{}).Vector()={{0,0},{2,4},{4,8}};
    R::PropertySmoothingConfig smoothing;
    smoothing.Input={R::GeometryElementDomain::MeshVertex,"custom_uv",Geometry::PropertyValueKind::Vec2};
    smoothing.Output={R::GeometryElementDomain::MeshVertex,"filtered_uv",Geometry::PropertyValueKind::Vec2};
    smoothing.Filter.Method=Geometry::Smoothing::PropertyFilter::Implicit;
    smoothing.Filter.TimeStep=2;
    auto config=h.Control().GetEngineConfigControlState().ActiveConfig;
    auto section=R::MakePropertySmoothingConfigSectionRegistration().DefaultSection;
    section.PayloadJson=R::SerializePropertySmoothingConfig(smoothing);
    Config::UpsertEngineConfigSection(config.AppSections,section);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene,entity));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("mesh.processing.property_smoothing",true));
    int frames=0;
    bool requested=false;
    h.Driver->OnFrame=[&](R::Engine& engine) {
        ++frames;
        auto* window=ImGui::FindWindowByName("Smooth Property");
        if(window) { ImGui::SetWindowSize(window,{750,1000}); ImGui::SetWindowPos(window,{0,0}); ImGui::FocusWindow(window); }
        if(window && frames==10) { ImGui::ActivateItemByID(window->GetID("Smooth property")); requested=true; }
        const auto output=std::as_const(vertices).Get<glm::vec2>("filtered_uv");
        if(output)
        {
            EXPECT_TRUE(requested);
            EXPECT_NEAR(output[0].x,1.5f,1e-6f);
            EXPECT_NEAR(output[0].y,3.0f,1e-6f);
            EXPECT_EQ(std::as_const(vertices).Get<glm::vec2>("custom_uv")[0],glm::vec2(0));
            engine.RequestExit();
        }
        if(frames>60) { ADD_FAILURE()<<"Property smoothing panel did not publish"; engine.RequestExit(); }
    };
    h.Engine->Run();
}

// UI-057: Smooth Property parameters come from the section's field table: typed values are
// clamped to the declared range, and the hover hint carries the table's description,
// accepted range and default.
TEST(SandboxProcessingPanels, PropertySmoothingControlsClampToTheFieldTableAndShowItsHint)
{
    const auto fields = R::PropertySmoothingConfigFieldSpecs();
    const auto* neighbors = R::FindConfigFieldSpec(fields, "neighbors");
    ASSERT_NE(neighbors, nullptr);
    const auto hint = Editor::FormatConfigFieldHint(*neighbors, "12");
    EXPECT_NE(hint.find(neighbors->Description), std::string::npos) << hint;
    EXPECT_NE(hint.find("Accepted: 1 to 1024"), std::string::npos) << hint;
    EXPECT_NE(hint.find("Default: 12"), std::string::npos) << hint;
    const auto* lambda = R::FindConfigFieldSpec(fields, "lambda");
    ASSERT_NE(lambda, nullptr);
    EXPECT_NE(Editor::FormatConfigFieldHint(*lambda, {}).find("greater than 0, at most 1"), std::string::npos);

    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0, 0, 0}), b = mesh.AddVertex({1, 0, 0}), c = mesh.AddVertex({0, 1, 0});
    ASSERT_TRUE(mesh.AddTriangle(a, b, c));
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    scene.Raw().get<GS::Vertices>(entity).Properties.GetOrAdd<double>("v:mean_curvature", 0.0);
    R::PropertySmoothingConfig smoothing;
    smoothing.Weight = Geometry::Smoothing::PropertyWeight::Uniform; // shows the Neighbors input
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto section = R::MakePropertySmoothingConfigSectionRegistration().DefaultSection;
    section.PayloadJson = R::SerializePropertySmoothingConfig(smoothing);
    Config::UpsertEngineConfigSection(config.AppSections, section);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.property_smoothing", true));
    const auto activeNeighbors = [&] {
        const auto& active = h.Control().GetEngineConfigControlState().ActiveConfig;
        const auto* stored = Config::FindEngineConfigSection(active.AppSections, R::kPropertySmoothingConfigSectionName);
        return stored ? stored->PayloadJson : std::string{};
    };
    int frames = 0;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        ++frames;
        auto* window = ImGui::FindWindowByName("Smooth Property");
        if (window) { ImGui::SetWindowSize(window, {750, 1200}); ImGui::SetWindowPos(window, {0, 0}); ImGui::FocusWindow(window); }
        if (window && frames >= 10 && frames < 16) EditScalarControl(window, "Neighbors", frames - 10, "5000");
        if (frames == 20) engine.RequestExit();
    };
    h.Engine->Run();
    EXPECT_NE(activeNeighbors().find("\"neighbors\":1024"), std::string::npos)
        << "5000 neighbors were clamped to the declared maximum and applied: " << activeNeighbors();
}

// UI-072 slice 2: the mesh-field panels read their bounds from the runtime tables. Each case types an
// out-of-range value into a control and expects the clamped value in the applied configuration; the old raw
// inputs stored the typed value, the validator rejected it and the active configuration never changed.
namespace
{
    struct SpecClampCase
    {
        const char* Window;
        const char* Title;
        const char* Control;
        const char* Typed;
        std::string_view Section;
        const char* Expected;
    };

    void ExpectSpecClamp(const SpecClampCase& test)
    {
        PanelHarness h;
        auto& scene = h.Scene();
        const auto entity = scene.Create();
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
        ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
        ASSERT_TRUE(h.Shell.SetEditorWindowOpen(test.Window, true));
        int frames = 0;
        h.Driver->OnFrame = [&](R::Engine& engine) {
            ++frames;
            auto* window = ImGui::FindWindowByName(test.Title);
            if (window) { ImGui::SetWindowSize(window, {750, 1600}); ImGui::SetWindowPos(window, {0, 0}); ImGui::FocusWindow(window); }
            if (window && frames >= 10 && frames < 16) EditScalarControl(window, test.Control, frames - 10, test.Typed);
            if (frames == 20) engine.RequestExit();
        };
        h.Engine->Run();
        const auto& active = h.Control().GetEngineConfigControlState().ActiveConfig;
        const auto* stored = Config::FindEngineConfigSection(active.AppSections, test.Section);
        ASSERT_NE(stored, nullptr);
        EXPECT_NE(stored->PayloadJson.find(test.Expected), std::string::npos)
            << test.Control << " typed " << test.Typed << ": " << stored->PayloadJson;
    }
}

TEST(SandboxProcessingPanels, EigenbasisClampsToTheSpecRange)
{
    ExpectSpecClamp({"view.laplacian_eigenbasis", "Spectral Modes", "Eigenpairs", "9999",
                     R::kLaplacianEigenbasisConfigSectionName, "\"count\":256"});
    ExpectSpecClamp({"view.laplacian_eigenbasis", "Spectral Modes", "Maximum iterations##Eigenbasis", "9999999",
                     R::kLaplacianEigenbasisConfigSectionName, "\"max_iterations\":100000"});
}

TEST(SandboxProcessingPanels, GeodesicsExpansionBudgetClampsToTheSpecRange)
{
    ExpectSpecClamp({"mesh.processing.geodesics", "Mesh / Geodesics / Virtual Source Propagation", "Expansion budget",
                     "0", R::kGeodesicsConfigSectionName, "\"max_halfedge_expansions\":1,"});
}

// UI-072 slice 3: the topology panels bound their drags by the command owners' tables. The ranges are the
// ones the panels carried as literals, so this pins that the panel and the table cannot drift apart: an
// out-of-range entry shows the table's bound.
TEST(SandboxProcessingPanels, TopologyPanelDragsShowTheTableBounds)
{
    struct Case { const char* Window; const char* Title; const char* Control; const char* Typed; const char* Shown; };
    const std::array cases{
        Case{"mesh.processing.denoise", "Mesh / Processing / Denoise", "Normal iterations##MeshDenoise", "9999999", "{ 4096 } Normal iterations"},
        Case{"mesh.processing.remesh", "Mesh / Processing / Remesh", "Iterations##MeshRemesh", "500", "{ 64 } Iterations"},
        Case{"mesh.processing.subdivide", "Mesh / Processing / Subdivide", "Iterations##MeshSubdivide", "500", "{ 10 } Iterations"},
        Case{"mesh.processing.simplify", "Mesh / Processing / Simplify", "Target faces##MeshSimplify", "2000000000", "{ 1000000000 } Target faces"},
    };
    for (const auto& test : cases)
    {
        SCOPED_TRACE(test.Control);
        PanelHarness h;
        auto& scene = h.Scene();
        const auto entity = scene.Create();
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
        ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
        ASSERT_TRUE(h.Shell.SetEditorWindowOpen(test.Window, true));
        int frames = 0;
        std::string drawn;
        h.Driver->OnFrame = [&](R::Engine& engine) {
            ++frames;
            auto* window = ImGui::FindWindowByName(test.Title);
            if (window) { ImGui::SetWindowSize(window, {750, 1600}); ImGui::SetWindowPos(window, {0, 0}); ImGui::FocusWindow(window); }
            if (window && frames >= 10 && frames < 16) EditScalarControl(window, test.Control, frames - 10, test.Typed);
            if (frames == 18) { ImGui::GetCurrentContext()->LogBuffer.clear(); ImGui::LogToBuffer(); ImGui::GetCurrentContext()->LogWindow = nullptr; }
            if (frames == 20)
            {
                drawn = ImGui::GetCurrentContext()->LogBuffer.c_str();
                ImGui::LogFinish();
                engine.RequestExit();
            }
        };
        h.Engine->Run();
        EXPECT_NE(drawn.find(test.Shown), std::string::npos) << drawn;
    }
}

// UI-055: the Coherent Point Drift panel drives a run through its own buttons: Start
// captures, Step and Run to end iterate on the job service while the preview overlay
// follows, and Apply publishes the transform. Apply and Discard drop the preview.
TEST(SandboxProcessingPanels, CoherentPointDriftPanelStepsRunsAndApplies)
{
    PanelHarness h;
    auto& scene = h.Scene();
    std::vector<glm::vec3> points;
    for (int i = 0; i < 64; ++i)
        points.push_back({std::sin(float(i) * 1.3f), std::cos(float(i) * 0.7f), 0.5f * std::sin(float(i) * 2.1f)});
    const auto make = [&](glm::vec3 offset) {
        const auto entity = scene.Create();
        scene.Raw().emplace_or_replace<Extrinsic::ECS::Components::Transform::Component>(entity);
        auto& vertices = scene.Raw().emplace<GS::Vertices>(entity).Properties;
        vertices.Resize(points.size());
        auto positions = vertices.GetOrAdd<glm::vec3>("v:position");
        for (std::size_t i = 0; i < points.size(); ++i) positions[i] = points[i] + offset;
        return entity;
    };
    const auto source = make({});
    const auto target = make({0.25f, -0.1f, 0.05f});
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    R::SetCoherentPointDriftConfig(config, {.SourceStableEntityId = R::SelectionController::ToStableEntityId(source),
                                            .TargetStableEntityId = R::SelectionController::ToStableEntityId(target),
                                            .OutlierWeight = 0.0});
    ASSERT_TRUE(h.Apply(config));
    // The panel follows the scene selection: the source is selected, the target is picked
    // from the panel's own combo.
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, source));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.coherent_point_drift", true));
    const auto targetTitle = "Entity " + std::to_string(static_cast<std::uint32_t>(target)) + " (" +
                             std::to_string(R::SelectionController::ToStableEntityId(target)) + ")";

    auto& transform = scene.Raw().get<Extrinsic::ECS::Components::Transform::Component>(source);
    auto* interaction = h.Engine->Services().Find<R::SceneInteractionModule>();
    ASSERT_NE(interaction, nullptr);
    const auto overlay = [&] { return interaction->PreviewOverlayPointCount("coherent_point_drift"); };
    int frames = 0;
    const auto click = [](ImGuiWindow* window, const char* label) { ImGui::ActivateItemByID(window->GetID(label)); };
    h.Driver->OnFrame = [&](R::Engine& engine) {
        ++frames;
        auto* window = ImGui::FindWindowByName("Coherent Point Drift");
        if (!window) return;
        ImGui::SetWindowSize(window, {520, 1400});
        ImGui::SetWindowPos(window, {0, 0});
        // Focusing the panel closes open combo popups, so only focus it while none is open.
        if (ImGui::GetCurrentContext()->OpenPopupStack.empty()) ImGui::FocusWindow(window);
        if (frames == 6) click(window, "Target (fixed)##CPD");
        if (frames == 8)
        {
            auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
            EXPECT_FALSE(popups.empty());
            if (!popups.empty() && popups.back().Window)
                ImGui::ActivateItemByID(popups.back().Window->GetID(targetTitle.c_str()));
        }
        if (frames == 14) click(window, "Start##CPD");
        if (frames == 18) click(window, "Step##CPD");
        if (frames == 28) click(window, "Run to end##CPD");
        if (frames == 70)
        {
            EXPECT_EQ(transform.Position, glm::vec3(0.0f)) << "nothing is published before Apply";
            EXPECT_GT(overlay(), 0u) << "the moving source is previewed";
            click(window, "Apply##CPD");
        }
        // Apply and Discard drop the preview; the frame that clicks them must not redraw it.
        if (frames == 74)
        {
            EXPECT_EQ(overlay(), 0u) << "the preview outlived Apply";
            click(window, "Restart##CPD");
        }
        if (frames == 78) click(window, "Step##CPD");
        if (frames == 90)
        {
            EXPECT_GT(overlay(), 0u);
            click(window, "Discard##CPD");
        }
        if (frames == 94)
        {
            EXPECT_EQ(overlay(), 0u) << "the preview outlived Discard";
            engine.RequestExit();
        }
        if (frames > 200) engine.RequestExit();
    };
    h.Engine->Run();
    EXPECT_GE(frames, 94);
    EXPECT_NEAR(transform.Position.x, 0.25f, 2e-3f);
    EXPECT_NEAR(transform.Position.y, -0.1f, 2e-3f);
    // This harness composes no document history; undo/redo of the publication is covered by
    // Test.CoherentPointDriftOperations.cpp.
}

// RUNTIME-292: the Smooth Property window drives a GPU property transaction (ADR 0030): once a
// Vulkan run waits for the user, Accept and Discard appear; Accept is disabled while the inputs
// changed under the result (stale), Discard always works, and Accept publishes the front through
// the undoable transaction. The transaction is injected through the panel's test seam because the
// null device cannot run the kernels; its state machine is the runtime's real one.
TEST(SandboxProcessingPanels, PropertySmoothingAcceptsOrDiscardsAPendingGpuResult)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    scene.Raw().emplace_or_replace<Extrinsic::ECS::Components::Transform::Component>(entity);
    auto& vertices = scene.Raw().emplace<GS::Vertices>(entity).Properties;
    vertices.Resize(16);
    auto positions = vertices.GetOrAdd<glm::vec3>("v:position");
    auto signal = vertices.GetOrAdd<float>("signal", 0.f);
    for (std::size_t i = 0; i < 16; ++i) { positions[i] = {float(i % 4), float(i / 4), 0.f}; signal[i] = float(i); }
    R::PropertySmoothingConfig smoothing;
    smoothing.Input = {R::GeometryElementDomain::PointCloudPoint, "signal", Geometry::PropertyValueKind::Float};
    smoothing.Output = {R::GeometryElementDomain::PointCloudPoint, "smooth", Geometry::PropertyValueKind::Float};
    smoothing.Positions = {R::GeometryElementDomain::PointCloudPoint, "v:position", Geometry::PropertyValueKind::Vec3};
    smoothing.Neighbors = 4;
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto section = R::MakePropertySmoothingConfigSectionRegistration().DefaultSection;
    section.PayloadJson = R::SerializePropertySmoothingConfig(smoothing);
    Config::UpsertEngineConfigSection(config.AppSections, section);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("mesh.processing.property_smoothing", true));
    // The transaction's own context: the scene, an undo history and the engine's job service.
    R::EditorCommandHistory history;
    R::EditorProcessingContext context;
    context.Scene = &scene;
    context.CommandHistory = &history;
    // The correlation stamp makes these jobs visible to the panel's session projection.
    std::uint64_t correlation = 0u;
    context.JobCommands.Submit = [&](R::JobDesc desc, const auto&) {
        desc.CorrelationId = ++correlation;
        return h.Engine->Jobs().Submit(std::move(desc));
    };
    const auto commands = R::BindEditorProcessingCommands(context);
    const auto id = R::SelectionController::ToStableEntityId(entity);
    const std::vector<double> front(16, 42.0);
    R::EditorPropertySmoothingTransactionHandle stale, fresh;
    int frames = 0, appliedAt = 0;
    const auto startLog = [] {
        ImGui::GetCurrentContext()->LogBuffer.clear();
        ImGui::LogToBuffer();
        ImGui::GetCurrentContext()->LogWindow = nullptr;
    };
    const auto finishLog = [] {
        std::string text{ImGui::GetCurrentContext()->LogBuffer.c_str()};
        ImGui::LogFinish();
        return text;
    };
    h.Driver->OnFrame = [&](R::Engine& engine) {
        ++frames;
        auto* window = ImGui::FindWindowByName("Smooth Property");
        if (window) { ImGui::SetWindowSize(window, {750, 1200}); ImGui::SetWindowPos(window, {0, 0}); ImGui::FocusWindow(window); }
        if (!window) { if (frames > 60) { ADD_FAILURE() << "no Smooth Property window"; engine.RequestExit(); } return; }
        switch (frames)
        {
        case 10:
            stale = R::MakeEditorPropertySmoothingTransactionForTest(commands, id, smoothing, front);
            ASSERT_TRUE(stale);
            h.Panels.InjectPropertySmoothingTransactionForTest(stale);
            vertices.Get<float>("signal")[0] += 1.f; // the input changes under the waiting result
            break;
        case 13:
            EXPECT_TRUE(R::SnapshotEditorPropertySmoothing(commands, stale).Stale);
            ImGui::ActivateItemByID(window->GetID("Accept##Smoothing"));
            break;
        case 16:
            EXPECT_EQ(R::SnapshotEditorPropertySmoothing(commands, stale).Phase, R::EditorGpuTransactionPhase::ReadyToAccept)
                << "a stale result cannot be accepted from the panel";
            EXPECT_FALSE(std::as_const(vertices).Exists("smooth"));
            ImGui::ActivateItemByID(window->GetID("Discard##Smoothing"));
            break;
        case 19:
            startLog();
            EXPECT_EQ(R::SnapshotEditorPropertySmoothing(commands, stale).Phase, R::EditorGpuTransactionPhase::Discarded);
            EXPECT_FALSE(std::as_const(vertices).Exists("smooth"));
            fresh = R::MakeEditorPropertySmoothingTransactionForTest(commands, id, smoothing, front);
            ASSERT_TRUE(fresh);
            EXPECT_TRUE(R::SnapshotEditorPropertySmoothing(commands, fresh).CanAccept);
            h.Panels.InjectPropertySmoothingTransactionForTest(fresh);
            break;
        case 22:
            {
                // The discarded result never reads as a finished run; the waiting fresh one reads
                // "awaiting accept", not done.
                const std::string text = finishLog();
                EXPECT_EQ(text.find("done"), std::string::npos);
                EXPECT_NE(text.find("awaiting accept"), std::string::npos);
            }
            ImGui::ActivateItemByID(window->GetID("Accept##Smoothing"));
            break;
        default:
            if (appliedAt == 0 && frames > 22 &&
                R::SnapshotEditorPropertySmoothing(commands, fresh).Phase == R::EditorGpuTransactionPhase::Applied)
            {
                appliedAt = frames;
                startLog();
            }
            if (appliedAt != 0 && frames == appliedAt + 5)
            {
                // The accepted run's outcome stays on screen after the transaction ends.
                EXPECT_NE(finishLog().find("done"), std::string::npos);
                const auto smooth = std::as_const(vertices).Get<float>("smooth");
                ASSERT_TRUE(smooth);
                EXPECT_FLOAT_EQ(smooth[3], 42.f);
                EXPECT_TRUE(history.Undo().Succeeded()) << "Accept published through the undoable transaction";
                EXPECT_FALSE(std::as_const(vertices).Exists("smooth"));
                engine.RequestExit();
            }
            if (frames > 300) { ADD_FAILURE() << "the panel never accepted the result"; engine.RequestExit(); }
            break;
        }
    };
    h.Engine->Run();
}

// RUNTIME-296: the Normal Estimation window drives the Vulkan vertex-normals transaction like
// the smoothing window drives its scalar one: a stale result cannot be accepted, only
// discarded; a current one is accepted through the undoable publication.
TEST(SandboxProcessingPanels, NormalEstimationAcceptsOrDiscardsAPendingGpuResult)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    scene.Raw().emplace_or_replace<Extrinsic::ECS::Components::Transform::Component>(entity);
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0.f, 0.f, 0.f}), b = mesh.AddVertex({1.f, 0.f, 0.f}),
               c = mesh.AddVertex({0.f, 1.f, 0.f}), d = mesh.AddVertex({1.f, 1.f, 0.2f});
    (void)mesh.AddTriangle(a, b, c);
    (void)mesh.AddTriangle(c, b, d);
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    auto& vertices = scene.Raw().get<GS::Vertices>(entity).Properties;
    R::NormalEstimationConfig normals;
    normals.StableEntityId = R::SelectionController::ToStableEntityId(entity);
    normals.Method = R::NormalEstimationMethod::MeshFaceWeighted;
    normals.Backend = R::NormalEstimationBackend::Vulkan;
    normals.Positions = {R::GeometryElementDomain::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3};
    normals.Output = {R::GeometryElementDomain::MeshVertex, "v:normal", Geometry::PropertyValueKind::Vec3};
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto section = R::MakeNormalEstimationConfigSectionRegistration().DefaultSection;
    section.PayloadJson = R::SerializeNormalEstimationConfig(normals);
    Config::UpsertEngineConfigSection(config.AppSections, section);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.normal_estimation", true));
    R::EditorCommandHistory history;
    R::EditorProcessingContext context;
    context.Scene = &scene;
    context.CommandHistory = &history;
    context.JobCommands.Submit = [&](R::JobDesc desc, const auto&) { return h.Engine->Jobs().Submit(std::move(desc)); };
    const auto commands = R::BindEditorProcessingCommands(context);
    const std::vector<glm::vec3> front(vertices.Size(), glm::vec3{0.f, 0.f, 1.f});
    R::EditorNormalTransactionHandle stale, fresh;
    int frames = 0;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        ++frames;
        auto* window = ImGui::FindWindowByName("Normal Estimation");
        if (window) { ImGui::SetWindowSize(window, {750, 1200}); ImGui::SetWindowPos(window, {0, 0}); ImGui::FocusWindow(window); }
        if (!window) { if (frames > 60) { ADD_FAILURE() << "no Normal Estimation window"; engine.RequestExit(); } return; }
        switch (frames)
        {
        case 10:
            stale = R::MakeEditorNormalTransactionForTest(commands, normals, front);
            ASSERT_TRUE(stale);
            h.Panels.InjectNormalTransactionForTest(stale);
            vertices.Get<glm::vec3>("v:position")[0].z += 1.f; // the positions change under the waiting result
            break;
        case 13:
            EXPECT_TRUE(R::SnapshotEditorNormalEstimation(commands, stale).Stale);
            ImGui::ActivateItemByID(window->GetID("Accept##Normals"));
            break;
        case 16:
            EXPECT_EQ(R::SnapshotEditorNormalEstimation(commands, stale).Phase, R::EditorGpuTransactionPhase::ReadyToAccept)
                << "a stale result cannot be accepted from the panel";
            EXPECT_FALSE(std::as_const(vertices).Exists("v:normal"));
            ImGui::ActivateItemByID(window->GetID("Discard##Normals"));
            break;
        case 19:
            EXPECT_EQ(R::SnapshotEditorNormalEstimation(commands, stale).Phase, R::EditorGpuTransactionPhase::Discarded);
            EXPECT_FALSE(std::as_const(vertices).Exists("v:normal"));
            fresh = R::MakeEditorNormalTransactionForTest(commands, normals, front);
            ASSERT_TRUE(fresh);
            EXPECT_TRUE(R::SnapshotEditorNormalEstimation(commands, fresh).CanAccept);
            h.Panels.InjectNormalTransactionForTest(fresh);
            break;
        case 22:
            ImGui::ActivateItemByID(window->GetID("Accept##Normals"));
            break;
        default:
            if (frames > 22 && R::SnapshotEditorNormalEstimation(commands, fresh).Phase == R::EditorGpuTransactionPhase::Applied)
            {
                const auto published = std::as_const(vertices).Get<glm::vec3>("v:normal");
                ASSERT_TRUE(published);
                EXPECT_EQ(published[3], glm::vec3(0.f, 0.f, 1.f));
                EXPECT_TRUE(history.Undo().Succeeded()) << "Accept published through the undoable transaction";
                EXPECT_FALSE(std::as_const(vertices).Exists("v:normal"));
                engine.RequestExit();
            }
            if (frames > 300) { ADD_FAILURE() << "the panel never accepted the result"; engine.RequestExit(); }
            break;
        }
    };
    h.Engine->Run();
}

// Detaching the panels discards a GPU result that still waits for Accept or Discard (its ring
// would otherwise block every later run on that output); terminal transactions are untouched.
TEST(SandboxProcessingPanels, DetachDiscardsPendingGpuResults)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    scene.Raw().emplace_or_replace<Extrinsic::ECS::Components::Transform::Component>(entity);
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0.f, 0.f, 0.f}), b = mesh.AddVertex({1.f, 0.f, 0.f}), c = mesh.AddVertex({0.f, 1.f, 0.f});
    (void)mesh.AddTriangle(a, b, c);
    GS::PopulateFromMesh(scene.Raw(), entity, mesh);
    auto& vertices = scene.Raw().get<GS::Vertices>(entity).Properties;
    auto signal = vertices.GetOrAdd<float>("signal", 0.f);
    for (std::size_t i = 0; i < vertices.Size(); ++i) signal[i] = float(i);
    R::EditorCommandHistory history;
    R::EditorProcessingContext context;
    context.Scene = &scene;
    context.CommandHistory = &history;
    const auto commands = R::BindEditorProcessingCommands(context);
    const auto id = R::SelectionController::ToStableEntityId(entity);
    R::NormalEstimationConfig normals;
    normals.StableEntityId = id;
    normals.Method = R::NormalEstimationMethod::MeshFaceWeighted;
    normals.Backend = R::NormalEstimationBackend::Vulkan;
    normals.Positions = {R::GeometryElementDomain::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3};
    normals.Output = {R::GeometryElementDomain::MeshVertex, "v:normal", Geometry::PropertyValueKind::Vec3};
    R::PropertySmoothingConfig smoothing;
    smoothing.Input = {R::GeometryElementDomain::MeshVertex, "signal", Geometry::PropertyValueKind::Float};
    smoothing.Output = {R::GeometryElementDomain::MeshVertex, "smooth", Geometry::PropertyValueKind::Float};
    smoothing.Positions = {R::GeometryElementDomain::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3};
    smoothing.Neighbors = 2;
    const auto normalRun = R::MakeEditorNormalTransactionForTest(commands, normals, std::vector<glm::vec3>(vertices.Size(), glm::vec3{0.f, 0.f, 1.f}));
    const auto smoothingRun = R::MakeEditorPropertySmoothingTransactionForTest(commands, id, smoothing, std::vector<double>(vertices.Size(), 1.0));
    ASSERT_TRUE(normalRun);
    ASSERT_TRUE(smoothingRun);
    h.Panels.InjectNormalTransactionForTest(normalRun);
    h.Panels.InjectPropertySmoothingTransactionForTest(smoothingRun);
    EXPECT_EQ(R::SnapshotEditorNormalEstimation(commands, normalRun).Phase, R::EditorGpuTransactionPhase::ReadyToAccept);
    EXPECT_EQ(R::SnapshotEditorPropertySmoothing(commands, smoothingRun).Phase, R::EditorGpuTransactionPhase::ReadyToAccept);
    h.Panels.Unregister();
    EXPECT_EQ(R::SnapshotEditorNormalEstimation(commands, normalRun).Phase, R::EditorGpuTransactionPhase::Discarded);
    EXPECT_EQ(R::SnapshotEditorPropertySmoothing(commands, smoothingRun).Phase, R::EditorGpuTransactionPhase::Discarded);
    EXPECT_FALSE(std::as_const(vertices).Exists("v:normal"));
    EXPECT_FALSE(std::as_const(vertices).Exists("smooth"));
}

TEST(SandboxProcessingPanels, GpuTransactionsDisplayTerminalFailuresAndStaleResults)
{
    using Phase=R::EditorGpuTransactionPhase;
    for (int method=0; method<3; ++method)
    for (bool stale : {false,true})
    {
        SCOPED_TRACE(method);
        SCOPED_TRACE(stale);
        PanelHarness h;
        Extrinsic::Tests::MockDevice device;
        Extrinsic::Graphics::GpuPropertyResidency residency{device};
        device.TransferQueue.AcceptBufferUploads=true;
        auto& scene=h.Scene(); const auto entity=scene.Create();
        scene.Raw().emplace<Extrinsic::ECS::Components::Transform::Component>(entity);
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto a=mesh.AddVertex({0,0,0}),b=mesh.AddVertex({1,0,0}),c=mesh.AddVertex({0,1,0});
        (void)mesh.AddTriangle(a,b,c); GS::PopulateFromMesh(scene.Raw(),entity,mesh);
        auto& rows=scene.Raw().get<GS::Vertices>(entity).Properties;
        (void)rows.GetOrAdd<float>("signal",1.f);
        const auto id=R::SelectionController::ToStableEntityId(entity);
        R::NormalEstimationConfig normals;
        normals.StableEntityId=id; normals.Method=R::NormalEstimationMethod::MeshFaceWeighted;
        normals.Backend=R::NormalEstimationBackend::Vulkan;
        normals.Positions={R::GeometryElementDomain::MeshVertex,"v:position",Geometry::PropertyValueKind::Vec3};
        normals.Output={R::GeometryElementDomain::MeshVertex,"v:normal",Geometry::PropertyValueKind::Vec3};
        R::PropertySmoothingConfig smoothing;
        smoothing.Positions=normals.Positions; smoothing.Neighbors=2;
        smoothing.Input={R::GeometryElementDomain::MeshVertex,"signal",Geometry::PropertyValueKind::Float};
        smoothing.Output={R::GeometryElementDomain::MeshVertex,"smooth",Geometry::PropertyValueKind::Float};
        R::OutlierAnalysisConfig outliers;
        outliers.StableEntityId=id; outliers.Method=R::OutlierAnalysisMethod::Radius;
        outliers.Backend=R::OutlierAnalysisBackend::VulkanLBVH; outliers.Positions=normals.Positions;
        outliers.Score.Domain=outliers.Mask.Domain=R::GeometryElementDomain::MeshVertex;
        auto config=h.Control().GetEngineConfigControlState().ActiveConfig;
        R::SetNormalEstimationConfig(config,normals); R::SetOutlierAnalysisConfig(config,outliers);
        auto section=R::MakePropertySmoothingConfigSectionRegistration().DefaultSection;
        section.PayloadJson=R::SerializePropertySmoothingConfig(smoothing);
        Config::UpsertEngineConfigSection(config.AppSections, section);
        ASSERT_TRUE(h.Apply(config)); ASSERT_TRUE(h.Selection().SetSelectedEntity(scene,entity));
        const char* ids[]={"view.outlier_analysis","view.normal_estimation","mesh.processing.property_smoothing"};
        const char* titles[]={"Outlier Analysis","Normal Estimation","Smooth Property"};
        ASSERT_TRUE(h.Shell.SetEditorWindowOpen(ids[method],true));
        R::EditorProcessingContext context; context.Scene=&scene;
        context.JobCommands.Submit=[&](R::JobDesc job,const auto&)->R::JobToken {
            return stale ? h.Engine->Jobs().Submit(std::move(job)) : R::JobToken{};
        };
        const auto commands=R::BindEditorProcessingCommands(context);
        auto outlierRun=R::MakeEditorOutlierTransactionForTest(commands,outliers,
            std::vector<float>(rows.Size(),1),std::vector<std::uint32_t>(rows.Size(),0),residency);
        auto normalRun=R::MakeEditorNormalTransactionForTest(commands,normals,std::vector<glm::vec3>(rows.Size(),{0,0,1}));
        auto smoothingRun=R::MakeEditorPropertySmoothingTransactionForTest(commands,id,smoothing,std::vector<double>(rows.Size(),1));
        ASSERT_TRUE(outlierRun); ASSERT_TRUE(normalRun); ASSERT_TRUE(smoothingRun);
        if(method==0)h.Panels.InjectOutlierTransactionForTest(outlierRun);
        else if(method==1)h.Panels.InjectNormalTransactionForTest(normalRun);
        else h.Panels.InjectPropertySmoothingTransactionForTest(smoothingRun);
        const auto terminal=[&]()->std::pair<Phase,std::string> {
            if(method==0){const auto s=R::SnapshotEditorOutlierAnalysis(commands,outlierRun);return {s.Phase,s.Result.Message};}
            if(method==1){const auto s=R::SnapshotEditorNormalEstimation(commands,normalRun);return {s.Phase,s.Result.Message};}
            const auto s=R::SnapshotEditorPropertySmoothing(commands,smoothingRun);return {s.Phase,s.Result.Message};
        };
        int frames=0,terminalFrame=0; bool checked=false;
        h.Driver->OnFrame=[&](R::Engine& engine) {
            if(++frames>100){ADD_FAILURE()<<"terminal result was not displayed";engine.RequestExit();return;}
            auto* window=ImGui::FindWindowByName(titles[method]); if(!window)return;
            ImGui::SetWindowSize(window,{800,1800}); ImGui::SetWindowPos(window,{0,0});
            if(frames==5){
                if(method==0)(void)R::AcceptEditorOutlierAnalysis(commands,outlierRun);
                else if(method==1)(void)R::AcceptEditorNormalEstimation(commands,normalRun);
                else (void)R::AcceptEditorPropertySmoothing(commands,smoothingRun);
                if(stale)rows.Get<glm::vec3>("v:position")[0]={2,3,4};
            }
            const auto [phase,message]=terminal();
            if(phase!=Phase::Failed && phase!=Phase::Discarded)return;
            EXPECT_EQ(phase,stale?Phase::Discarded:Phase::Failed);
            if(!terminalFrame){terminalFrame=frames;return;}
            // Wait beyond the frame which consumes/resets the transaction; the result must persist.
            if(frames==terminalFrame+3){
                ImGui::GetCurrentContext()->LogBuffer.clear(); ImGui::LogToBuffer();
                ImGui::GetCurrentContext()->LogWindow=nullptr;
            }
            if(frames==terminalFrame+5){
                const std::string text{ImGui::GetCurrentContext()->LogBuffer.c_str()}; ImGui::LogFinish();
                EXPECT_FALSE(message.empty()); EXPECT_NE(text.find(message),std::string::npos)<<text;
                checked=true;engine.RequestExit();
            }
        };
        h.Engine->Run(); EXPECT_TRUE(checked);
        R::DiscardEditorOutlierAnalysis({},outlierRun);
        R::DiscardEditorNormalEstimation({},normalRun);
        R::DiscardEditorPropertySmoothing({},smoothingRun);
    }
}

TEST(SandboxProcessingPanels, ScalarTransactionTerminalResultsPersistAndDetachDiscards)
{
    using Phase=R::EditorGpuTransactionPhase;
    for(unsigned method=0;method<3;++method) for(bool applied:{false,true})
    {
        SCOPED_TRACE(method);
        SCOPED_TRACE(applied);PanelHarness h;
        Extrinsic::Tests::MockDevice device;Extrinsic::Graphics::GpuPropertyResidency residency{device};
        device.TransferQueue.AcceptBufferUploads=true;
        auto& scene=h.Scene();const auto entity=scene.Create();
        scene.Raw().emplace<Extrinsic::ECS::Components::Transform::Component>(entity);
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto a=mesh.AddVertex({0,0,0}),b=mesh.AddVertex({1,0,0}),c=mesh.AddVertex({0,1,0});
        (void)mesh.AddTriangle(a,b,c);GS::PopulateFromMesh(scene.Raw(),entity,mesh);
        auto& rows=scene.Raw().get<GS::Vertices>(entity).Properties;
        R::KernelDensityConfig density;R::PointSpacingConfig spacing;R::DensityWeightConfig weights;
        density.StableEntityId=spacing.StableEntityId=weights.StableEntityId=R::SelectionController::ToStableEntityId(entity);
        density.Positions.Domain=spacing.Positions.Domain=weights.Positions.Domain=R::GeometryElementDomain::MeshVertex;
        density.Density.Domain=spacing.Radii.Domain=weights.Weights.Domain=R::GeometryElementDomain::MeshVertex;
        density.Backend=R::KernelDensityBackend::VulkanLBVH;spacing.Backend=R::PointSpacingBackend::VulkanLBVH;weights.Backend=R::DensityWeightBackend::VulkanLBVH;
        auto config=h.Control().GetEngineConfigControlState().ActiveConfig;
        R::SetKernelDensityConfig(config,density);R::SetPointSpacingConfig(config,spacing);R::SetDensityWeightConfig(config,weights);
        ASSERT_TRUE(h.Apply(config));ASSERT_TRUE(h.Selection().SetSelectedEntity(scene,entity));
        const char* ids[]={"view.kernel_density","view.point_spacing","view.density_weights"};
        const char* titles[]={"Kernel Density","Point Spacing and Radii","Compact Density Weights"};
        ASSERT_TRUE(h.Shell.SetEditorWindowOpen(ids[method],true));
        R::EditorProcessingContext context;context.Scene=&scene;
        context.JobCommands.Submit=[&](R::JobDesc job,const auto&)->R::JobToken {
            return applied?h.Engine->Jobs().Submit(std::move(job)):R::JobToken{};
        };
        // Seed device diagnostics through the existing mock-front seam; no device execution is claimed.
        R::EditorPointScalarTransactionSnapshot diagnostics;
        diagnostics.GpuInputUploadBytes=36;diagnostics.CpuStageReadbackBytes=12;
        diagnostics.GpuInputCacheHits=1;diagnostics.GpuQueryBatches=1;
        diagnostics.Statistics.Mean=3;diagnostics.Statistics.Minimum=2;diagnostics.Statistics.Maximum=4;
        diagnostics.Statistics.Bandwidth=1;diagnostics.Statistics.AverageSpacing=2;
        bool kept=false;
        const auto observer=h.Shell.AddFrameObserver([&](const Editor::SandboxEditorContext& frame){
            const auto check=[&](const auto& result){
                if(!result || result->Status!=R::EditorCommandStatus::Applied)return false;
                EXPECT_EQ(result->WrittenCount,rows.Size());EXPECT_EQ(result->ActualBackend,"vulkan_lbvh");
                EXPECT_EQ(result->GpuInputUploadBytes,36u);EXPECT_EQ(result->CpuStageReadbackBytes,12u);
                EXPECT_EQ(result->GpuInputCacheHits,1u);EXPECT_EQ(result->GpuQueryBatches,1u);
                return true;};
            if(method==0 && check(frame.PointFields.Results.LastKernelDensityResult)){
                kept=true;EXPECT_EQ(frame.PointFields.Results.LastKernelDensityResult->MeanDensity,3);}
            if(method==1 && check(frame.PointFields.Results.LastPointSpacingResult)){
                kept=true;EXPECT_EQ(frame.PointFields.Results.LastPointSpacingResult->MeanRadius,3);}
            if(method==2 && check(frame.PointAnalysis.Results.LastDensityWeightResult)){
                kept=true;EXPECT_EQ(frame.PointAnalysis.Results.LastDensityWeightResult->MaxWeight,4);}
        });
        const auto commands=R::BindEditorProcessingCommands(context);
        const auto make=[&]{
            std::vector<float> values(rows.Size(),3);
            if(method==0)return R::MakeEditorKernelDensityTransactionForTest(commands,density,values,residency,diagnostics);
            if(method==1)return R::MakeEditorPointSpacingTransactionForTest(commands,spacing,values,residency,diagnostics);
            return R::MakeEditorDensityWeightTransactionForTest(commands,weights,values,residency,diagnostics);};
        auto run=make();ASSERT_TRUE(run);h.Panels.InjectPointScalarTransactionForTest(method,run);
        int frames=0,terminalFrame=0;bool checked=false;
        h.Driver->OnFrame=[&](R::Engine& engine){
            if(++frames>40){ADD_FAILURE()<<"scalar terminal result was not displayed";engine.RequestExit();return;}
            auto* window=ImGui::FindWindowByName(titles[method]);if(!window)return;
            ImGui::SetWindowSize(window,{800,1800});ImGui::SetWindowPos(window,{0,0});
            if(frames==5)ImGui::ActivateItemByID(window->GetID("Accept"));
            const auto state=R::SnapshotEditorPointScalar(commands,run);
            if(state.Phase!=Phase::Applied && state.Phase!=Phase::Failed)return;
            if(!terminalFrame){terminalFrame=frames;return;}
            if(frames==terminalFrame+3){ImGui::GetCurrentContext()->LogBuffer.clear();ImGui::LogToBuffer();ImGui::GetCurrentContext()->LogWindow=nullptr;}
            if(frames==terminalFrame+5){
                EXPECT_EQ(state.Phase,applied?Phase::Applied:Phase::Failed);
                const std::string text{ImGui::GetCurrentContext()->LogBuffer.c_str()};ImGui::LogFinish();
                EXPECT_FALSE(state.Message.empty());EXPECT_NE(text.find(state.Message),std::string::npos)<<text;
                checked=true;engine.RequestExit();}
        };
        h.Engine->Run();EXPECT_TRUE(checked);EXPECT_EQ(kept,applied);h.Shell.RemoveFrameObserver(observer);
        auto pending=make();ASSERT_TRUE(pending);h.Panels.InjectPointScalarTransactionForTest(method,pending);
        h.Panels.Unregister();EXPECT_EQ(R::SnapshotEditorPointScalar(commands,pending).Phase,Phase::Discarded);
        EXPECT_EQ(rows.Exists(method==0?density.Density.Name:method==1?spacing.Radii.Name:weights.Weights.Name),applied);
    }
}

TEST(SandboxProcessingPanels, KMeansDismissAndUnrelatedCompletionRetainGpuCorrelation)
{
    PanelHarness h(Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry(),true);
    const auto entity=h.Scene().Create();
    PopulateSamples(h.Scene().Raw(),entity,R::GeometryElementDomain::PointCloudPoint);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(h.Scene(),entity));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("pointcloud.processing.kmeans",true));
    const R::CommandCorrelationId active{101},other{102};
    unsigned frame=0;bool dismissed=false,replaced=false;
    std::optional<R::KMeansRunCompleted> last;
    const auto observer=h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id="test.kmeans_correlation",.MenuPath={"View"},.Title="K-Means correlation observer",.OpenByDefault=true,
        .Draw=[&](bool&,const Editor::SandboxEditorContext& context){last=context.PointCloudService->Results.LastKMeansResult;}});
    h.Driver->OnFrame=[&](R::Engine& engine){
        ++frame;auto* window=ImGui::FindWindowByName("PointCloud / Processing / K-Means");
        if(!window){if(frame>100){ADD_FAILURE();engine.RequestExit();}return;}
        ImGui::SetWindowSize(window,{850,1500});ImGui::SetWindowPos(window,{0,0});
        if(frame==3){
            R::KMeansRunCompleted queued{.Correlation=active,.Status=R::KMeansRunStatus::Queued,
                .RequestedBackend=R::ClusteringBackend::VulkanCompute};
            h.Methods.InjectKMeansSubmissionForTest(queued);engine.Events().Publish(queued);
        }
        if(frame==6)ImGui::ActivateItemByID(window->GetID("Dismiss##KMeans"));
        if(frame==9){
            EXPECT_FALSE(last);dismissed=!last;
            EXPECT_EQ(h.Methods.KMeansGpuCorrelationForTest(),active);
            engine.Events().Publish(R::KMeansRunCompleted{.Correlation=other,.Status=R::KMeansRunStatus::GeometryProcessingFailed});
        }
        if(frame==12){
            ASSERT_TRUE(last);EXPECT_EQ(last->Correlation,other);replaced=last->Correlation==other;
            EXPECT_EQ(h.Methods.KMeansGpuCorrelationForTest(),active);
            engine.RequestExit();
        }
    };
    h.Engine->Run();EXPECT_TRUE(dismissed);EXPECT_TRUE(replaced);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

// UI-069: the shared operation-progress widget. The view is pure; the draw
// path only maps it to ImGui, so the text and bar mode are asserted on the
// view and the Cancel wiring through a real frame.
TEST(SandboxProcessingPanels, OperationProgressViewFollowsTheReadModel)
{
    using State = R::EditorOperationState;
    R::EditorOperationProgress progress{};

    EXPECT_FALSE(Editor::DescribeOperationProgress(progress, true).Visible) << "None draws nothing";

    progress = {.State = State::Running, .Determinate = true, .Normalized = 0.42f, .ElapsedSeconds = 3.3,
                .Label = "solve"};
    auto view = Editor::DescribeOperationProgress(progress, true);
    ASSERT_TRUE(view.Visible && view.Bar);
    EXPECT_FLOAT_EQ(view.Fraction, 0.42f);
    EXPECT_EQ(view.Overlay, "running \xC2\xB7 solve  42%  3.3s");
    EXPECT_TRUE(view.ShowCancel);
    EXPECT_FALSE(Editor::DescribeOperationProgress(progress, false).ShowCancel) << "no cancel path, no button";

    progress.Determinate = false;
    view = Editor::DescribeOperationProgress(progress, true);
    EXPECT_LT(view.Fraction, 0.0f) << "indeterminate is an animated bar, never 0%";
    EXPECT_EQ(view.Overlay, "running \xC2\xB7 solve  3.3s") << "state word and run label stay visible";

    progress.State = State::Queued;
    progress.ElapsedSeconds = 0.0;
    progress.Label.clear();
    EXPECT_EQ(Editor::DescribeOperationProgress(progress, true).Overlay, "queued") << "no measured time, none shown";
    progress.ElapsedSeconds = 1.0;
    progress.State = State::Running;
    EXPECT_EQ(Editor::DescribeOperationProgress(progress, true).Overlay, "running  1.0s");
    progress.State = State::Queued;

    progress = {.State = State::Failed, .Diagnostic = "solver diverged"};
    view = Editor::DescribeOperationProgress(progress, true);
    EXPECT_TRUE(view.Visible);
    EXPECT_FALSE(view.Bar);
    EXPECT_EQ(view.Overlay, "failed");
    EXPECT_EQ(view.Diagnostic, "solver diverged");
    EXPECT_FALSE(view.ShowCancel) << "a finished run cannot be cancelled";
    progress.State = State::Cancelled;
    EXPECT_EQ(Editor::DescribeOperationProgress(progress, true).Overlay, "cancelled");
    progress = {.State = State::Succeeded, .ElapsedSeconds = 3.3};
    view = Editor::DescribeOperationProgress(progress, true);
    EXPECT_TRUE(view.Visible);
    EXPECT_FALSE(view.Bar);
    EXPECT_EQ(view.Overlay, "done 3.3s");
    EXPECT_FALSE(view.ShowCancel);

    // The asset queue's overlay is the same helper without elapsed time.
    EXPECT_EQ(Editor::FormatProgressOverlay(true, 1.5f, "ignored"), "100%");
    EXPECT_EQ(Editor::FormatProgressOverlay(false, 0.0f, "decoding"), "decoding");
}

TEST(SandboxProcessingPanels, IterationProgressIsADeterminateFractionOfTheIterationCap)
{
    auto progress = Editor::MakeIterationProgress(3u, 12u, 1.5, "ICP iteration 3");
    EXPECT_EQ(progress.State, R::EditorOperationState::Running);
    EXPECT_TRUE(progress.Determinate);
    EXPECT_FLOAT_EQ(progress.Normalized, 0.25f);
    EXPECT_EQ(Editor::DescribeOperationProgress(progress, true).Overlay, "running \xC2\xB7 ICP iteration 3  25%  1.5s");
    EXPECT_FLOAT_EQ(Editor::MakeIterationProgress(20u, 12u, 0.0, {}).Normalized, 1.0f) << "converging past the cap clamps";
    EXPECT_FALSE(Editor::MakeIterationProgress(3u, 0u, 0.0, "x").Determinate) << "no cap, no fraction";
}

// The runtime reaps a finished job a frame after it ends; the slot keeps the last projection of
// its current key until the next run, a key change or a scene replacement.
TEST(SandboxProcessingPanels, OperationRunSlotKeepsTheLastFinishedRunUntilTheKeyOrTheSceneChanges)
{
    using State = R::EditorOperationState;
    Editor::OperationRunSlot slot;
    const auto with = [](R::EditorOperationProgress progress, const std::uint64_t epoch) {
        progress.Epoch = epoch;
        return progress;
    };
    const auto none = [&](const std::uint64_t epoch) { return with({}, epoch); };
    const R::EditorOperationProgress running{.State = State::Running, .Determinate = true, .Normalized = 0.5f};
    const R::EditorOperationProgress failed{.State = State::Failed, .Diagnostic = "diverged"};
    const R::EditorOperationProgress done{.State = State::Succeeded, .ElapsedSeconds = 2.0};

    EXPECT_EQ(slot.Observe(none(1u), "7/a").State, State::None);
    EXPECT_EQ(slot.Observe(with(running, 1u), "7/a").State, State::Running);
    EXPECT_EQ(slot.Observe(with(failed, 1u), "7/a").State, State::Failed);
    EXPECT_EQ(slot.Observe(none(1u), "7/a").State, State::Failed) << "the reaped job's outcome stays visible";
    EXPECT_EQ(slot.Observe(none(1u), "7/a").Diagnostic, "diverged");

    // Another output (or entity) never inherits it.
    EXPECT_EQ(slot.Observe(none(1u), "7/b").State, State::None);
    EXPECT_EQ(slot.Observe(with(done, 1u), "7/b").State, State::Succeeded);
    EXPECT_EQ(slot.Observe(none(1u), "7/a").State, State::None) << "a key change starts afresh";
    EXPECT_EQ(slot.Observe(none(1u), "8/a").State, State::None);

    EXPECT_EQ(slot.Observe(with(running, 1u), "8/a").State, State::Running);
    EXPECT_EQ(slot.Observe(none(1u), "8/a").State, State::None) << "a run that vanished unseen has no outcome to show";

    // An unstamped answer (no runtime surface, or "no run" before a stamp) is no scene change.
    EXPECT_EQ(slot.Observe(with(failed, 1u), "8/a").State, State::Failed);
    EXPECT_EQ(slot.Observe(none(0u), "8/a").State, State::Failed);

    // A scene load or new scene changes the epoch and drops it.
    EXPECT_EQ(slot.Observe(none(2u), "8/a").State, State::None);
    EXPECT_EQ(slot.Observe(with(done, 2u), "8/a").State, State::Succeeded);

    // Watching a new run, or forgetting a discard, drops the old outcome.
    slot.Watch(8u, R::EditorOutputRef{8u, "a"});
    EXPECT_EQ(slot.Observe(none(2u), "8/out:a").State, State::None);
    EXPECT_TRUE(slot.WatchesOutput(8u, "a"));
    EXPECT_FALSE(slot.WatchesOutput(8u, "b"));
    EXPECT_FALSE(slot.WatchesOutput(9u, "a"));
    EXPECT_EQ(slot.Observe(with(done, 2u), "8/out:a").State, State::Succeeded);
    slot.Forget();
    EXPECT_FALSE(slot.Watching());
    EXPECT_EQ(slot.Observe(none(2u), "8/out:a").State, State::None);
}

// A duplicate refusal note describes one click: the next own submission (whatever it answers)
// and a scene replacement drop it.
TEST(SandboxProcessingPanels, DuplicateRefusalNoteEndsWithTheNextOwnSubmissionOrTheScene)
{
    struct Answer
    {
        R::EditorCommandStatus Status{};
        std::string Message{};
    };
    struct State
    {
        std::string ConfigDiagnostic{};
        std::optional<Answer> LastResult{};
        Editor::OperationRunSlot Run{};
    };
    struct Applied
    {
        bool Ok{};
        [[nodiscard]] bool Succeeded() const noexcept { return Ok; }
    };
    const R::EditorProcessingCommands unbound{}; // no editor jobs: a Pending answer is a duplicate
    const std::function<void(Answer)> noSink{};
    const auto submit = [&](State& state, const bool applies, Answer answer) {
        Editor::ApplyQueuedProcessingExecution(unbound, state, 0,
            [applies](int) { return Applied{applies}; }, [answer] { return answer; }, noSink, "rejected", 7u, "out");
    };
    const auto refused = [&](State& state) {
        submit(state, true, {R::EditorCommandStatus::Pending, "out already has an active Running job (job 1:1)."});
        ASSERT_FALSE(state.Run.Note().empty());
        EXPECT_FALSE(state.LastResult.has_value()) << "a duplicate refusal is not the panel's result";
    };

    State rejected;
    refused(rejected);
    submit(rejected, false, {});
    EXPECT_TRUE(rejected.Run.Note().empty()) << "a submission rejected by its apply still replaces the refusal";
    EXPECT_EQ(rejected.ConfigDiagnostic, "rejected");

    State failed;
    refused(failed);
    submit(failed, true, {R::EditorCommandStatus::GeometryProcessingFailed, "nothing to do"});
    EXPECT_TRUE(failed.Run.Note().empty()) << "a synchronous failure replaces the refusal";
    ASSERT_TRUE(failed.LastResult.has_value());
    EXPECT_EQ(failed.LastResult->Status, R::EditorCommandStatus::GeometryProcessingFailed);

    State scene;
    (void)scene.Run.Observe({.Epoch = 1u}, std::string{}); // drawn in scene 1 before the click
    refused(scene);
    (void)scene.Run.Observe({.Epoch = 1u}, "7/out:out");
    EXPECT_FALSE(scene.Run.Note().empty()) << "the refusal stays while its scene does";
    (void)scene.Run.Observe({.Epoch = 2u}, "7/out:out");
    EXPECT_TRUE(scene.Run.Note().empty()) << "a scene replacement drops the previous scene's refusal";
}

TEST(SandboxProcessingPanels, OperationProgressWidgetCancelRequiresAnActiveRunAndAHandler)
{
    PanelHarness h;
    R::EditorOperationProgress progress{.State = R::EditorOperationState::Running, .Determinate = true,
                                        .Normalized = 0.5f};
    int cancels = 0;
    bool useHandler = true;
    const auto windowHandle = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.operation_progress", .MenuPath = {"View"}, .Title = "Operation progress test",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext&) {
            if (ImGui::Begin("Operation progress test"))
                Editor::DrawOperationProgress(progress,
                    useHandler ? std::function<void()>{[&] { ++cancels; }} : std::function<void()>{},
                    "operation_test");
            ImGui::End();
        }});
    int frames = 0, step = 0;
    bool done = false;
    float determinateHeight = 0.0f, indeterminateHeight = 0.0f;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frames > 200) { ADD_FAILURE() << "operation progress test did not finish"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("Operation progress test");
        if (!window) return;
        ImGui::SetWindowSize(window, {500, 200});
        ImGui::SetWindowPos(window, {0, 0});
        const ImGuiID cancel = ImHashStr("Cancel", 0, ImHashStr("operation_test", 0, window->ID));
        ++step;
        if (step == 2) determinateHeight = window->ContentSize.y; // a determinate bar was laid out
        if (step == 4) progress.Determinate = false;
        if (step == 5) { indeterminateHeight = window->ContentSize.y; progress.Determinate = true; }
        if (step == 3) ImGui::ActivateItemByID(cancel);
        if (step == 6)
        {
            EXPECT_EQ(cancels, 1);
            useHandler = false;          // no handler: no button
            ImGui::ActivateItemByID(cancel);
        }
        if (step == 9)
        {
            EXPECT_EQ(cancels, 1);
            useHandler = true;
            progress.State = R::EditorOperationState::Failed; // a finished run has no Cancel
            ImGui::ActivateItemByID(cancel);
        }
        if (step == 10) progress.State = R::EditorOperationState::None;
        if (step == 12)
        {
            EXPECT_EQ(cancels, 1);
            EXPECT_GT(determinateHeight, 0.0f);
            EXPECT_GT(indeterminateHeight, 0.0f) << "an indeterminate bar draws too";
            EXPECT_LT(window->ContentSize.y, determinateHeight) << "None draws nothing";
            done = true;
            engine.RequestExit();
        }
    };
    h.Engine->Run();
    EXPECT_TRUE(done);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(windowHandle));
}

namespace
{
    void PopulateLargeCloud(auto& raw, const auto entity, const std::size_t count, const float shift)
    {
        Geometry::PointCloud::Cloud cloud;
        std::uint32_t seed = 12345u;
        const auto next = [&seed] { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / 16777216.0f; };
        for (std::size_t i = 0u; i < count; ++i)
            (void)cloud.AddPoint({next() + shift, next(), next()});
        GS::PopulateFromCloud(raw, entity, cloud);
        raw.template emplace<G::RenderPoints>(entity);
    }

    [[nodiscard]] bool AnyActiveJob(R::Engine& engine)
    {
        for (const auto& job : engine.Jobs().SnapshotAll())
            if (R::IsActiveEditorJobState(job.State)) return true;
        return false;
    }
}

// A run that never converges ends only through the widget's Cancel, which calls the
// panel's existing cancel (CancelEditorRegistration).
TEST(SandboxProcessingPanels, IcpProgressWidgetCancelStopsARunThatNeverConverges)
{
    PanelHarness h;
    auto& scene = h.Scene();
    using Transform = Extrinsic::ECS::Components::Transform::Component;
    const auto source = scene.Create(), target = scene.Create();
    PopulateLargeCloud(scene.Raw(), source, 20000u, 0.0f);
    PopulateLargeCloud(scene.Raw(), target, 20000u, 0.2f);
    for (const auto entity : {source, target}) scene.Raw().emplace<Transform>(entity);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, source));
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto registration = *R::GetRegistrationConfig(config);
    registration.SourceStableEntityId = R::SelectionController::ToStableEntityId(source);
    registration.TargetStableEntityId = R::SelectionController::ToStableEntityId(target);
    registration.MaxIterations = 100000u;
    registration.ConvergenceThreshold = 0.0;
    registration.InlierRatio = 1.0;
    R::SetRegistrationConfig(config, registration);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.registration", true));
    std::optional<R::EditorRegistrationResult> result;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.icp_cancel_observer", .MenuPath = {"View"}, .Title = "ICP cancel observer", .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            result = context.Registration.Results.LastRegistrationResult;
        }});
    int frame = 0, step = 0, activeFrames = 0, cancelFrame = -1, holdUntil = -1;
    bool completed = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frame > 3000) { ADD_FAILURE() << "ICP did not stop"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("ICP Registration");
        if (!window) return;
        ImGui::SetWindowSize(window, {750, 1400});
        ImGui::SetWindowPos(window, {0, 0});
        ++step;
        if (step == 3) ImGui::ActivateItemByID(window->GetID("Run ICP##ICP"));
        if (step > 3 && AnyActiveJob(engine)) ++activeFrames;
        if (activeFrames > 10 && holdUntil < 0) holdUntil = frame + 40;
        // The run does not end on its own: it stays active through the hold.
        if (holdUntil >= 0 && frame <= holdUntil) EXPECT_TRUE(AnyActiveJob(engine)) << "frame " << frame;
        if (holdUntil >= 0 && frame > holdUntil && cancelFrame < 0) cancelFrame = frame;
        if (cancelFrame >= 0)
        {
            // The button only exists while the run is active; activating it every frame is a no-op afterwards.
            ImGui::ActivateItemByID(ImHashStr("Cancel", 0, ImHashStr("icp_progress", 0, window->ID)));
            if (!AnyActiveJob(engine) && frame > cancelFrame + 2)
            {
                completed = true;
                engine.RequestExit();
            }
        }
    };
    h.Engine->Run();
    EXPECT_TRUE(completed) << "the 100000-iteration run only ends through Cancel";
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->Succeeded());
    EXPECT_NE(result->Message.find("ICP was cancelled"), std::string::npos) << result->Message;
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

TEST(SandboxProcessingPanels, CpdProgressWidgetCancelStopsARunToTheCap)
{
    PanelHarness h;
    auto& scene = h.Scene();
    using Transform = Extrinsic::ECS::Components::Transform::Component;
    const auto source = scene.Create(), target = scene.Create();
    PopulateLargeCloud(scene.Raw(), source, 4000u, 0.0f);
    PopulateLargeCloud(scene.Raw(), target, 4000u, 0.2f);
    for (const auto entity : {source, target}) scene.Raw().emplace<Transform>(entity);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, source));
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto cpd = *R::GetCoherentPointDriftConfig(config);
    cpd.SourceStableEntityId = R::SelectionController::ToStableEntityId(source);
    cpd.TargetStableEntityId = R::SelectionController::ToStableEntityId(target);
    cpd.MaxIterations = 10000u;
    cpd.Tolerance = 0.0;
    R::SetCoherentPointDriftConfig(config, cpd);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.coherent_point_drift", true));
    int frame = 0, step = 0, activeFrames = 0, cancelFrame = -1, holdUntil = -1, settled = -1;
    bool completed = false, cancelledPhase = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frame > 3000) { ADD_FAILURE() << "CPD did not stop"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("Coherent Point Drift");
        if (!window) return;
        ImGui::SetWindowSize(window, {500, 1400});
        ImGui::SetWindowPos(window, {0, 0});
        ++step;
        if (step == 3) ImGui::ActivateItemByID(window->GetID("Start##CPD"));
        if (step == 6) ImGui::ActivateItemByID(window->GetID("Run to end##CPD"));
        if (step > 6 && AnyActiveJob(engine)) ++activeFrames;
        if (activeFrames > 10 && holdUntil < 0) holdUntil = frame + 40;
        if (holdUntil >= 0 && frame <= holdUntil) EXPECT_TRUE(AnyActiveJob(engine)) << "frame " << frame;
        if (holdUntil >= 0 && frame > holdUntil && cancelFrame < 0) cancelFrame = frame;
        if (cancelFrame >= 0)
        {
            ImGui::ActivateItemByID(ImHashStr("Cancel", 0, ImHashStr("cpd_progress", 0, window->ID)));
            if (!AnyActiveJob(engine) && settled < 0)
            {
                settled = frame;
                ImGui::GetCurrentContext()->LogBuffer.clear();
                ImGui::LogToBuffer();
                ImGui::GetCurrentContext()->LogWindow = nullptr;
            }
            if (settled >= 0 && frame > settled + 4)
            {
                cancelledPhase = std::string{ImGui::GetCurrentContext()->LogBuffer.c_str()}.find("Phase: cancelled") != std::string::npos;
                ImGui::LogFinish();
                completed = true;
                engine.RequestExit();
            }
        }
    };
    h.Engine->Run();
    EXPECT_TRUE(completed) << "the 10000-iteration run only ends through Cancel";
    EXPECT_TRUE(cancelledPhase) << "the run ends in the Cancelled phase";
    if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
}

// UI-069: the derived-job table cell and the UV regeneration status line draw the shared widget.
TEST(SandboxProcessingPanels, DerivedJobCellsAndUvStatusLineShowTheSharedWidget)
{
    PanelHarness h;
    using State = R::JobState;
    R::EditorBoundRenderStateModel bound{};
    const auto row = [](const char* label, const State state, const float progress, const bool determinate) {
        R::EditorBoundRenderStateRow value{};
        value.Kind = R::EditorBoundRenderStateRowKind::DerivedJob;
        value.Label = label;
        value.JobStatus = state;
        value.JobProgress = progress;
        value.JobProgressDeterminate = determinate;
        return value;
    };
    bound.Rows.push_back(row("determinate", State::Running, 0.5f, true));
    bound.Rows.push_back(row("silent", State::Running, 0.0f, false));
    bound.Rows.push_back(row("finished", State::Published, 1.0f, true));
    bound.Rows.push_back(row("broken", State::Dropped, 0.0f, false));
    R::EditorTextureBakeControlsModel model;
    model.Uv.UvRegenerationJob = R::EditorJobRecord{.State = State::Running, .Name = "UV atlas",
                                                    .ProgressDeterminate = false, .ElapsedMilliseconds = 2500u};
    std::optional<R::EditorUvRegenerationCommandResult> result, adoption;
    std::int32_t width = 1024, height = 1024, padding = 2;
    bool force = false, preserve = false;
    const Editor::SandboxUvRegenerationControls controls{&result, &adoption, &width, &height, &padding, &force, &preserve};
    const auto windowHandle = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.derived_job_cells", .MenuPath = {"View"}, .Title = "Derived job cells test", .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            if (ImGui::Begin("Derived job cells test"))
            {
                Editor::DrawBoundRenderStateRows(bound);
                Editor::DrawSandboxUvRegenerationControls(model, &context, controls);
            }
            ImGui::End();
        }});
    int frames = 0, step = 0;
    std::string text;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frames > 200) { ADD_FAILURE() << "derived job cells test did not finish"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("Derived job cells test");
        if (!window) return;
        ImGui::SetWindowSize(window, {1400, 900});
        ImGui::SetWindowPos(window, {0, 0});
        ++step;
        if (step == 3)
        {
            ImGui::GetCurrentContext()->LogBuffer.clear();
            ImGui::LogToBuffer();
            ImGui::GetCurrentContext()->LogWindow = nullptr;
        }
        if (step == 6)
        {
            text = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
            engine.RequestExit();
        }
    };
    h.Engine->Run();
    if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
    EXPECT_NE(text.find("running  50%"), std::string::npos) << text;
    EXPECT_NE(text.find("done"), std::string::npos) << text;
    EXPECT_NE(text.find("failed"), std::string::npos) << text;
    // The UV status line names the run and its time, with no percentage for an indeterminate job.
    EXPECT_NE(text.find("running \xC2\xB7 UV atlas  2.5s"), std::string::npos) << text;
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(windowHandle));
}

namespace
{
    // Clicks `runLabel` in window `title`, waits for `finished()`, then expects the finished run on
    // screen; after the selection moves to `other` nothing of it may show.
    template <class Finished>
    void ExpectRunShownOnlyForItsEntity(PanelHarness& h, const char* title, const char* runLabel,
                                        const auto home, const auto other, Finished finished)
    {
        auto& scene = h.Scene();
        int frame = 0, step = 0, phase = 0, at = 0;
        bool completed = false;
        const auto capture = [] {
            ImGui::GetCurrentContext()->LogBuffer.clear();
            ImGui::LogToBuffer();
            ImGui::GetCurrentContext()->LogWindow = nullptr;
        };
        const auto read = [] {
            std::string text{ImGui::GetCurrentContext()->LogBuffer.c_str()};
            ImGui::LogFinish();
            return text;
        };
        h.Driver->OnFrame = [&](R::Engine& engine) {
            if (++frame > 600) { ADD_FAILURE() << title << " did not finish"; engine.RequestExit(); return; }
            auto* window = ImGui::FindWindowByName(title);
            if (!window) return;
            ImGui::SetWindowSize(window, {750, 1400});
            ImGui::SetWindowPos(window, {0, 0});
            if (++step == 3) ImGui::ActivateItemByID(window->GetID(runLabel));
            // Some methods select their output; the run belongs to the entity it ran on.
            if (phase == 0 && step > 3 && finished())
            {
                EXPECT_TRUE(h.Selection().SetSelectedEntity(scene, home));
                phase = 1; at = frame; capture();
            }
            if (phase == 1 && frame == at + 5)
            {
                const std::string text = read();
                EXPECT_NE(text.find("done"), std::string::npos) << title << ": the finished run stays visible\n" << text;
                EXPECT_TRUE(h.Selection().SetSelectedEntity(scene, other));
                phase = 2; at = frame;
            }
            if (phase == 2 && frame == at + 3) capture();
            if (phase == 2 && frame == at + 8)
            {
                const std::string text = read();
                EXPECT_EQ(text.find("done"), std::string::npos) << title << ": another entity must not show this run\n" << text;
                completed = true;
                engine.RequestExit();
            }
        };
        h.Engine->Run();
        if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
        EXPECT_TRUE(completed) << title;
    }
}

TEST(SandboxProcessingPanels, OutlierPanelShowsItsFinishedRunOnlyForItsEntity)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create(), other = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
    PopulateSamples(scene.Raw(), other, R::GeometryElementDomain::PointCloudPoint);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    auto& props = scene.Raw().get<GS::Vertices>(entity).Properties;
    props.Get<glm::vec3>("v:position")[8] = {100, 100, 100};
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto outliers = *R::GetOutlierAnalysisConfig(config);
    outliers.StableEntityId = R::SelectionController::ToStableEntityId(entity);
    outliers.Method = R::OutlierAnalysisMethod::Radius;
    outliers.Radius = 1.1f;
    outliers.MinimumNeighbors = 1;
    R::SetOutlierAnalysisConfig(config, outliers);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.outlier_analysis", true));
    ExpectRunShownOnlyForItsEntity(h, "Outlier Analysis", "Detect outliers", entity, other,
        [&] { return std::as_const(props).Exists(outliers.Mask.Name); });
}

TEST(SandboxProcessingPanels, PointConstructionPanelShowsItsFinishedRunOnlyForItsEntity)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create(), other = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
    PopulateSamples(scene.Raw(), other, R::GeometryElementDomain::PointCloudPoint);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto construction = *R::GetPointConstructionConfig(config);
    construction.StableEntityId = R::SelectionController::ToStableEntityId(entity);
    R::SetPointConstructionConfig(config, construction);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.point_construction", true));
    std::optional<R::EditorPointConstructionResult> result;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.construction_progress_observer", .MenuPath = {"View"}, .Title = "Construction progress observer",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            result = context.PointConstruction.Results.LastPointConstructionResult;
        }});
    ExpectRunShownOnlyForItsEntity(h, "Construct from Points", "Construct", entity, other,
        [&] { return result && result->Status != R::EditorCommandStatus::Pending; });
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

// Consolidation names its run by the correlation id of its submission, like K-Means.
TEST(SandboxProcessingPanels, ConsolidationPanelShowsItsFinishedRunOnlyForItsEntity)
{
    PanelHarness h(Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry(), false, true);
    auto& scene = h.Scene();
    const auto entity = scene.Create(), other = scene.Create();
    for (const auto e : {entity, other})
    {
        auto& props = scene.Raw().emplace<GS::Vertices>(e).Properties;
        props.Resize(64);
        auto positions = props.GetOrAdd<glm::vec3>("v:position", {});
        for (std::size_t i = 0u; i < 64u; ++i)
            positions[i] = {float(i % 8) * 0.1f, float(i / 8) * 0.1f, 0.01f * float((i * 7u) % 5u)};
        scene.Raw().emplace<G::RenderPoints>(e);
    }
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("pointcloud.processing.consolidation", true));
    std::optional<R::PointCloudConsolidationResult> result;
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.consolidation_progress_observer", .MenuPath = {"View"}, .Title = "Consolidation progress observer",
        .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
            result = context.PointCloudService->Results.LastPointCloudConsolidationResult;
        }});
    ExpectRunShownOnlyForItsEntity(
        h, "PointCloud / Processing / Consolidate (LOP/WLOP/CLOP/EAR)",
        "Consolidate selected property set##PointCloudConsolidation", entity, other,
        [&] { return result && result->Status != R::PointCloudConsolidationRunStatus::Queued; });
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

// The slot names the run at submit (never from a later draft), maps a GPU transaction waiting
// for Accept to "awaiting accept" instead of its finished compute job, forgets a discard, and
// shows only for the run's entity.
TEST(SandboxProcessingPanels, OperationRunSlotCapturesTheKeyAtSubmitAndMapsTransactionPhases)
{
    PanelHarness h;
    R::EditorOperationProgress canned{};
    std::optional<R::EditorOperationRunKey> lastKey;
    R::EditorProcessingContext context;
    context.JobCommands.Progress = [&](const R::EditorOperationRunKey& key) {
        lastKey = key;
        auto progress = canned;
        progress.Epoch = 1u;
        return progress;
    };
    const auto commands = R::BindEditorProcessingCommands(context);
    Editor::OperationRunSlot slot;
    std::uint32_t selected = 7u;
    const auto windowHandle = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.run_slot", .MenuPath = {"View"}, .Title = "Run slot test", .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext&) {
            if (ImGui::Begin("Run slot test")) slot.Draw(commands, selected, "slot");
            ImGui::End();
        }});
    struct Case { const char* Name; std::function<void()> Arrange; std::function<void(const std::string&)> Check; };
    const R::EditorOperationProgress running{.State = R::EditorOperationState::Running, .Determinate = true,
                                             .Normalized = 0.5f, .Label = "solve"};
    const R::EditorOperationProgress finished{.State = R::EditorOperationState::Succeeded};
    const std::array cases{
        Case{"running", [&] { slot.WatchOutput(7u, "a"); canned = running; },
             [&](const std::string& t) {
                 EXPECT_NE(t.find("running"), std::string::npos) << t;
                 ASSERT_TRUE(lastKey.has_value());
                 const auto* output = std::get_if<R::EditorOutputRef>(&*lastKey);
                 ASSERT_NE(output, nullptr);
                 EXPECT_EQ(output->EntityId, 7u);
                 EXPECT_EQ(output->OutputName, "a") << "the key captured at submit";
             }},
        Case{"awaiting accept", [&] { canned = finished; slot.AwaitingAccept(true); },
             [&](const std::string& t) {
                 EXPECT_NE(t.find("awaiting accept"), std::string::npos) << t;
                 EXPECT_EQ(t.find("done"), std::string::npos) << t;
             }},
        Case{"accepted", [&] { slot.AwaitingAccept(false); },
             [&](const std::string& t) { EXPECT_NE(t.find("done"), std::string::npos) << t; }},
        Case{"another entity", [&] { selected = 8u; },
             [&](const std::string& t) { EXPECT_EQ(t.find("done"), std::string::npos) << t; }},
        Case{"back", [&] { selected = 7u; },
             [&](const std::string& t) { EXPECT_NE(t.find("done"), std::string::npos) << t; }},
        // RUNTIME-279: a run cancelled through the job surface reads "cancelled", never "done".
        Case{"cancelled", [&] { slot.WatchOutput(7u, "a"); canned = {.State = R::EditorOperationState::Cancelled, .Diagnostic = "cancelled"}; },
             [&](const std::string& t) {
                 EXPECT_NE(t.find("cancelled"), std::string::npos) << t;
                 EXPECT_EQ(t.find("done"), std::string::npos) << t;
             }},
        Case{"discarded", [&] { slot.Forget(); canned = {}; },
             [&](const std::string& t) {
                 EXPECT_EQ(t.find("done"), std::string::npos) << t;
                 EXPECT_EQ(t.find("awaiting"), std::string::npos) << t;
             }},
    };
    int frame = 0, step = 0;
    std::size_t index = 0;
    bool completed = false;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frame > 300) { ADD_FAILURE() << "slot test did not finish"; engine.RequestExit(); return; }
        if (!ImGui::FindWindowByName("Run slot test")) return;
        ++step;
        // Each case: arrange, let two frames draw it, capture two, read.
        const int local = step - 1 - static_cast<int>(index) * 6;
        if (local == 0) cases[index].Arrange();
        if (local == 2)
        {
            ImGui::GetCurrentContext()->LogBuffer.clear();
            ImGui::LogToBuffer();
            ImGui::GetCurrentContext()->LogWindow = nullptr;
        }
        if (local == 4)
        {
            const std::string text{ImGui::GetCurrentContext()->LogBuffer.c_str()};
            ImGui::LogFinish();
            SCOPED_TRACE(cases[index].Name);
            cases[index].Check(text);
            if (++index == cases.size()) { completed = true; engine.RequestExit(); }
        }
    };
    h.Engine->Run();
    if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(windowHandle));
}

namespace
{
    // Mesh methods through their panels, on a small grid mesh with a second mesh to select away to.
    void ExpectMeshPanelRun(const char* windowId, const char* title, const char* runLabel)
    {
        PanelHarness h;
        auto& scene = h.Scene();
        const auto entity = scene.Create(), other = scene.Create();
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::MeshVertex);
        PopulateSamples(scene.Raw(), other, R::GeometryElementDomain::MeshVertex);
        ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
        ASSERT_TRUE(h.Shell.SetEditorWindowOpen(windowId, true));
        const std::uint64_t jobsBefore = h.Engine->Jobs().Stats().SubmittedJobs;
        ExpectRunShownOnlyForItsEntity(h, title, runLabel, entity, other,
            [&] { return h.Engine->Jobs().Stats().SubmittedJobs > jobsBefore && !AnyActiveJob(*h.Engine); });
    }
}

TEST(SandboxProcessingPanels, SubdividePanelShowsItsFinishedRunOnlyForItsEntity)
{
    ExpectMeshPanelRun("mesh.processing.subdivide", "Mesh / Processing / Subdivide", "Subdivide##MeshSubdivide");
}

TEST(SandboxProcessingPanels, RemeshPanelShowsItsFinishedRunOnlyForItsEntity)
{
    ExpectMeshPanelRun("mesh.processing.remesh", "Mesh / Processing / Remesh", "Remesh##MeshRemesh");
}

TEST(SandboxProcessingPanels, DenoisePanelShowsItsFinishedRunOnlyForItsEntity)
{
    ExpectMeshPanelRun("mesh.processing.denoise", "Mesh / Processing / Denoise", "Denoise##MeshDenoise");
}

// The curvature job is filed under the serialized command; the panel names its run by the same
// text, which this pins.
TEST(SandboxProcessingPanels, CurvaturePanelShowsItsFinishedRunOnlyForItsEntity)
{
    ExpectMeshPanelRun("mesh.processing.curvature", "Mesh / Processing / Curvature", "Compute##MeshCurvature");
}

// A run started elsewhere (an agent or batch call) on the output the panel's draft names shows in
// the panel too: nothing was submitted here, so the slot asks for the draft's own output.
TEST(SandboxProcessingPanels, ARunStartedElsewhereShowsForTheDraftsOutput)
{
    PanelHarness h;
    auto& scene = h.Scene();
    const auto entity = scene.Create();
    PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
    ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
    auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
    auto density = *R::GetKernelDensityConfig(config);
    density.StableEntityId = R::SelectionController::ToStableEntityId(entity);
    density.KNeighbors = 3;
    density.Bandwidth = 0.5f;
    density.Density.Name = "elsewhere_density";
    R::SetKernelDensityConfig(config, density);
    ASSERT_TRUE(h.Apply(config));
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen("view.kernel_density", true));
    R::EditorProcessingCommands commands{};
    const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
        .Id = "test.elsewhere_observer", .MenuPath = {"View"}, .Title = "Elsewhere observer", .OpenByDefault = true,
        .Draw = [&](bool&, const Editor::SandboxEditorContext& context) { commands = context.PointFields.Commands; }});
    auto& props = scene.Raw().get<GS::Vertices>(entity).Properties;
    int frame = 0, step = 0, publishedAt = 0;
    std::string text;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frame > 400) { ADD_FAILURE() << "elsewhere test did not finish"; engine.RequestExit(); return; }
        auto* window = ImGui::FindWindowByName("Kernel Density");
        if (!window || !commands.IsBound()) return;
        ImGui::SetWindowSize(window, {700, 1000});
        ImGui::SetWindowPos(window, {0, 0});
        // The panel never clicks Estimate: the command comes from outside, as an agent's would.
        if (++step == 3) EXPECT_TRUE(R::ApplyEditorKernelDensityCommand(commands, density).Status != R::EditorCommandStatus::MissingScene);
        if (!publishedAt && std::as_const(props).Exists("elsewhere_density"))
        {
            publishedAt = frame;
            ImGui::GetCurrentContext()->LogBuffer.clear();
            ImGui::LogToBuffer();
            ImGui::GetCurrentContext()->LogWindow = nullptr;
        }
        if (publishedAt && frame == publishedAt + 5)
        {
            text = ImGui::GetCurrentContext()->LogBuffer.c_str();
            ImGui::LogFinish();
            engine.RequestExit();
        }
    };
    h.Engine->Run();
    if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
    EXPECT_GT(publishedAt, 0);
    EXPECT_NE(text.find("done"), std::string::npos) << text;
    EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
}

// RUNTIME-313: clicking while another caller's run (here an agent-style command) is active on the
// same output is refused as a duplicate. The refusal is shown with the run slot, which follows the
// active run; it is never stored as the panel's result, which would read "Pending" for good
// because the active run delivers to its own caller. One case per panel path: the shared
// DrawProcessingExecution (kernel density) and a direct queued execution (outliers).
namespace
{
    struct DuplicateCase
    {
        const char* WindowId;
        const char* Title;
        const char* Button;
        const char* Output;
        // Applies the panel's config; returns the command an agent would send for the same output.
        std::function<std::function<R::EditorCommandStatus(const R::EditorProcessingCommands&, std::function<void()>)>(
            PanelHarness&, std::uint32_t)> Configure;
        std::function<R::EditorProcessingCommands(const Editor::SandboxEditorContext&)> Commands;
        std::function<std::optional<R::EditorCommandStatus>(const Editor::SandboxEditorContext&)> PanelResult;
    };

    void ExpectDuplicateRefusalIsNotThePanelsResult(const DuplicateCase& c)
    {
        SCOPED_TRACE(c.Title);
        PanelHarness h;
        auto& scene = h.Scene();
        const auto entity = scene.Create();
        PopulateSamples(scene.Raw(), entity, R::GeometryElementDomain::PointCloudPoint);
        ASSERT_TRUE(h.Selection().SetSelectedEntity(scene, entity));
        const auto elsewhereCommand = c.Configure(h, R::SelectionController::ToStableEntityId(entity));
        ASSERT_TRUE(h.Shell.SetEditorWindowOpen(c.WindowId, true));
        R::EditorProcessingCommands commands{};
        std::optional<R::EditorCommandStatus> panelResult;
        const auto observer = h.Shell.RegisterEditorWindow(Editor::EditorWindowDescriptor{
            .Id = "test.duplicate_observer", .MenuPath = {"View"}, .Title = "Duplicate observer", .OpenByDefault = true,
            .Draw = [&](bool&, const Editor::SandboxEditorContext& context) {
                commands = c.Commands(context);
                panelResult = c.PanelResult(context);
            }});
        auto& props = scene.Raw().get<GS::Vertices>(entity).Properties;
        std::atomic_bool release{false};
        bool delivered = false;
        int frame = 0, step = 0, deliveredAt = 0;
        std::string text;
        h.Driver->OnFrame = [&](R::Engine& engine) {
            if (++frame > 400) { ADD_FAILURE() << "duplicate test did not finish"; engine.RequestExit(); return; }
            auto* window = ImGui::FindWindowByName(c.Title);
            if (!window || !commands.IsBound()) return;
            ImGui::SetWindowSize(window, {700, 1400});
            ImGui::SetWindowPos(window, {0, 0});
            ++step;
            if (step == 2)
            {
                // Hold the only worker so the elsewhere run stays queued while the panel clicks.
                (void)engine.Jobs().Submit(R::JobDesc{.DebugName = "blocker",
                    .Work = [&](const R::JobCancellation&) {
                        while (!release.load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        return R::JobResultEnvelope::Make(true); },
                    .PublishCompletion = [](R::KernelEventBus&, const R::JobResultEnvelope&) { return true; }});
                EXPECT_EQ(elsewhereCommand(commands, [&] { delivered = true; }), R::EditorCommandStatus::Pending);
            }
            if (step == 3)
            {
                ImGui::GetCurrentContext()->LogBuffer.clear();
                ImGui::LogToBuffer();
                ImGui::GetCurrentContext()->LogWindow = nullptr;
                ImGui::ActivateItemByID(window->GetID(c.Button));
            }
            if (step == 8)
            {
                text = ImGui::GetCurrentContext()->LogBuffer.c_str();
                ImGui::LogFinish();
                EXPECT_FALSE(panelResult.has_value()) << "the refusal is not the panel's result";
                release.store(true, std::memory_order_release);
            }
            if (step > 8 && !deliveredAt && delivered) deliveredAt = frame;
            if (deliveredAt && frame == deliveredAt + 5) engine.RequestExit();
        };
        h.Engine->Run();
        if (ImGui::GetCurrentContext()->LogEnabled) ImGui::LogFinish();
        release.store(true, std::memory_order_release);
        EXPECT_TRUE(delivered);
        EXPECT_TRUE(std::as_const(props).Exists(c.Output));
        EXPECT_NE(text.find("already has an active"), std::string::npos) << text;
        EXPECT_FALSE(panelResult == R::EditorCommandStatus::Pending)
            << "the panel never reads Pending after the active run delivered elsewhere";
        EXPECT_TRUE(h.Shell.UnregisterEditorWindow(observer));
    }
}

TEST(SandboxProcessingPanels, DuplicateRunRefusalIsNotStoredAsThePanelsResult)
{
    ExpectDuplicateRefusalIsNotThePanelsResult({
        .WindowId = "view.kernel_density", .Title = "Kernel Density", .Button = "Estimate density",
        .Output = "duplicate_density",
        .Configure = [](PanelHarness& h, const std::uint32_t id) {
            auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
            auto density = *R::GetKernelDensityConfig(config);
            density.StableEntityId = id;
            density.KNeighbors = 3;
            density.Bandwidth = 0.5f;
            density.Density.Name = "duplicate_density";
            R::SetKernelDensityConfig(config, density);
            EXPECT_TRUE(h.Apply(config));
            return std::function<R::EditorCommandStatus(const R::EditorProcessingCommands&, std::function<void()>)>(
                [density](const R::EditorProcessingCommands& commands, std::function<void()> done) {
                    return R::ApplyEditorKernelDensityCommand(commands, density,
                        [done](R::EditorKernelDensityResult) { done(); }).Status; });
        },
        .Commands = [](const Editor::SandboxEditorContext& context) { return context.PointFields.Commands; },
        .PanelResult = [](const Editor::SandboxEditorContext& context) -> std::optional<R::EditorCommandStatus> {
            const auto& r = context.PointFields.Results.LastKernelDensityResult;
            return r ? std::optional{r->Status} : std::nullopt; }});
    ExpectDuplicateRefusalIsNotThePanelsResult({
        .WindowId = "view.outlier_analysis", .Title = "Outlier Analysis", .Button = "Detect outliers",
        .Output = "duplicate_outliers",
        .Configure = [](PanelHarness& h, const std::uint32_t id) {
            auto config = h.Control().GetEngineConfigControlState().ActiveConfig;
            auto outliers = *R::GetOutlierAnalysisConfig(config);
            outliers.StableEntityId = id;
            outliers.Method = R::OutlierAnalysisMethod::Radius;
            outliers.Radius = 1.1f;
            outliers.MinimumNeighbors = 1;
            outliers.Mask.Name = "duplicate_outliers";
            R::SetOutlierAnalysisConfig(config, outliers);
            EXPECT_TRUE(h.Apply(config));
            return std::function<R::EditorCommandStatus(const R::EditorProcessingCommands&, std::function<void()>)>(
                [outliers](const R::EditorProcessingCommands& commands, std::function<void()> done) {
                    return R::ApplyEditorOutlierAnalysisCommand(commands, outliers,
                        [done](R::EditorOutlierAnalysisResult) { done(); }).Status; });
        },
        .Commands = [](const Editor::SandboxEditorContext& context) { return context.PointAnalysis.Commands; },
        .PanelResult = [](const Editor::SandboxEditorContext& context) -> std::optional<R::EditorCommandStatus> {
            const auto& r = context.PointAnalysis.Results.LastOutlierAnalysisResult;
            return r ? std::optional{r->Status} : std::nullopt; }});
}
