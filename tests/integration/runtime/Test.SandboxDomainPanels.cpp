// ARCH-006 Slice 4 app/runtime composition coverage.
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Sandbox.Editor.DomainPanels;
import Extrinsic.Sandbox.Editor.Shell;

namespace Core = Extrinsic::Core;
namespace Runtime = Extrinsic::Runtime;
namespace SandboxEditor = Extrinsic::Sandbox::Editor;

namespace
{
    class OneFrameApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            engine.RequestExit();
        }
        void OnShutdown(Runtime::Engine&) override {}
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

TEST(SandboxDomainPanels, RegistersTheTenAppOwnedWindowsWithStableMenuMetadata)
{
    struct ExpectedWindow
    {
        std::string_view Id;
        std::vector<std::string> MenuPath;
        std::string_view Title;
    };
    const std::array<ExpectedWindow, 10> expected{{
        {"pointcloud.appearance", {"PointCloud"}, "Appearance"},
        {"pointcloud.properties", {"PointCloud"}, "Properties"},
        {"pointcloud.selection", {"PointCloud"}, "Selection details"},
        {"pointcloud.processing.remove_outliers",
         {"PointCloud", "Processing"},
         "Remove Outliers"},
        {"graph.appearance", {"Graph"}, "Appearance"},
        {"graph.properties", {"Graph"}, "Properties"},
        {"graph.selection", {"Graph"}, "Selection details"},
        {"mesh.appearance", {"Mesh"}, "Appearance"},
        {"mesh.properties", {"Mesh"}, "Properties"},
        {"mesh.selection", {"Mesh"}, "Selection details"},
    }};

    SandboxEditor::EditorShell editorUi;
    SandboxEditor::DomainPanels panels;
    panels.Register(editorUi);

    const auto menu = editorUi.BuildEditorWindowMenuModel();
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
    SandboxEditor::EditorShell firstUi;
    SandboxEditor::EditorShell secondUi;

    {
        SandboxEditor::DomainPanels panels;
        panels.Register(firstUi);
        ASSERT_EQ(firstUi.BuildEditorWindowMenuModel().size(), 20u);

        panels.Register(firstUi);
        EXPECT_EQ(firstUi.BuildEditorWindowMenuModel().size(), 20u);

        panels.Register(secondUi);
        EXPECT_EQ(firstUi.BuildEditorWindowMenuModel().size(), 10u);
        EXPECT_EQ(secondUi.BuildEditorWindowMenuModel().size(), 20u);

        panels.Unregister();
        EXPECT_EQ(secondUi.BuildEditorWindowMenuModel().size(), 10u);

        panels.Register(secondUi);
        ASSERT_EQ(secondUi.BuildEditorWindowMenuModel().size(), 20u);
    }

    EXPECT_EQ(firstUi.BuildEditorWindowMenuModel().size(), 10u);
    EXPECT_EQ(secondUi.BuildEditorWindowMenuModel().size(), 10u);
}

TEST(SandboxDomainPanels, ClosedRegisteredWindowsBuildNoDomainModels)
{
    Runtime::Engine engine(
        HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    SandboxEditor::EditorShell editorUi;
    SandboxEditor::DomainPanels panels;
    panels.Register(editorUi);
    editorUi.Attach(engine);
    engine.Run();

    EXPECT_EQ(editorUi.GetLastFrame().ModelBuildStats.DomainWindowModelBuilds,
              0u);
    EXPECT_EQ(
        editorUi.GetLastFrame().ModelBuildStats.DomainWindowModelCacheHits,
        0u);

    editorUi.Detach();
    panels.Unregister();
    engine.Shutdown();
}

TEST(SandboxDomainPanels, OpenSameDomainWindowsShareOneModelBuildPerFrame)
{
    Runtime::Engine engine(
        HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    SandboxEditor::EditorShell editorUi;
    SandboxEditor::DomainPanels panels;
    panels.Register(editorUi);
    for (const std::string_view id :
         {"pointcloud.appearance",
          "pointcloud.properties",
          "pointcloud.selection",
          "pointcloud.processing.remove_outliers"})
    {
        ASSERT_TRUE(editorUi.SetEditorWindowOpen(id, true)) << id;
    }

    editorUi.Attach(engine);
    engine.Run();

    EXPECT_EQ(editorUi.GetLastFrame().ModelBuildStats.DomainWindowModelBuilds,
              1u);
    EXPECT_EQ(
        editorUi.GetLastFrame().ModelBuildStats.DomainWindowModelCacheHits,
        3u);

    editorUi.Detach();
    panels.Unregister();
    engine.Shutdown();
}
