#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.Engine;
import Extrinsic.Sandbox.Editor.MeshProcessingPanels;
import Extrinsic.Sandbox.Editor.Shell;

namespace Config = Extrinsic::Core::Config;
namespace Runtime = Extrinsic::Runtime;
namespace Editor = Extrinsic::Sandbox::Editor;

namespace
{
    class OneFrameApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override {}
        void Frame(double, double) override
        {
            Kernel().RequestExit();
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
        config.Render.EnablePromotedVulkanDevice = false;
        config.Render.DefaultRecipeConfigPath.clear();
        return config;
    }

    [[nodiscard]] std::string ReadPanelSource()
    {
        const std::filesystem::path path =
            std::filesystem::path{ENGINE_ROOT_DIR} /
            "src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp";
        std::ifstream input{path, std::ios::binary};
        EXPECT_TRUE(input.is_open()) << path;
        std::ostringstream contents{};
        contents << input.rdbuf();
        return contents.str();
    }

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

TEST(SandboxCurvatureSegmentationPanel,
     RegistersInsideTheExistingCurvatureWindow)
{
    Intrinsic::Tests::RuntimeTestKernel engine{
        HeadlessConfig(), std::make_unique<OneFrameApplication>()};
    engine.EmplaceModule<Runtime::EditorUiModule>();
    engine.Initialize();

    Editor::EditorShell shell{};
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MeshProcessingPanels panels{};
    panels.Register(shell);
    panels.Register(shell);

    const auto menu = shell.BuildEditorWindowMenuModel();
    const Runtime::EditorWindowMenuEntry* entry =
        FindWindow(menu, "mesh.processing.curvature");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->MenuPath,
              (std::vector<std::string>{"Mesh", "Processing"}));
    EXPECT_EQ(entry->Title, "Curvature");
    EXPECT_EQ(
        std::count_if(
            menu.begin(),
            menu.end(),
            [](const Runtime::EditorWindowMenuEntry& candidate)
            {
                return candidate.Id == "mesh.processing.curvature";
            }),
        1);
    ASSERT_TRUE(shell.SetEditorWindowOpen(
        "mesh.processing.curvature", true));

    engine.Run();

    EXPECT_NE(ImGui::FindWindowByName(
                  "Mesh / Processing / Curvature"),
              nullptr);
    const Runtime::EditorUiHost* host =
        engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(host, nullptr);
    EXPECT_GT(host->GetDiagnostics().LastVertexCount, 0u);
    EXPECT_GT(host->GetDiagnostics().LastIndexCount, 0u);

    panels.Unregister();
    shell.Detach();
    engine.Shutdown();
}

TEST(SandboxCurvatureSegmentationPanel,
     UsesOneValidatedConfiguredRunAndSimultaneousFaceEdgeVisualization)
{
    const std::string source = ReadPanelSource();
    EXPECT_NE(source.find(
                  "CurvatureSegmentationSelectionMode::FixedCount"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "CurvatureSegmentationSelectionMode::Automatic"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "CurvatureSegmentationMethod::FeatureAlignedPatches"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "ApplyEditorCurvatureSegmentationConfig"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "ApplyEditorConfiguredCurvatureSegmentationCommand"),
              std::string::npos);
    EXPECT_NE(source.find("LastSegmentationStableEntityId"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "*Curvature.LastSegmentationStableEntityId"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "sandbox.curvature_segmentation.panel.run"),
              std::string::npos);

    const std::size_t visualizationBegin = source.find(
        "void ShowCurvatureSegmentationVisualization");
    const std::size_t visualizationEnd = source.find(
        "struct MeshProcessingPanels::Impl", visualizationBegin);
    ASSERT_NE(visualizationBegin, std::string::npos);
    ASSERT_NE(visualizationEnd, std::string::npos);
    const std::string_view visualization{
        source.data() + visualizationBegin,
        visualizationEnd - visualizationBegin};
    EXPECT_NE(visualization.find(".SetSurface = true"),
              std::string_view::npos);
    EXPECT_NE(visualization.find(".EnableSurface = true"),
              std::string_view::npos);
    EXPECT_NE(visualization.find(".SetEdges = true"),
              std::string_view::npos);
    EXPECT_NE(visualization.find(".EnableEdges = true"),
              std::string_view::npos);
    EXPECT_NE(visualization.find(
                  "EditorVisualizationTarget::Surface"),
              std::string_view::npos);
    EXPECT_NE(visualization.find(
                  "EditorVisualizationPropertyDomain::MeshFaces"),
              std::string_view::npos);
    EXPECT_NE(visualization.find(
                  "kCurvatureRegionColorProperty"),
              std::string_view::npos);
    EXPECT_NE(visualization.find(
                  "EditorVisualizationTarget::Edges"),
              std::string_view::npos);
    EXPECT_NE(visualization.find(
                  "EditorVisualizationPropertyDomain::MeshEdges"),
              std::string_view::npos);
    EXPECT_NE(visualization.find(
                  "kCurvatureFeaturePatchColorProperty"),
              std::string_view::npos);
    EXPECT_NE(source.find("f:curvature_region_color"),
              std::string::npos);
    EXPECT_NE(source.find("e:curvature_feature_patch_color"),
              std::string::npos);

    const std::size_t scalarBegin = source.find(
        "ShowCurvatureScalarVisualization(");
    ASSERT_NE(scalarBegin, std::string::npos);
    EXPECT_NE(source.find(
                  "EditorVisualizationPropertyDomain::MeshVertices",
                  scalarBegin),
              std::string::npos);
    EXPECT_NE(source.find("v:mean_curvature"), std::string::npos);
    EXPECT_NE(source.find("v:gaussian_curvature"), std::string::npos);
    EXPECT_NE(source.find("v:min_principal_curvature"),
              std::string::npos);
    EXPECT_NE(source.find("v:max_principal_curvature"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "PendingCurvatureVisualizationStableEntityId"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "EditorCommandStatus::Pending",
                  scalarBegin),
              std::string::npos);
    EXPECT_NE(source.find(
                  "Scalar visualization:",
                  scalarBegin),
              std::string::npos);
}
