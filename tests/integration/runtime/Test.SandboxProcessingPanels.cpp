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

import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.JobService;
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
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Sandbox.ConfigSections;
import Extrinsic.Sandbox.Editor.MeshProcessingPanels;
import Extrinsic.Sandbox.Editor.MethodPanels;
import Extrinsic.Sandbox.Editor.Shell;
import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.PointCloud;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.PointCloudConsolidationTypes;

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
            bool clustering = false)
        {
            Config::EngineConfig config{};
            Config::PopulateEngineConfigSectionDefaults(config, sections);
            config.Simulation.WorkerThreadCount = 1u;
            config.ReferenceScene.Enabled = false;
            config.Camera.Enabled = false;
            config.Window.Backend = Config::WindowBackend::Null;
            auto driver = std::make_unique<PanelDriver>();
            Driver = driver.get();
            Engine = std::make_unique<Intrinsic::Tests::RuntimeTestKernel>(
                config, std::move(driver));
            Engine->EmplaceModule<R::EngineConfigControl>(std::move(sections));
            Engine->EmplaceModule<R::SceneInteractionModule>();
            Engine->EmplaceModule<R::AsyncWorkModule>();
            if (clustering) Engine->EmplaceModule<R::ClusteringModule>();
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
        if (frame == 3 || frame == 12 || frame == 18)
            ExpectInputEntities(active, 0u);
        if (frame == 6 || frame == 15)
            ExpectInputEntities(active, R::SelectionController::ToStableEntityId(first));
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
                ImGui::SetScrollY(window, window->ScrollMax.y);
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
    selection.SelectedEntities = {{.StableEntityId=3}};
    sync();
    EXPECT_EQ(source.Entity, 3u);
    EXPECT_EQ(target.Entity, 0u);
    target.Entity = 9;
    sync();
    EXPECT_EQ(target.Entity, 9u);
    selection.SelectedEntities.clear();
    sync();
    EXPECT_EQ(source.Entity, 0u);
    EXPECT_EQ(target.Entity, 0u);
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

TEST(SandboxProcessingPanels, CpuAccelerationControlsPersistTheRequestedExecutionPath)
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
        Control{"view.normal_estimation", "Normal Estimation", "Acceleration##Normals", "Vulkan LBVH (CPU fit)",
            [](const auto& c) { return int(R::GetNormalEstimationConfig(c)->Backend); }},
        Control{"view.outlier_analysis", "Outlier Analysis", "Acceleration", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetOutlierAnalysisConfig(c)->Backend); }},
        Control{"view.keypoint_analysis", "ISS Keypoint Analysis", "Acceleration", "Vulkan LBVH neighborhoods",
            [](const auto& c) { return int(R::GetKeypointAnalysisConfig(c)->Backend); }},
        Control{"view.keypoint_analysis", "ISS Keypoint Analysis", "Backend", "Vulkan",
            [](const auto& c) { return int(R::GetKeypointAnalysisConfig(c)->Backend); },1,3},
        Control{"view.descriptor_analysis", "FPFH Descriptor Analysis", "Acceleration", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetDescriptorAnalysisConfig(c)->Backend); }},
        Control{"view.kernel_density", "Kernel Density", "Acceleration", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetKernelDensityConfig(c)->Backend); }},
        Control{"view.density_weights", "Compact Density Weights", "Acceleration", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetDensityWeightConfig(c)->Backend); }},
        Control{"view.point_construction", "Construct from Points", "Acceleration", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetPointConstructionConfig(c)->Backend); }},
        Control{"view.point_spacing", "Point Spacing and Radii", "Acceleration", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetPointSpacingConfig(c)->Backend); }},
        Control{"view.bilateral_filter", "Bilateral Point Filter", "Acceleration", "Vulkan LBVH",
            [](const auto& c) { return int(R::GetBilateralFilterConfig(c)->Backend); }},
        Control{"view.registration", "ICP Registration", "Acceleration##ICP", "Vulkan LBVH (CPU solve)",
            [](const auto& c) { return int(R::GetRegistrationConfig(c)->Backend); }},
    };
    ASSERT_TRUE(h.Shell.SetEditorWindowOpen(controls.front().Window, true));
    std::size_t action = 0;
    int step = 0, frames = 0;
    h.Driver->OnFrame = [&](R::Engine& engine) {
        if (++frames > 180) { ADD_FAILURE() << "Acceleration controls did not finish"; engine.RequestExit(); return; }
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
        int frame = 0, step = 0;
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
            completed = true;
            engine.RequestExit();
        };
        h.Engine->Run();
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
    std::int32_t width = 1, height = 1, padding = 0, resolution = 1024, uvPadding = 2;
    float texelsPerUnit = 0.0f;
    bool force = true, preserve = false;
    const Editor::SandboxUvRegenerationControls controls{
        .LastResult = &result, .LastExtentAdoption = &adopted,
        .BakeWidth = &width, .BakeHeight = &height, .BakePadding = &padding,
        .UvResolution = &resolution, .UvPadding = &uvPadding,
        .UvTexelsPerUnit = &texelsPerUnit,
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
    std::int32_t bakeWidth = 1024, bakeHeight = 1024, bakePadding = 0, resolution = 64, padding = 2;
    float density = 0.f;
    bool force = true, preserve = false;
    const Editor::SandboxUvRegenerationControls controls{
        &result, &adoption, &bakeWidth, &bakeHeight, &bakePadding,
        &resolution, &padding, &density, &force, &preserve};
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
