#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Sandbox.ConfigSections;
import Extrinsic.Sandbox.Editor.MethodPanels;
import Extrinsic.Sandbox.Editor.Shell;
import Geometry.Properties;

namespace Config = Extrinsic::Core::Config;
namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Runtime = Extrinsic::Runtime;
namespace Sandbox = Extrinsic::Sandbox;
namespace Editor = Extrinsic::Sandbox::Editor;

namespace
{
    class OneFrameApplication final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override {}
        void Frame(double, double) override
        {
            Kernel().RequestExit();
        }
        void Shutdown() override {}
    };

    class WaitForConsolidationApplication final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            Service = Kernel().Services().Find<
                Runtime::PointCloudConsolidationService>();
            if (Service != nullptr)
            {
                Subscription = Service->SubscribeCompleted(
                    [this](
                        const Runtime::PointCloudConsolidationResult& result)
                    {
                        Completion = result;
                    });
            }
        }

        void Frame(double, double) override
        {
            ++Frames;
            if (Completion.has_value() || Frames > 240u)
            {
                TimedOut = !Completion.has_value();
                Kernel().RequestExit();
            }
        }

        void Shutdown() override
        {
            if (Service != nullptr)
                Service->Unsubscribe(Subscription);
        }

        Runtime::PointCloudConsolidationService* Service{};
        Runtime::KernelEventSubscription Subscription{};
        std::optional<Runtime::PointCloudConsolidationResult> Completion{};
        std::uint32_t Frames{0u};
        bool TimedOut{false};
    };

    [[nodiscard]] Config::EngineConfig HeadlessConfig()
    {
        Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 2u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Config::WindowBackend::Null;
        config.Render.EnablePromotedVulkanDevice = false;
        config.Render.DefaultRecipeConfigPath.clear();
        return config;
    }

    struct EditorUiShellHarness
    {
        Intrinsic::Tests::RuntimeTestKernel Kernel{
            HeadlessConfig(),
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
            menu.begin(),
            menu.end(),
            [id](const Runtime::EditorWindowMenuEntry& entry)
            {
                return entry.Id == id;
            });
        return found == menu.end() ? nullptr : &*found;
    }

    [[nodiscard]] ECS::EntityHandle AddNoisyPointCloud(
        ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity = scene.Create();
        auto& vertices = scene.Raw().emplace<GS::Vertices>(entity);
        vertices.Properties.Resize(25u);
        auto positions = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{GS::PropertyNames::kPosition},
            glm::vec3{0.0f});
        std::size_t index = 0u;
        for (int y = 0; y < 5; ++y)
        {
            for (int x = 0; x < 5; ++x)
            {
                const float noise =
                    ((x * 13 + y * 7) % 5 - 2) * 0.025f;
                positions[index++] = glm::vec3{
                    (static_cast<float>(x) - 2.0f) * 0.2f,
                    (static_cast<float>(y) - 2.0f) * 0.2f,
                    noise,
                };
            }
        }
        return entity;
    }

    [[nodiscard]] ECS::EntityHandle AddMeshPointSetSources(
        ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity = AddNoisyPointCloud(scene);
        const auto positions =
            scene.Raw().get<GS::Vertices>(entity).Properties.Get<glm::vec3>(
                GS::PropertyNames::kPosition);
        auto& faces = scene.Raw().emplace<GS::Faces>(entity);
        faces.Properties.Resize(positions.Vector().size());
        auto centroids = faces.Properties.GetOrAdd<glm::vec3>(
            "f:centroid",
            glm::vec3{0.0f});
        centroids.Vector() = positions.Vector();
        scene.Raw().emplace<GS::Edges>(entity);
        scene.Raw().emplace<GS::Halfedges>(entity);
        scene.Raw().emplace<GS::HasMeshTopology>(entity);
        return entity;
    }

    [[nodiscard]] ECS::EntityHandle AddGraphPointSetSources(
        ECS::Scene::Registry& scene)
    {
        const ECS::EntityHandle entity = AddNoisyPointCloud(scene);
        scene.Raw().emplace<GS::Edges>(entity);
        scene.Raw().emplace<GS::Halfedges>(entity);
        scene.Raw().emplace<GS::HasGraphTopology>(entity);
        return entity;
    }
}

TEST(SandboxPointCloudConsolidationPanel,
     ThreeDomainRegistrationsAreStableOpenAndShareOnePanelPath)
{
    struct ExpectedWindow
    {
        std::string_view Id;
        std::vector<std::string> MenuPath;
        std::string_view WindowTitle;
    };
    const std::array<ExpectedWindow, 3u> expected{{
        {"pointcloud.processing.consolidation",
         {"PointCloud", "Processing"},
         "PointCloud / Processing / Consolidate (LOP/WLOP/CLOP/EAR)"},
        {"graph.processing.consolidation",
         {"Graph", "Processing"},
         "Graph / Processing / Consolidate (LOP/WLOP/CLOP/EAR)"},
        {"mesh.processing.consolidation",
         {"Mesh", "Processing"},
         "Mesh / Processing / Consolidate (LOP/WLOP/CLOP/EAR)"},
    }};

    EditorUiShellHarness harness{};
    Editor::MethodPanels panels{};
    panels.Register(harness.Shell);
    panels.Register(harness.Shell);

    auto menu = harness.Shell.BuildEditorWindowMenuModel();
    for (const ExpectedWindow& expectedWindow : expected)
    {
        const Runtime::EditorWindowMenuEntry* entry =
            FindWindow(menu, expectedWindow.Id);
        ASSERT_NE(entry, nullptr) << expectedWindow.Id;
        EXPECT_EQ(entry->MenuPath, expectedWindow.MenuPath)
            << expectedWindow.Id;
        EXPECT_EQ(entry->Title, "Consolidate (LOP/WLOP/CLOP/EAR)")
            << expectedWindow.Id;
        EXPECT_FALSE(entry->Open) << expectedWindow.Id;
        EXPECT_EQ(
            std::count_if(
                menu.begin(),
                menu.end(),
                [&expectedWindow](
                    const Runtime::EditorWindowMenuEntry& candidate)
                {
                    return candidate.Id == expectedWindow.Id;
                }),
            1)
            << expectedWindow.Id;
        ASSERT_TRUE(
            harness.Shell.SetEditorWindowOpen(expectedWindow.Id, true))
            << expectedWindow.Id;
    }

    harness.Kernel.Run();

    for (const ExpectedWindow& expectedWindow : expected)
    {
        EXPECT_NE(
            ImGui::FindWindowByName(expectedWindow.WindowTitle.data()),
            nullptr)
            << expectedWindow.Id;
    }

    panels.Unregister();
    menu = harness.Shell.BuildEditorWindowMenuModel();
    for (const ExpectedWindow& expectedWindow : expected)
        EXPECT_EQ(FindWindow(menu, expectedWindow.Id), nullptr);
}

TEST(SandboxPointCloudConsolidationPanel,
     StrategiesAndTypedRequestAreExactAndFailClosed)
{
    using Strategy = Runtime::PointCloudConsolidationStrategy;
    constexpr std::array expectedStrategies{
        Strategy::Lop,
        Strategy::Wlop,
        Strategy::Clop,
        Strategy::Ear,
    };
    constexpr std::array<std::string_view, 4u> expectedTokens{
        "lop", "wlop", "clop", "ear"};
    const auto options =
        Editor::SandboxPointCloudConsolidationStrategyOptions();
    for (std::size_t index = 0u; index < options.size(); ++index)
    {
        EXPECT_EQ(options[index].Strategy, expectedStrategies[index]);
        EXPECT_EQ(options[index].StableToken, expectedTokens[index]);
        EXPECT_EQ(
            Runtime::StableToken(options[index].Strategy),
            expectedTokens[index]);
        EXPECT_FALSE(options[index].Label.empty());
        EXPECT_TRUE(options[index].Available);
    }

    Runtime::PointCloudConsolidationConfig config{};
    config.Backend = Runtime::PointCloudConsolidationBackend::VulkanCompute;
    config.Strategy = Strategy::Ear;
    config.SupportRadius = 0.375;
    config.SupportRadiusMode = Runtime::
        PointCloudConsolidationSupportRadiusMode::Manual;
    config.RepulsionWeight = 0.25;
    config.MaxIterations = 18u;
    config.ConvergenceTolerance = 2.0e-5;
    config.TargetPointCount = 73u;
    config.Seed = 0x1234abcdu;
    config.NormalSource = Runtime::PointCloudConsolidationNormalSource::
        RequireAuthored;
    config.NormalAngleRadians = 0.35;
    config.NormalRefinementRounds = 5u;
    config.EarEdgeSensitivity = 8.0;

    Runtime::PointCloudConsolidationPropertyRefs properties =
        Runtime::MakePointCloudConsolidationPropertyRefs(
            Runtime::GeometryElementDomain::MeshFace,
            "f:centroid",
            "f:normal");
    properties.OutputPositions.Name = "lop:centroid";
    properties.OutputNormals->Name = "lop:normal";

    const auto request =
        Editor::BuildSandboxPointCloudConsolidationPanelApplyRequest(
            91u, properties, config);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->Execute.StableEntityId, 91u);
    EXPECT_EQ(
        request->Execute.Properties.InputPositions,
        properties.InputPositions);
    EXPECT_EQ(
        request->Execute.Properties.InputNormals,
        properties.InputNormals);
    EXPECT_EQ(
        request->Execute.Properties.OutputPositions,
        properties.OutputPositions);
    EXPECT_EQ(
        request->Execute.Properties.OutputNormals,
        properties.OutputNormals);
    EXPECT_EQ(
        request->Execute.Properties.InputPositions.Domain,
        Runtime::GeometryElementDomain::MeshFace);
    EXPECT_EQ(
        request->Execute.Properties.InputPositions.Name,
        "f:centroid");
    EXPECT_EQ(
        request->Execute.Properties.OutputPositions.Name,
        "lop:centroid");
    EXPECT_EQ(request->Execute.Config.Strategy, config.Strategy);
    EXPECT_EQ(request->Execute.Config.Backend, config.Backend);
    EXPECT_DOUBLE_EQ(
        request->Execute.Config.SupportRadius,
        config.SupportRadius);
    EXPECT_EQ(request->Execute.Config.SupportRadiusMode,
              Runtime::PointCloudConsolidationSupportRadiusMode::Manual);
    EXPECT_EQ(request->Execute.Config.TargetPointCount, 73u);
    EXPECT_EQ(request->Execute.Config.Seed, 0x1234abcdu);
    EXPECT_EQ(request->Execute.Config.NormalSource, config.NormalSource);
    EXPECT_DOUBLE_EQ(
        request->Execute.Config.EarEdgeSensitivity,
        config.EarEdgeSensitivity);
    EXPECT_EQ(
        request->SourceId,
        "sandbox.point_cloud_consolidation.panel");

    EXPECT_FALSE(
        Editor::BuildSandboxPointCloudConsolidationPanelApplyRequest(
            0u, properties, config)
            .has_value());
    properties.OutputPositions.Domain =
        Runtime::GeometryElementDomain::MeshVertex;
    EXPECT_FALSE(
        Editor::BuildSandboxPointCloudConsolidationPanelApplyRequest(
            91u,
            properties,
            config)
            .has_value());
    properties.OutputPositions.Domain =
        Runtime::GeometryElementDomain::MeshFace;
    config.Strategy = static_cast<Strategy>(999u);
    EXPECT_FALSE(
        Editor::BuildSandboxPointCloudConsolidationPanelApplyRequest(
            91u, properties, config)
            .has_value());
    config.Strategy = Strategy::Wlop;
    config.SupportRadius = 0.0;
    EXPECT_FALSE(
        Editor::BuildSandboxPointCloudConsolidationPanelApplyRequest(
            91u, properties, config)
            .has_value());
}

TEST(SandboxPointCloudConsolidationPanel,
     ResultSummaryProjectsIdentityConvergenceAndFailure)
{
    const Runtime::PointCloudConsolidationResult applied{
        .Status = Runtime::PointCloudConsolidationRunStatus::Applied,
        .RequestedBackend =
            Runtime::PointCloudConsolidationBackend::VulkanCompute,
        .ActualBackend =
            Runtime::PointCloudConsolidationBackend::CpuReference,
        .FellBackToCpu = true,
        .BackendDiagnostic = "Vulkan unavailable; CPU completed.",
        .ImplementationId = "cpu_reference",
        .StrategyToken = "ear",
        .SupportRadiusAnalysisStatus = "success",
        .SupportRadiusSource = "recommended",
        .SupportRadiusQuantile = "p75",
        .SupportRadiusEstimatorVersion = 2u,
        .SupportRadiusProfileSampleCount = 40u,
        .SupportRadiusRequestedNeighborRank = 16u,
        .SupportRadiusNeighborRank = 8u,
        .SupportRadiusWorkloadAdjusted = true,
        .SupportRadiusNeighborDistance = 0.2,
        .ResolvedSupportRadius = 0.25,
        .SupportRadiusBoundingBoxDiagonal = 3.0,
        .SupportNeighborsP50 = 12.0,
        .SupportNeighborsP95 = 18.0,
        .SupportNeighborsMax = 21u,
        .PredictedSupportQueryCount = 2'000u,
        .PredictedContributionCount = 36'000u,
        .InputPointCount = 40u,
        .OutputPointCount = 57u,
        .Iterations = 6u,
        .Converged = true,
        .AverageDisplacement = 0.0125,
        .MaxDisplacement = 0.05,
        .UsedAuthoredNormals = true,
        .NormalRefinementIterations = 4u,
        .InsertedPointCount = 17u,
        .Message = "applied",
    };
    const auto summary =
        Editor::BuildSandboxPointCloudConsolidationResultSummary(applied);
    EXPECT_TRUE(summary.Succeeded);
    EXPECT_FALSE(summary.Queued);
    EXPECT_EQ(summary.Status, "Applied");
    EXPECT_EQ(summary.ImplementationId, "cpu_reference");
    EXPECT_EQ(summary.StrategyToken, "ear");
    EXPECT_EQ(summary.RequestedBackend, "gpu_vulkan_compute");
    EXPECT_EQ(summary.ActualBackend, "cpu_reference");
    EXPECT_TRUE(summary.FellBackToCpu);
    EXPECT_EQ(summary.BackendDiagnostic,
              "Vulkan unavailable; CPU completed.");
    EXPECT_EQ(summary.SupportRadiusAnalysisStatus, "success");
    EXPECT_EQ(summary.SupportRadiusSource, "recommended");
    EXPECT_EQ(summary.SupportRadiusRequestedNeighborRank, 16u);
    EXPECT_EQ(summary.SupportRadiusNeighborRank, 8u);
    EXPECT_TRUE(summary.SupportRadiusWorkloadAdjusted);
    EXPECT_DOUBLE_EQ(summary.ResolvedSupportRadius, 0.25);
    EXPECT_DOUBLE_EQ(summary.SupportNeighborsP95, 18.0);
    EXPECT_EQ(summary.PredictedContributionCount, 36'000u);
    EXPECT_EQ(summary.InputPointCount, 40u);
    EXPECT_EQ(summary.OutputPointCount, 57u);
    EXPECT_EQ(summary.Iterations, 6u);
    EXPECT_TRUE(summary.Converged);
    EXPECT_DOUBLE_EQ(summary.AverageDisplacement, 0.0125);
    EXPECT_DOUBLE_EQ(summary.MaxDisplacement, 0.05);
    EXPECT_TRUE(summary.UsedAuthoredNormals);
    EXPECT_EQ(summary.NormalRefinementIterations, 4u);
    EXPECT_EQ(summary.InsertedPointCount, 17u);

    Runtime::PointCloudConsolidationResult failed{};
    failed.Status =
        Runtime::PointCloudConsolidationRunStatus::StaleSource;
    failed.StrategyToken = "wlop";
    failed.Message = "source changed";
    const auto failure =
        Editor::BuildSandboxPointCloudConsolidationResultSummary(failed);
    EXPECT_FALSE(failure.Succeeded);
    EXPECT_FALSE(failure.Queued);
    EXPECT_EQ(failure.Status, "StaleSource");
    EXPECT_EQ(failure.StrategyToken, "wlop");
    EXPECT_EQ(failure.Message, "source changed");
}

TEST(SandboxPointCloudConsolidationPanel,
     MeshVertexAndFaceSlotsUseTheSameEditorPreflight)
{
    Intrinsic::Tests::RuntimeTestKernel engine{
        HeadlessConfig(), std::make_unique<OneFrameApplication>()};
    engine.EmplaceModule<Runtime::SceneInteractionModule>();
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.Initialize();

    ECS::Scene::Registry* scene =
        engine.Worlds().Get(engine.ActiveWorld());
    ASSERT_NE(scene, nullptr);
    const ECS::EntityHandle entity = AddMeshPointSetSources(*scene);
    Runtime::SelectionController* selection =
        engine.Services().Find<Runtime::SelectionController>();
    ASSERT_NE(selection, nullptr);
    ASSERT_TRUE(selection->SetSelectedEntity(*scene, entity));
    const std::uint32_t stableEntityId =
        Runtime::SelectionController::ToStableEntityId(entity);

    Runtime::EditorWorkspaceAttachment attachment{};
    attachment.Attach(engine.Worlds(), engine.Services());
    const auto workspace =
        Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment);
    ASSERT_TRUE(workspace.has_value());
    const Runtime::EditorInspectorModel inspector =
        Runtime::BuildEditorInspectorModel(workspace->SnapshotQueries);
    ASSERT_TRUE(inspector.HasEntity);
    EXPECT_EQ(inspector.Geometry.Domain, GS::Domain::Mesh);
    EXPECT_TRUE(std::none_of(
        inspector.Diagnostics.begin(),
        inspector.Diagnostics.end(),
        [](const Runtime::EditorDiagnostic& diagnostic)
        {
            return diagnostic.Code ==
                   Runtime::EditorDiagnosticCode::UnsupportedGeometryDomain;
        }));

    const auto hasSlot = [&inspector](
        const Runtime::GeometryElementDomain domain,
        const std::string_view name)
    {
        return std::any_of(
            inspector.PropertyCatalog.Rows.begin(),
            inspector.PropertyCatalog.Rows.end(),
            [domain, name](const Runtime::EditorPropertyCatalogRow& row)
            {
                return row.Bindable && row.Descriptor.Domain == domain &&
                       row.Descriptor.Name == name &&
                       row.Descriptor.ValueKind ==
                           Geometry::PropertyValueKind::Vec3;
            });
    };
    EXPECT_TRUE(hasSlot(
        Runtime::GeometryElementDomain::MeshVertex,
        GS::PropertyNames::kPosition));
    EXPECT_TRUE(hasSlot(
        Runtime::GeometryElementDomain::MeshFace,
        "f:centroid"));

    const Runtime::EditorGeometryProcessingPreparedFrame prepared =
        Runtime::PrepareEditorGeometryProcessingFrame(attachment);
    ASSERT_TRUE(prepared.Commands.IsBound());
    ASSERT_TRUE(prepared.PointCloudConsolidationAvailable);

    Runtime::PointCloudConsolidationConfig config{};
    config.TargetPointCount = 0u;
    using SlotCase = std::tuple<
        Runtime::GeometryElementDomain,
        std::string_view,
        std::string_view>;
    const std::array<SlotCase, 2u> slotCases{{
        SlotCase{
            Runtime::GeometryElementDomain::MeshVertex,
            GS::PropertyNames::kPosition,
            "lop:vertex"},
        SlotCase{
            Runtime::GeometryElementDomain::MeshFace,
            "f:centroid",
            "lop:face"},
    }};
    for (const auto& [domain, inputName, outputName] : slotCases)
    {
        Runtime::PointCloudConsolidationPropertyRefs properties =
            Runtime::MakePointCloudConsolidationPropertyRefs(
                domain,
                std::string{inputName},
                std::nullopt);
        properties.OutputPositions.Name = outputName;
        const auto panelRequest =
            Editor::BuildSandboxPointCloudConsolidationPanelApplyRequest(
                stableEntityId,
                properties,
                config);
        ASSERT_TRUE(panelRequest.has_value());
        const Runtime::PointCloudConsolidationAvailability availability =
            Runtime::ResolveEditorPointCloudConsolidationAvailability(
                prepared.Commands,
                panelRequest->Execute);
        EXPECT_TRUE(availability.Available) << availability.Message;
        EXPECT_EQ(availability.InputPointCount, 25u);
        EXPECT_FALSE(availability.CardinalityChanging);
    }

    attachment.Detach();
    engine.Shutdown();
}

TEST(SandboxPointCloudConsolidationPanel,
     GraphAndPointCloudPositionsUseTheSemanticEditorPreflight)
{
    Intrinsic::Tests::RuntimeTestKernel engine{
        HeadlessConfig(), std::make_unique<OneFrameApplication>()};
    engine.EmplaceModule<Runtime::SceneInteractionModule>();
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.Initialize();

    ECS::Scene::Registry* scene =
        engine.Worlds().Get(engine.ActiveWorld());
    ASSERT_NE(scene, nullptr);
    const ECS::EntityHandle graph = AddGraphPointSetSources(*scene);
    const ECS::EntityHandle pointCloud = AddNoisyPointCloud(*scene);
    Runtime::SelectionController* selection =
        engine.Services().Find<Runtime::SelectionController>();
    ASSERT_NE(selection, nullptr);

    Runtime::EditorWorkspaceAttachment attachment{};
    attachment.Attach(engine.Worlds(), engine.Services());
    using SourceCase = std::tuple<
        ECS::EntityHandle,
        GS::Domain,
        Runtime::GeometryElementDomain>;
    const std::array<SourceCase, 2u> sourceCases{{
        SourceCase{
            graph,
            GS::Domain::Graph,
            Runtime::GeometryElementDomain::GraphNode},
        SourceCase{
            pointCloud,
            GS::Domain::PointCloud,
            Runtime::GeometryElementDomain::PointCloudPoint},
    }};
    for (const auto& [entity, provenance, domain] : sourceCases)
    {
        ASSERT_TRUE(selection->SetSelectedEntity(*scene, entity));
        const std::uint32_t stableEntityId =
            Runtime::SelectionController::ToStableEntityId(entity);
        const auto workspace =
            Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment);
        ASSERT_TRUE(workspace.has_value());
        const Runtime::EditorInspectorModel inspector =
            Runtime::BuildEditorInspectorModel(workspace->SnapshotQueries);
        ASSERT_TRUE(inspector.HasEntity);
        EXPECT_EQ(inspector.Geometry.Domain, provenance);
        EXPECT_TRUE(std::none_of(
            inspector.Diagnostics.begin(),
            inspector.Diagnostics.end(),
            [](const Runtime::EditorDiagnostic& diagnostic)
            {
                return diagnostic.Code ==
                       Runtime::EditorDiagnosticCode::UnsupportedGeometryDomain;
            }));
        EXPECT_TRUE(std::any_of(
            inspector.PropertyCatalog.Rows.begin(),
            inspector.PropertyCatalog.Rows.end(),
            [domain](const Runtime::EditorPropertyCatalogRow& row)
            {
                return row.Bindable && row.Descriptor.Domain == domain &&
                       row.Descriptor.Name == GS::PropertyNames::kPosition &&
                       row.Descriptor.ValueKind ==
                           Geometry::PropertyValueKind::Vec3;
            }));

        const Runtime::EditorGeometryProcessingPreparedFrame prepared =
            Runtime::PrepareEditorGeometryProcessingFrame(attachment);
        Runtime::PointCloudConsolidationPropertyRefs properties =
            Runtime::MakePointCloudConsolidationPropertyRefs(
                domain,
                std::string{GS::PropertyNames::kPosition},
                std::nullopt);
        properties.OutputPositions.Name = "lop:point";
        const auto request =
            Editor::BuildSandboxPointCloudConsolidationPanelApplyRequest(
                stableEntityId,
                properties,
                Runtime::PointCloudConsolidationConfig{});
        ASSERT_TRUE(request.has_value());
        const Runtime::PointCloudConsolidationAvailability availability =
            Runtime::ResolveEditorPointCloudConsolidationAvailability(
                prepared.Commands,
                request->Execute);
        EXPECT_TRUE(availability.Available) << availability.Message;
        EXPECT_EQ(availability.InputPointCount, 25u);
    }

    attachment.Detach();
    engine.Shutdown();
}

TEST(SandboxPointCloudConsolidationPanel,
     ValidatedActionQueuesAndPublishesThroughPreparedWorkspace)
{
    auto configControl = std::make_unique<Runtime::EngineConfigControl>(
        Sandbox::CreateSandboxConfigSectionRegistry());
    Config::EngineConfig config = Runtime::CreateReferenceEngineConfig(
        configControl->SectionRegistry());
    config.Simulation.WorkerThreadCount = 2u;
    config.ReferenceScene.Enabled = false;
    config.Camera.Enabled = false;
    config.Window.Backend = Config::WindowBackend::Null;
    config.Render.EnablePromotedVulkanDevice = false;
    config.Render.DefaultRecipeConfigPath.clear();

    auto app = std::make_unique<WaitForConsolidationApplication>();
    WaitForConsolidationApplication* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine{
        std::move(config), std::move(app)};
    engine.AddModule(std::move(configControl));
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::SceneInteractionModule>();
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.Initialize();

    ECS::Scene::Registry* scene = engine.Worlds().Get(engine.ActiveWorld());
    ASSERT_NE(scene, nullptr);
    const ECS::EntityHandle entity = AddNoisyPointCloud(*scene);
    Runtime::SelectionController* selection =
        engine.Services().Find<Runtime::SelectionController>();
    ASSERT_NE(selection, nullptr);
    ASSERT_TRUE(selection->SetSelectedEntity(*scene, entity));
    const std::uint32_t stableEntityId =
        Runtime::SelectionController::ToStableEntityId(entity);

    Runtime::EditorWorkspaceAttachment attachment{};
    attachment.Attach(engine.Worlds(), engine.Services());
    ASSERT_TRUE(
        Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment).has_value());
    const Runtime::EditorGeometryProcessingPreparedFrame prepared =
        Runtime::PrepareEditorGeometryProcessingFrame(attachment);
    ASSERT_TRUE(prepared.Commands.IsBound());
    ASSERT_TRUE(prepared.ConfigCommandsAvailable);
    ASSERT_TRUE(prepared.PointCloudConsolidationAvailable);

    Editor::SandboxEditorContext context{};
    context.GeometryCommands = prepared.Commands;
    context.GeometryConfigCommandsAvailable =
        prepared.ConfigCommandsAvailable;
    context.PointCloudConsolidationAvailable =
        prepared.PointCloudConsolidationAvailable;

    Runtime::PointCloudConsolidationConfig requested{};
    requested.Backend =
        Runtime::PointCloudConsolidationBackend::VulkanCompute;
    requested.Strategy = Runtime::PointCloudConsolidationStrategy::Wlop;
    requested.SupportRadius = 0.65;
    requested.RepulsionWeight = 0.0;
    requested.MaxIterations = 1u;
    requested.ConvergenceTolerance = 1.0;
    requested.TargetPointCount = 16u;
    requested.Seed = 17u;
    requested.NormalRefinementRounds = 1u;
    const Runtime::PointCloudConsolidationPropertyRefs properties =
        Runtime::MakePointCloudConsolidationPropertyRefs(
            Runtime::GeometryElementDomain::PointCloudPoint,
            std::string{GS::PropertyNames::kPosition},
            std::nullopt);
    const Editor::SandboxPointCloudConsolidationPanelActionResult action =
        Editor::ApplySandboxPointCloudConsolidationPanelAction(
            context,
            stableEntityId,
            properties,
            requested);
    ASSERT_TRUE(action.Succeeded());
    ASSERT_TRUE(action.Submission.has_value());
    EXPECT_EQ(
        action.Submission->Status,
        Runtime::PointCloudConsolidationRunStatus::Queued);
    EXPECT_NE(
        action.Submission->Message.find("support-radius profiling"),
        std::string::npos);
    EXPECT_NE(
        action.Submission->Message.find("Vulkan"),
        std::string::npos);
    EXPECT_EQ(
        action.Config.Source,
        Runtime::RuntimeConfigControlSource::Editor);
    EXPECT_TRUE(action.Config.SectionChanged(
        Runtime::kPointCloudConsolidationConfigSectionName));

    engine.Run();
    ASSERT_FALSE(appPtr->TimedOut);
    ASSERT_TRUE(appPtr->Completion.has_value());
    ASSERT_TRUE(appPtr->Completion->Succeeded())
        << appPtr->Completion->Message;
    EXPECT_EQ(appPtr->Completion->OutputPointCount, 16u);
    EXPECT_EQ(appPtr->Completion->RequestedBackend,
              Runtime::PointCloudConsolidationBackend::VulkanCompute);
    EXPECT_EQ(appPtr->Completion->ActualBackend,
              Runtime::PointCloudConsolidationBackend::CpuReference);
    EXPECT_TRUE(appPtr->Completion->FellBackToCpu);
    EXPECT_EQ(appPtr->Completion->SupportRadiusSource, "recommended");
    EXPECT_EQ(appPtr->Completion->SupportRadiusAnalysisStatus, "success");
    EXPECT_GT(appPtr->Completion->ResolvedSupportRadius, 0.0);
    EXPECT_GT(appPtr->Completion->PredictedContributionCount, 0u);
    EXPECT_EQ(
        scene->Raw().get<GS::Vertices>(entity).Properties.Size(),
        16u);

    ASSERT_TRUE(
        Runtime::PrepareEditorWorkspaceSnapshotFrame(attachment).has_value());
    const Runtime::EditorGeometryProcessingPreparedFrame completed =
        Runtime::PrepareEditorGeometryProcessingFrame(attachment);
    ASSERT_TRUE(
        completed.Results.LastPointCloudConsolidationResult.has_value());
    EXPECT_TRUE(
        completed.Results.LastPointCloudConsolidationResult->Succeeded());
    EXPECT_EQ(
        completed.Results.LastPointCloudConsolidationResult
            ->ImplementationId,
        "cpu_reference");
    EXPECT_EQ(
        completed.Results.LastPointCloudConsolidationResult
            ->SupportRadiusSource,
        "recommended");

    Runtime::EditorCommandHistory* history =
        engine.Services().Find<Runtime::EditorCommandHistory>();
    ASSERT_NE(history, nullptr);
    ASSERT_EQ(history->UndoCount(), 1u);
    EXPECT_EQ(
        history->Undo().Status,
        Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(
        scene->Raw().get<GS::Vertices>(entity).Properties.Size(),
        25u);

    attachment.Detach();
    engine.Shutdown();
}
