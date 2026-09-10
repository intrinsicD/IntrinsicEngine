#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.DescriptorAnalysisConfig;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Sandbox.Editor.MeshProcessingPanels;
import Extrinsic.Sandbox.Editor.Shell;
import Geometry.HalfedgeMesh;

namespace Runtime = Extrinsic::Runtime;
namespace Editor = Extrinsic::Sandbox::Editor;
namespace Config = Extrinsic::Core::Config;
namespace G = Extrinsic::Graphics::Components;
namespace GS = Extrinsic::ECS::Components::GeometrySources;

namespace
{
    class PanelDriver final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        std::function<void(Runtime::Engine&)> OnFrame{};
        void Frame(double, double) override { OnFrame(Kernel()); }
    };
}

TEST(SandboxDescriptorAnalysisPanel, SliderFollowsHistogramOnlyAfterShow)
{
    Config::EngineConfigSectionRegistry sections;
    ASSERT_TRUE(sections.Register(Runtime::MakeDescriptorAnalysisConfigSectionRegistration()));
    Config::EngineConfig config{};
    Config::PopulateEngineConfigSectionDefaults(config, sections);
    config.Simulation.WorkerThreadCount = 1u;
    config.ReferenceScene.Enabled = false;
    config.Camera.Enabled = false;
    config.Window.Backend = Config::WindowBackend::Null;
    auto application = std::make_unique<PanelDriver>();
    auto* driver = application.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(application));
    engine.EmplaceModule<Runtime::EngineConfigControl>(std::move(sections));
    engine.EmplaceModule<Runtime::EditorUiModule>();
    engine.Initialize();
    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const auto entity = scene.Create();
    const auto secondEntity = scene.Create();
    Geometry::HalfedgeMesh::Mesh mesh;
    const auto a = mesh.AddVertex({0, 0, 0});
    const auto b = mesh.AddVertex({1, 0, 0});
    const auto c = mesh.AddVertex({0, 1, 0});
    ASSERT_TRUE(mesh.AddTriangle(a, b, c));
    Runtime::DescriptorAnalysisConfig analysis{};
    analysis.StableEntityId = Runtime::SelectionController::ToStableEntityId(entity);
    analysis.Positions.Domain = Runtime::GeometryElementDomain::MeshVertex;
    analysis.Normals.Domain = analysis.Positions.Domain;
    analysis.Outputs = Runtime::MakeDescriptorOutputProperties(analysis.Positions.Domain);
    for (const auto target : {entity, secondEntity})
    {
        GS::PopulateFromMesh(scene.Raw(), target, mesh);
        scene.Raw().emplace<G::RenderSurface>(target);
        auto& properties = scene.Raw().get<GS::Vertices>(target).Properties;
        (void)properties.GetOrAdd<glm::vec3>("v:normal", {0, 0, 1});
        for (const auto& output : analysis.Outputs)
            if (output.Name != analysis.Outputs[0].Name)
                (void)properties.GetOrAdd<float>(output.Name, 1.0f);
    }
    const auto& properties = scene.Raw().get<GS::Vertices>(entity).Properties;
    const auto propertyRevision = properties.Revision();
    auto* control = engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(control, nullptr);
    auto candidate = control->GetEngineConfigControlState().ActiveConfig;
    Runtime::SetDescriptorAnalysisConfig(candidate, analysis);
    ASSERT_TRUE(control->ApplyEngineConfigHotSubset(
        control->PreviewEngineConfigControlDocument(Config::SerializeEngineConfig(candidate))).Succeeded());

    Editor::EditorShell shell;
    shell.Attach(engine.Worlds(), engine.Services());
    Editor::MeshProcessingPanels panels;
    panels.Register(shell);
    ASSERT_TRUE(shell.SetEditorWindowOpen("view.descriptor_analysis", true));
    int frame = 0;
    driver->OnFrame = [&](Runtime::Engine& kernel) {
        ++frame;
        if (frame < 3)
            return;
        auto* window = ImGui::FindWindowByName("FPFH Descriptor Analysis");
        if (window == nullptr)
        {
            ADD_FAILURE() << "Descriptor panel did not open";
            kernel.RequestExit();
            return;
        }
        ImGui::SetWindowSize(window, {650, 850});
        ImGui::FocusWindow(window);
        const auto editBin = [&](int start, const char* value) {
            if (frame == start)
            {
                ImGui::ActivateItemByID(window->GetID("Display histogram bin"));
                ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
            }
            if (frame == start + 2)
                ImGui::GetIO().AddInputCharactersUTF8(value);
            if (frame == start + 4)
                ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
            if (frame == start + 5)
                ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
        };
        if (frame == 3)
            ImGui::ActivateItemByID(window->GetID("Show histogram bin"));
        editBin(7, "7");
        editBin(22, "19");
        editBin(33, "999");
        editBin(47, "7");
        if (frame == 15)
            EXPECT_FALSE(scene.Raw().all_of<G::VisualizationLaneOverrides>(entity));
        if (frame == 16)
            ImGui::ActivateItemByID(window->GetID("Show histogram bin"));
        if (frame == 20 || frame == 31 || frame == 42)
        {
            const auto* overrides = scene.Raw().try_get<G::VisualizationLaneOverrides>(entity);
            EXPECT_TRUE(overrides && overrides->Surface);
            if (overrides && overrides->Surface)
            {
                const int bin = frame == 20 ? 7 : frame == 31 ? 19 : 32;
                EXPECT_EQ(overrides->Surface->Source, G::VisualizationConfig::ColorSource::ScalarField);
                EXPECT_EQ(overrides->Surface->ScalarFieldName, analysis.Outputs[bin].Name);
            }
        }
        if (frame == 44)
        {
            analysis.StableEntityId = Runtime::SelectionController::ToStableEntityId(secondEntity);
            Runtime::SetDescriptorAnalysisConfig(candidate, analysis);
            EXPECT_TRUE(control->ApplyEngineConfigHotSubset(
                control->PreviewEngineConfigControlDocument(Config::SerializeEngineConfig(candidate))).Succeeded());
        }
        if (frame == 57)
        {
            EXPECT_FALSE(scene.Raw().all_of<G::VisualizationLaneOverrides>(secondEntity));
            kernel.RequestExit();
        }
    };
    engine.Run();
    EXPECT_EQ(frame, 57);
    EXPECT_EQ(properties.Revision(), propertyRevision);
    panels.Unregister();
    shell.Detach();
    engine.Shutdown();
}
