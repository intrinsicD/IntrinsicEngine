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
import Extrinsic.Runtime.EditorUiModule;
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

    struct EditorUiShellHarness
    {
        Runtime::Engine Kernel{
            HeadlessConfig(),
            std::make_unique<OneFrameApplication>()};
        SandboxEditor::EditorShell Shell{};

        EditorUiShellHarness()
        {
            Kernel.EmplaceModule<Runtime::EditorUiModule>();
            Kernel.Initialize();
            Shell.Attach(Kernel);
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

    EditorUiShellHarness harness;
    SandboxEditor::DomainPanels panels;
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
        SandboxEditor::DomainPanels panels;
        panels.Register(first.Shell);
        ASSERT_EQ(first.Shell.BuildEditorWindowMenuModel().size(), 20u);

        panels.Register(first.Shell);
        EXPECT_EQ(first.Shell.BuildEditorWindowMenuModel().size(), 20u);

        panels.Register(second.Shell);
        EXPECT_EQ(first.Shell.BuildEditorWindowMenuModel().size(), 10u);
        EXPECT_EQ(second.Shell.BuildEditorWindowMenuModel().size(), 20u);

        panels.Unregister();
        EXPECT_EQ(second.Shell.BuildEditorWindowMenuModel().size(), 10u);

        panels.Register(second.Shell);
        ASSERT_EQ(second.Shell.BuildEditorWindowMenuModel().size(), 20u);
    }

    EXPECT_EQ(first.Shell.BuildEditorWindowMenuModel().size(), 10u);
    EXPECT_EQ(second.Shell.BuildEditorWindowMenuModel().size(), 10u);
}

TEST(SandboxDomainPanels, ClosedRegisteredWindowsBuildNoDomainModels)
{
    EditorUiShellHarness harness;
    SandboxEditor::DomainPanels panels;
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
    SandboxEditor::DomainPanels panels;
    panels.Register(harness.Shell);
    for (const std::string_view id :
         {"pointcloud.appearance",
          "pointcloud.properties",
          "pointcloud.selection",
          "pointcloud.processing.remove_outliers"})
    {
        ASSERT_TRUE(harness.Shell.SetEditorWindowOpen(id, true)) << id;
    }

    harness.Kernel.Run();

    EXPECT_EQ(
        harness.Shell.GetLastFrame().ModelBuildStats.DomainWindowModelBuilds,
        1u);
    EXPECT_EQ(
        harness.Shell.GetLastFrame().ModelBuildStats.DomainWindowModelCacheHits,
        3u);
}
