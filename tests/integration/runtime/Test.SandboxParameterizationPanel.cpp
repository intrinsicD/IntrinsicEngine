#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.SandboxEditorFacades;
import Extrinsic.Runtime.SandboxConfigSections;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Sandbox.ConfigSections;
import Extrinsic.Sandbox.Editor.MethodPanels;
import Extrinsic.Sandbox.Editor.Shell;
import Geometry.HalfedgeMesh;
import Geometry.Parameterization;
import Geometry.Properties;

namespace Config = Extrinsic::Core::Config;
namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Runtime = Extrinsic::Runtime;
namespace Sandbox = Extrinsic::Sandbox;
namespace SandboxEditor = Extrinsic::Sandbox::Editor;

namespace
{
    class OneFrameApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override {}
        void Simulate(double) override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            engine.RequestExit();
        }
        void Shutdown() override {}
    };

    [[nodiscard]] Config::EngineConfig HeadlessConfig()
    {
        Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Config::WindowBackend::Null;
        return config;
    }

    struct EditorUiShellHarness
    {
        Intrinsic::Tests::RuntimeTestKernel Kernel{HeadlessConfig(),
                                                   std::make_unique<OneFrameApplication>()};
        SandboxEditor::EditorShell Shell{};

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

    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeGridMesh()
    {
        Geometry::HalfedgeMesh::Mesh mesh{};
        std::array<Geometry::VertexHandle, 9u> vertices{};
        for (std::uint32_t y = 0u; y < 3u; ++y)
        {
            for (std::uint32_t x = 0u; x < 3u; ++x)
            {
                vertices[y * 3u + x] = mesh.AddVertex(glm::vec3{
                    static_cast<float>(x),
                    static_cast<float>(y),
                    0.1f * static_cast<float>(x * y),
                });
            }
        }
        const auto at = [&vertices](const std::uint32_t x,
                                    const std::uint32_t y)
        {
            return vertices[y * 3u + x];
        };
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            for (std::uint32_t x = 0u; x < 2u; ++x)
            {
                EXPECT_TRUE(mesh.AddTriangle(
                    at(x, y), at(x + 1u, y), at(x + 1u, y + 1u)));
                EXPECT_TRUE(mesh.AddTriangle(
                    at(x, y), at(x + 1u, y + 1u), at(x, y + 1u)));
            }
        }
        return mesh;
    }

    struct ParameterizationPanelHarness
    {
        ECS::Scene::Registry Scene{};
        Runtime::SelectionController Selection{};
        Runtime::EditorCommandHistory History{};
        Runtime::SandboxEditorSelectedModelCache ModelCache{};
        std::unique_ptr<Intrinsic::Tests::RuntimeTestKernel> ConfigEngine{};
        Runtime::EngineConfigControl* ConfigControl{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        Runtime::SandboxEditorContext Context{};

        ParameterizationPanelHarness()
        {
            auto configControl =
                std::make_unique<Runtime::EngineConfigControl>(
                    Sandbox::CreateSandboxConfigSectionRegistry());
            Config::EngineConfig config =
                Runtime::CreateReferenceEngineConfig(
                    configControl->SectionRegistry());
            config.Simulation.WorkerThreadCount = 1u;
            config.ReferenceScene.Enabled = false;
            config.Camera.Enabled = false;
            config.Window.Backend = Config::WindowBackend::Null;
            config.Render.EnablePromotedVulkanDevice = false;
            config.Render.DefaultRecipeConfigPath.clear();
            ConfigEngine = std::make_unique<Intrinsic::Tests::RuntimeTestKernel>(
                std::move(config), std::make_unique<OneFrameApplication>());
            ConfigEngine->AddModule(std::move(configControl));
            ConfigEngine->Initialize();
            ConfigControl =
                ConfigEngine->Services()
                    .Find<Runtime::EngineConfigControl>();
            EXPECT_NE(ConfigControl, nullptr);

            Entity = Scene.Create();
            Geometry::HalfedgeMesh::Mesh mesh = MakeGridMesh();
            GS::PopulateFromMesh(Scene.Raw(), Entity, mesh);
            EXPECT_TRUE(Selection.SetSelectedEntity(Scene, Entity));
            StableEntityId =
                Runtime::SelectionController::ToStableEntityId(Entity);
            Context.Scene = &Scene;
            Context.Selection = &Selection;
            Context.CommandHistory = &History;
            Context.SelectedModelCache = &ModelCache;
            Context.EngineConfigControlState =
                ConfigControl != nullptr
                    ? &ConfigControl->GetEngineConfigControlState()
                    : nullptr;
            Context.EngineConfigCommandsAvailable =
                ConfigControl != nullptr;
            Context.PreviewEngineConfigDocument =
                [this](const std::string& document,
                       const std::string& sourceId)
                {
                    return ConfigControl->PreviewEngineConfigControlDocument(
                        document,
                        sourceId);
                };
            Context.ApplyEngineConfigHotSubset =
                [this](const Config::EngineConfigLoadResult& preview)
                {
                    return ConfigControl->ApplyEngineConfigHotSubset(
                        preview,
                        Runtime::RuntimeConfigControlSource::Editor);
                };
        }

        ~ParameterizationPanelHarness()
        {
            if (ConfigEngine)
            {
                ConfigEngine->Shutdown();
            }
        }

        [[nodiscard]] bool HasFiniteUvs() const
        {
            const auto& vertices = Scene.Raw().get<GS::Vertices>(Entity);
            const auto uvs = vertices.Properties.Get<glm::vec2>("v:texcoord");
            if (!uvs || uvs.Vector().size() != 9u)
                return false;
            for (const glm::vec2 uv : uvs.Vector())
            {
                if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
                    return false;
            }
            return true;
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
}

TEST(SandboxParameterizationPanel, RegistrationIsStableAndIdempotent)
{
    EditorUiShellHarness harness;
    SandboxEditor::MethodPanels panels;
    panels.Register(harness.Shell);
    panels.Register(harness.Shell);

    auto menu = harness.Shell.BuildEditorWindowMenuModel();
    const Runtime::EditorWindowMenuEntry* entry =
        FindWindow(menu, "mesh.processing.parameterize_uv");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->MenuPath,
              (std::vector<std::string>{"Mesh", "Processing"}));
    EXPECT_EQ(entry->Title, "Parameterize (UV)");
    EXPECT_FALSE(entry->Open);
    EXPECT_EQ(
        std::count_if(
            menu.begin(),
            menu.end(),
            [](const Runtime::EditorWindowMenuEntry& candidate)
            {
                return candidate.Id == "mesh.processing.parameterize_uv";
            }),
        1);

    panels.Unregister();
    menu = harness.Shell.BuildEditorWindowMenuModel();
    EXPECT_EQ(FindWindow(menu, "mesh.processing.parameterize_uv"), nullptr);
}

TEST(SandboxParameterizationPanel, StrategiesAndTypedRequestAreExact)
{
    using Strategy = Runtime::ParameterizationStrategyKind;
    constexpr std::array expectedStrategies{
        Strategy::Lscm,
        Strategy::HarmonicCotangent,
        Strategy::TutteUniform,
        Strategy::Bff,
    };
    constexpr std::array<std::string_view, 4u> expectedTokens{
        "lscm", "harmonic_cotangent", "tutte_uniform", "bff"};
    const auto options = SandboxEditor::SandboxParameterizationStrategyOptions();
    static_assert(options.size() == 4u);
    for (std::size_t index = 0u; index < options.size(); ++index)
    {
        EXPECT_EQ(options[index].Strategy, expectedStrategies[index]);
        EXPECT_EQ(options[index].StableToken, expectedTokens[index]);
        EXPECT_FALSE(options[index].Label.empty());
        EXPECT_EQ(
            Runtime::StableTokenForSandboxEditorParameterizationStrategy(
                options[index].Strategy),
            expectedTokens[index]);
    }

    Runtime::ParameterizationConfig config{};
    config.Strategy = Strategy::Bff;
    config.Lscm.AutoPins = false;
    config.Lscm.PinVertex0 = 2u;
    config.Lscm.PinVertex1 = 7u;
    config.Lscm.PinUv0 = {0.25, 0.5};
    config.Lscm.PinUv1 = {0.75, 0.125};
    config.Lscm.SolverTolerance = 2.5e-7;
    config.Lscm.MaxSolverIterations = 1234u;
    config.Harmonic.Boundary =
        Runtime::ParameterizationBoundaryPolicy::Custom;
    config.Harmonic.ArcLengthSpacing = false;
    config.Harmonic.ClampNonConvexWeights = false;
    config.Harmonic.PinnedVertices = {1u, 5u};
    config.Harmonic.PinnedUvs = {{0.1, 0.2}, {0.8, 0.9}};
    config.Bff.Mode =
        Runtime::ParameterizationBffBoundaryMode::TargetLengths;
    config.Bff.BoundaryData = {1.0, 2.0, 3.0};
    config.Bff.AngleSumTolerance = 3.0e-8;
    config.Bff.DegeneracyTolerance = 4.0e-11;
    config.View.RenderMode = Runtime::ParameterizationUvRenderMode::GpuShaded;
    config.View.BackgroundMode =
        Runtime::ParameterizationUvBackgroundMode::Texture;
    config.View.ShowDistortionHeatmap = true;

    const auto request =
        SandboxEditor::BuildSandboxParameterizationPanelApplyRequest(
            91u,
            config);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->Execute.StableEntityId, 91u);
    EXPECT_EQ(request->Config.SourceId, "sandbox.parameterization.panel");
    const auto& copied = request->Config.Config;
    EXPECT_EQ(copied.Strategy, config.Strategy);
    EXPECT_EQ(copied.Lscm.AutoPins, config.Lscm.AutoPins);
    EXPECT_EQ(copied.Lscm.PinVertex0, config.Lscm.PinVertex0);
    EXPECT_EQ(copied.Lscm.PinVertex1, config.Lscm.PinVertex1);
    EXPECT_DOUBLE_EQ(copied.Lscm.PinUv0.U, config.Lscm.PinUv0.U);
    EXPECT_DOUBLE_EQ(copied.Lscm.PinUv0.V, config.Lscm.PinUv0.V);
    EXPECT_DOUBLE_EQ(copied.Lscm.PinUv1.U, config.Lscm.PinUv1.U);
    EXPECT_DOUBLE_EQ(copied.Lscm.PinUv1.V, config.Lscm.PinUv1.V);
    EXPECT_DOUBLE_EQ(copied.Lscm.SolverTolerance,
                     config.Lscm.SolverTolerance);
    EXPECT_EQ(copied.Lscm.MaxSolverIterations,
              config.Lscm.MaxSolverIterations);
    EXPECT_EQ(copied.Harmonic.Boundary, config.Harmonic.Boundary);
    EXPECT_EQ(copied.Harmonic.ArcLengthSpacing,
              config.Harmonic.ArcLengthSpacing);
    EXPECT_EQ(copied.Harmonic.ClampNonConvexWeights,
              config.Harmonic.ClampNonConvexWeights);
    EXPECT_EQ(copied.Harmonic.PinnedVertices,
              config.Harmonic.PinnedVertices);
    ASSERT_EQ(copied.Harmonic.PinnedUvs.size(), 2u);
    EXPECT_DOUBLE_EQ(copied.Harmonic.PinnedUvs[1].U, 0.8);
    EXPECT_DOUBLE_EQ(copied.Harmonic.PinnedUvs[1].V, 0.9);
    EXPECT_EQ(copied.Bff.Mode, config.Bff.Mode);
    EXPECT_EQ(copied.Bff.BoundaryData, config.Bff.BoundaryData);
    EXPECT_DOUBLE_EQ(copied.Bff.AngleSumTolerance,
                     config.Bff.AngleSumTolerance);
    EXPECT_DOUBLE_EQ(copied.Bff.DegeneracyTolerance,
                     config.Bff.DegeneracyTolerance);
    EXPECT_EQ(copied.View.RenderMode, config.View.RenderMode);
    EXPECT_EQ(copied.View.BackgroundMode, config.View.BackgroundMode);
    EXPECT_EQ(copied.View.ShowDistortionHeatmap,
              config.View.ShowDistortionHeatmap);

    EXPECT_FALSE(
        SandboxEditor::BuildSandboxParameterizationPanelApplyRequest(
            0u,
            config)
            .has_value());
    config.Strategy = static_cast<Strategy>(999u);
    EXPECT_FALSE(
        SandboxEditor::BuildSandboxParameterizationPanelApplyRequest(
            91u,
            config)
            .has_value());
}

TEST(SandboxParameterizationPanel, ProjectionFitsAndFailsClosed)
{
    Runtime::SandboxEditorParameterizationViewModel model{};
    model.HasUvCoordinates = true;
    model.HasFiniteUvBounds = true;
    model.UVs = {
        {-2.0f, -1.0f},
        {2.0f, -1.0f},
        {2.0f, 1.0f},
        {-2.0f, 1.0f},
    };
    model.UvBoundsMin = {-2.0f, -1.0f};
    model.UvBoundsMax = {2.0f, 1.0f};
    model.Triangles = {{0u, 1u, 2u}, {0u, 2u, 3u}};
    const SandboxEditor::SandboxParameterizationUvPane pane{
        .Min = {10.0f, 20.0f},
        .Max = {410.0f, 220.0f},
        .Padding = 10.0f,
    };
    const auto projection =
        SandboxEditor::BuildSandboxParameterizationUvProjection(model, pane);
    ASSERT_TRUE(projection.Valid) << projection.Message;
    EXPECT_TRUE(projection.FitsPane);
    EXPECT_EQ(projection.Vertices.size(), model.UVs.size());
    EXPECT_EQ(projection.Triangles, model.Triangles);
    const float horizontalScale =
        std::abs(projection.Vertices[1].x - projection.Vertices[0].x) / 4.0f;
    const float verticalScale =
        std::abs(projection.Vertices[2].y - projection.Vertices[1].y) / 2.0f;
    EXPECT_NEAR(horizontalScale, verticalScale, 1.0e-5f);
    EXPECT_GT(projection.Vertices[0].y, projection.Vertices[3].y);

    auto compactIsland = model;
    compactIsland.UVs = {
        {0.45f, 0.45f},
        {0.55f, 0.45f},
        {0.55f, 0.55f},
        {0.45f, 0.55f},
    };
    compactIsland.UvBoundsMin = {0.45f, 0.45f};
    compactIsland.UvBoundsMax = {0.55f, 0.55f};
    const auto compactProjection =
        SandboxEditor::BuildSandboxParameterizationUvProjection(
            compactIsland,
            SandboxEditor::SandboxParameterizationUvPane{
                .Min = pane.Min,
                .Max = pane.Max,
                .Padding = pane.Padding,
                .IncludeUnitSquare = true,
            });
    ASSERT_TRUE(compactProjection.Valid) << compactProjection.Message;
    const glm::vec2 unitMin =
        SandboxEditor::ProjectSandboxParameterizationUvPoint(
            compactProjection,
            {0.0f, 0.0f});
    const glm::vec2 unitMax =
        SandboxEditor::ProjectSandboxParameterizationUvPoint(
            compactProjection,
            {1.0f, 1.0f});
    EXPECT_GE(unitMin.x, pane.Min.x + pane.Padding - 0.5f);
    EXPECT_LE(unitMin.x, pane.Max.x - pane.Padding + 0.5f);
    EXPECT_GE(unitMin.y, pane.Min.y + pane.Padding - 0.5f);
    EXPECT_LE(unitMin.y, pane.Max.y - pane.Padding + 0.5f);
    EXPECT_GE(unitMax.x, pane.Min.x + pane.Padding - 0.5f);
    EXPECT_LE(unitMax.x, pane.Max.x - pane.Padding + 0.5f);
    EXPECT_GE(unitMax.y, pane.Min.y + pane.Padding - 0.5f);
    EXPECT_LE(unitMax.y, pane.Max.y - pane.Padding + 0.5f);

    auto transformedPane = pane;
    transformedPane.Zoom = 2.0f;
    transformedPane.Pan = {17.0f, -9.0f};
    const auto transformed =
        SandboxEditor::BuildSandboxParameterizationUvProjection(
            model,
            transformedPane);
    ASSERT_TRUE(transformed.Valid);
    EXPECT_FALSE(transformed.FitsPane);
    EXPECT_NE(transformed.Vertices.front().x, projection.Vertices.front().x);
    EXPECT_NE(transformed.Vertices.front().y, projection.Vertices.front().y);

    auto degenerate = model;
    degenerate.UVs = {{3.0f, 4.0f}, {3.0f, 4.0f}, {3.0f, 4.0f}};
    degenerate.UvBoundsMin = {3.0f, 4.0f};
    degenerate.UvBoundsMax = {3.0f, 4.0f};
    degenerate.Triangles = {{0u, 1u, 2u}};
    EXPECT_TRUE(
        SandboxEditor::BuildSandboxParameterizationUvProjection(
            degenerate,
            pane)
            .Valid);

    auto invalid = model;
    invalid.Triangles = {{0u, 1u, 99u}};
    const auto rejected =
        SandboxEditor::BuildSandboxParameterizationUvProjection(invalid, pane);
    EXPECT_FALSE(rejected.Valid);
    EXPECT_FALSE(rejected.Message.empty());

    auto extreme = model;
    const float huge = std::numeric_limits<float>::max() * 0.5f;
    extreme.UVs = {
        {-huge, -huge},
        {huge, -huge},
        {huge, huge},
        {-huge, huge},
    };
    extreme.UvBoundsMin = {-huge, -huge};
    extreme.UvBoundsMax = {huge, huge};
    auto extremePane = pane;
    extremePane.Zoom = std::numeric_limits<float>::max();
    const auto overflowRejected =
        SandboxEditor::BuildSandboxParameterizationUvProjection(
            extreme,
            extremePane);
    EXPECT_FALSE(overflowRejected.Valid);
    EXPECT_FALSE(overflowRejected.Message.empty());
}

TEST(SandboxParameterizationPanel, ResultSummaryCarriesAggregateDiagnostics)
{
    Runtime::SandboxEditorParameterizationResult result{};
    result.Status = Runtime::SandboxEditorCommandStatus::Applied;
    result.Strategy = Runtime::ParameterizationStrategyKind::TutteUniform;
    result.StrategyToken = "tutte_uniform";
    result.ParameterizationStatus =
        Geometry::Parameterization::ParameterizationStatus::Success;
    result.Message = "Mesh parameterization applied.";
    result.Diagnostics.VertexStorageCount = 8u;
    result.Diagnostics.EvaluatedFaceCount = 6u;
    result.Diagnostics.SkippedFaceCount = 2u;
    result.Diagnostics.FlippedElementCount = 1u;
    result.Diagnostics.BoundaryEdgeCount = 7u;
    result.Diagnostics.MeanConformalDistortion = 1.25;
    result.Diagnostics.MeanAreaDistortion = 0.125;
    result.Diagnostics.MeanStretch = 1.5;

    const auto summary =
        SandboxEditor::BuildSandboxParameterizationResultSummary(result);
    EXPECT_TRUE(summary.Succeeded);
    EXPECT_TRUE(summary.HasDiagnostics);
    EXPECT_EQ(summary.StrategyToken, "tutte_uniform");
    EXPECT_FALSE(summary.CommandStatus.empty());
    EXPECT_EQ(summary.Message, result.Message);
    EXPECT_EQ(summary.EvaluatedFaceCount, 6u);
    EXPECT_EQ(summary.SkippedFaceCount, 2u);
    EXPECT_EQ(summary.FlippedElementCount, 1u);
    EXPECT_EQ(summary.BoundaryEdgeCount, 7u);
    EXPECT_DOUBLE_EQ(summary.MeanConformalDistortion, 1.25);
    EXPECT_DOUBLE_EQ(summary.MeanAreaDistortion, 0.125);
    EXPECT_DOUBLE_EQ(summary.MeanStretch, 1.5);

    result.Status = Runtime::SandboxEditorCommandStatus::StaleEntity;
    result.ParameterizationStatus =
        Geometry::Parameterization::ParameterizationStatus::InvalidInput;
    const auto invalidSummary =
        SandboxEditor::BuildSandboxParameterizationResultSummary(result);
    EXPECT_FALSE(invalidSummary.Succeeded);
    EXPECT_EQ(invalidSummary.SolverStatus, "invalid input");
    EXPECT_NE(invalidSummary.CommandStatus, summary.CommandStatus);
}

TEST(SandboxParameterizationPanel, RealWindowAndTypedActionAreOperational)
{
    ParameterizationPanelHarness harness{};
    Runtime::ParameterizationConfig config{};
    config.Strategy = Runtime::ParameterizationStrategyKind::TutteUniform;
    const SandboxEditor::SandboxParameterizationPanelActionResult action =
        SandboxEditor::ApplySandboxParameterizationPanelAction(
            harness.Context,
            harness.StableEntityId,
            config);
    ASSERT_TRUE(action.Succeeded());
    ASSERT_TRUE(action.Execution.has_value());
    EXPECT_EQ(action.Execution->Strategy,
              Runtime::ParameterizationStrategyKind::TutteUniform);
    EXPECT_TRUE(harness.HasFiniteUvs());
    EXPECT_TRUE(harness.History.CanUndo());

    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    engine.EmplaceModule<Runtime::EditorUiModule>();
    engine.Initialize();
    SandboxEditor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    SandboxEditor::MethodPanels panels;
    panels.Register(shell);
    ASSERT_TRUE(shell.SetEditorWindowOpen(
        "mesh.processing.parameterize_uv",
        true));

    engine.Run();

    EXPECT_NE(
        ImGui::FindWindowByName(
            "Mesh / Processing / Parameterize (UV)"),
        nullptr);
    const auto& imguiDiagnostics =
        engine.Services()
            .Find<Runtime::EditorUiHost>()
            ->GetDiagnostics();
    EXPECT_GE(imguiDiagnostics.FramesProduced, 1u);
    EXPECT_FALSE(imguiDiagnostics.LastFrameUsedUserTexture)
        << "Configured CpuLayout must use the panel ImDrawList path without a "
           "user texture.";
    EXPECT_GT(imguiDiagnostics.LastVertexCount, 0u);
    EXPECT_GT(imguiDiagnostics.LastIndexCount, 0u);
    panels.Unregister();
    shell.Detach();
    engine.Shutdown();
}
