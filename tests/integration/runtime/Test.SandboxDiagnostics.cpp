#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Logging;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.DeviceBootstrap;
import Extrinsic.Runtime.DiagnosticsStream;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Sandbox.Editor.Shell;

#include "TestImGuiFrameScope.hpp"
#include "../../../src/app/Sandbox/Editor/Sandbox.DiagnosticsPanel.hpp"

namespace R = Extrinsic::Runtime;
namespace Editor = Extrinsic::Sandbox::Editor;
namespace Log = Extrinsic::Core::Log;

TEST(SandboxDiagnostics, WindowHasStableViewRegistration)
{
    R::WorldRegistry worlds;
    R::ServiceRegistry services;
    R::EditorUiHost host;
    auto owner = host.ClaimOwnerControl();
    owner.SetOperational(true);
    ASSERT_TRUE(services.Provide<R::EditorUiHost>(host, "test").has_value());
    Editor::EditorShell shell;
    shell.Attach(worlds, services);
    const auto menu = shell.BuildEditorWindowMenuModel();
    const auto found = std::ranges::find_if(menu, [](const auto& row) { return row.Id == "view.diagnostics"; });
    ASSERT_NE(found, menu.end());
    EXPECT_EQ(found->Title, "Diagnostics / Log");
    EXPECT_EQ(found->MenuPath, (std::vector<std::string>{"View"}));
    EXPECT_FALSE(found->Open);
}

TEST(SandboxDiagnostics, FiltersClearAndDeviceHeaderUseTheRuntimeStream)
{
    TestSupport::ImGuiFrameScope frame;
    Extrinsic::Core::Config::RenderConfig config{};
    const auto device = R::CreateRuntimeDevice(config);
    R::EditorDiagnosticsStream stream;
    stream.AttachDevice(config, *device);
    (void)stream.AppendOperation({.Name = "processing", .Status = R::DiagnosticOperationStatus::Succeeded,
                                 .RequestedBackend = "gpu_compute", .ActualBackend = "cpu"});
    Log::ClearEntries();
    Log::Info("[Geometry] visible");
    Log::Warn("[Other] hidden");
    Log::Error("[Geometry] failure");
    Editor::DiagnosticsPanelState state;
    std::strcpy(state.Category.data(), "Geometry");
    auto draw = [&] {
        frame.NextFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1000, 720});
        ImGui::Begin("Diagnostics test");
        ImGui::LogToBuffer();
        Editor::DrawDiagnosticsPanel(&stream, state);
        const std::string drawn{GImGui->LogBuffer.c_str()};
        ImGui::LogFinish();
        ImGui::End();
        return drawn;
    };
    const auto drawn = draw();
    ASSERT_EQ(state.Entries.size(), 2u);
    EXPECT_EQ(state.Device.ActualBackend, "null");
    EXPECT_FALSE(state.Device.IsOperational);
    EXPECT_NE(drawn.find("requested vulkan | actual null | not operational"), std::string::npos);
    EXPECT_NE(drawn.find("Validation: disabled | errors: 0"), std::string::npos);
    EXPECT_NE(drawn.find("processing"), std::string::npos);
    auto* window = ImGui::FindWindowByName("Diagnostics test");
    ASSERT_NE(window, nullptr);
    ImGui::ActivateItemByID(window->GetID("Info"));
    (void)draw();
    EXPECT_FALSE(state.Levels[0]);
    ASSERT_EQ(state.Entries.size(), 1u);
    EXPECT_EQ(state.Entries.front().Level, R::DiagnosticLevel::Error);
    ImGui::ActivateItemByID(window->GetID("Auto-scroll"));
    (void)draw();
    EXPECT_FALSE(state.AutoScroll);
    const auto beforeClear = state.Cursor;
    ImGui::ActivateItemByID(window->GetID("Clear"));
    (void)draw();
    EXPECT_TRUE(state.Entries.empty());
    EXPECT_EQ(Log::GetEntryCount(), 0u);
    EXPECT_GE(state.Cursor, beforeClear);
    EXPECT_EQ(state.Dropped, 0u);
    ImGui::ActivateItemByID(window->GetID("Info"));
    const auto afterFilterChange = draw();
    EXPECT_TRUE(state.Levels[0]);
    EXPECT_TRUE(state.Entries.empty());
    EXPECT_EQ(state.Dropped, 0u) << "Filter changes must not recount an intentional Clear as lost logs";
    EXPECT_EQ(afterFilterChange.find("log entries no longer retained"), std::string::npos);
    Log::Error("[Geometry] after clear");
    (void)draw();
    ASSERT_EQ(state.Entries.size(), 1u);
    EXPECT_EQ(state.Entries.front().Message, "[Geometry] after clear");
    EXPECT_GT(state.Cursor, beforeClear);
    R::ClearEditorDiagnosticsLog();
    (void)draw();
    EXPECT_TRUE(state.Entries.empty()) << "Clears from other readers invalidate the window cache";
}

TEST(SandboxDiagnostics, AutoScrollFollowsNewEntriesAndCanStayPaused)
{
    TestSupport::ImGuiFrameScope frame;
    Log::ClearEntries();
    for (int i = 0; i < 80; ++i) Log::Info("[Scroll] row {}", i);
    Editor::DiagnosticsPanelState state;
    ImGuiWindow* logWindow = nullptr;
    auto draw = [&] {
        frame.NextFrame();
        ImGui::SetNextWindowSize({900, 700});
        ImGui::Begin("Diagnostics scroll");
        Editor::DrawDiagnosticsPanel(nullptr, state);
        auto* window = ImGui::GetCurrentWindow();
        if (!window->DC.ChildWindows.empty()) logWindow = window->DC.ChildWindows.front();
        ImGui::End();
    };
    draw();
    draw();
    ASSERT_NE(logWindow, nullptr);
    EXPECT_GT(logWindow->ScrollMax.y, 0);
    EXPECT_NEAR(logWindow->Scroll.y, logWindow->ScrollMax.y, 1.0f);
    state.AutoScroll = false;
    ImGui::SetScrollY(logWindow, 0.0f);
    draw();
    Log::Info("[Scroll] pause preserves viewport");
    draw();
    draw();
    EXPECT_NEAR(logWindow->Scroll.y, 0.0f, 1.0f);
}

TEST(SandboxDiagnostics, AheadCursorDropsCachedRowsAndPriorClearBoundary)
{
    TestSupport::ImGuiFrameScope frame;
    Log::ClearEntries();
    Log::Info("[Restart] current");
    Editor::DiagnosticsPanelState state;
    state.Cursor = Log::GetSequenceNumber() + 1000;
    state.ClearCursor = state.Cursor;
    state.Entries.push_back({.Sequence = state.Cursor, .Message = "previous process"});
    frame.NextFrame();
    ImGui::SetNextWindowSize({900, 700});
    ImGui::Begin("Diagnostics restart");
    Editor::DrawDiagnosticsPanel(nullptr, state);
    ImGui::End();
    ASSERT_EQ(state.Entries.size(), 1u);
    EXPECT_EQ(state.Entries.front().Message, "[Restart] current");
    EXPECT_EQ(state.ClearCursor, 0u);
    EXPECT_EQ(state.Cursor, Log::GetSequenceNumber());
}
