#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>
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
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.Engine;
import Extrinsic.Sandbox.Editor.MeshProcessingPanels;
import Extrinsic.Sandbox.Editor.Shell;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.PointCloudConsolidationTypes;

#include "../../../src/app/Sandbox/Editor/Sandbox.PanelSupport.hpp"

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
     RegistersSeparateCurvatureAndSegmentationWindows)
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
    const auto* segmentation = FindWindow(menu, "mesh.processing.segmentation");
    ASSERT_NE(segmentation, nullptr);
    EXPECT_EQ(segmentation->Title, "Curvature Segmentation");
    EXPECT_EQ(segmentation->MenuPath, (std::vector<std::string>{"Mesh", "Processing"}));
    ASSERT_TRUE(shell.SetEditorWindowOpen("mesh.processing.segmentation", true));
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
    EXPECT_NE(source.find("CurvatureSegmentationMethod::FeatureBoundaryCurves"),
              std::string::npos);
    EXPECT_NE(source.find("Experimental curves_v1"), std::string::npos);
    EXPECT_NE(source.find("result.BoundaryDiagnostics"), std::string::npos);
    EXPECT_NE(source.find(
                  "ApplyEditorCurvatureSegmentationConfig"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "ApplyEditorConfiguredCurvatureSegmentationCommand"),
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
                  "config.RegionColors.Name"),
              std::string_view::npos);
    EXPECT_NE(visualization.find(
                  "EditorVisualizationTarget::Edges"),
              std::string_view::npos);
    EXPECT_NE(visualization.find(
                  "EditorVisualizationPropertyDomain::MeshEdges"),
              std::string_view::npos);
    EXPECT_NE(visualization.find(
                  "config.FeatureColors.Name"),
              std::string_view::npos);


    EXPECT_NE(source.find("DrawProcessingPropertyShowButton"), std::string::npos);
    const auto curvatureBegin = source.find("void MeshProcessingPanels::Impl::DrawCurvatureControls(");
    const auto segmentationBegin = source.find("void MeshProcessingPanels::Impl::DrawCurvatureSegmentationControls(");
    ASSERT_NE(curvatureBegin, std::string::npos);
    ASSERT_NE(segmentationBegin, std::string::npos);
    const auto curvature = source.substr(curvatureBegin, segmentationBegin - curvatureBegin);
    EXPECT_EQ(curvature.find("DrawCurvatureSegmentationControls(model"), std::string::npos);
    EXPECT_NE(curvature.find("DrawProcessingScalarOutput(\"Mean curvature\", config.Mean)"), std::string::npos);
    EXPECT_NE(curvature.find("config.Direction2.Name"), std::string::npos);

}
