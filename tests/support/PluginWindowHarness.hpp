// ARCH-022: the check every plugin window owes (Q89/Q90): while the window is open, settings applied elsewhere (as
// config_apply does) replace its draft, so its own "Apply settings" applies them and not the draft it started with.
#pragma once

#include <optional>
#include <string>

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.PluginSections;

namespace Intrinsic::Tests::Plugins
{
    // Opens `windowId`, applies `first`, applies `elsewhere` on frame 4 and presses the window's "Apply settings" on
    // frame 8; returns the active section afterwards (equal to `elsewhere` when the window followed). The window's
    // Apply must have run and found nothing to change.
    template <const auto& Section, class H>
    std::optional<Extrinsic::Runtime::DerivedSectionConfig<Section>> ApplyElsewhereThenPressApply(
        H& harness, const char* windowId, const char* windowTitle,
        const Extrinsic::Runtime::DerivedSectionConfig<Section>& first,
        const Extrinsic::Runtime::DerivedSectionConfig<Section>& elsewhere)
    {
        namespace R = Extrinsic::Runtime;
        EXPECT_TRUE(harness.Apply(first));
        auto* ui = harness.Engine.Services().template Find<R::EditorUiHost>();
        if (ui == nullptr || !ui->SetWindowOpen(windowId, true))
        {
            ADD_FAILURE() << "the window " << windowId << " cannot be opened";
            return std::nullopt;
        }
        IMGUI_CHECKVERSION();
        ImGuiContext* context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280, 1600);
        io.IniFilename = nullptr;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        for (int frame = 0; frame < 14; ++frame)
        {
            io.DeltaTime = 1.f / 60.f;
            ImGui::NewFrame();
            if (frame == 4) EXPECT_TRUE(harness.Apply(elsewhere));
            if (frame == 8)
            {
                if (auto* window = ImGui::FindWindowByName(windowTitle))
                    ImGui::ActivateItemByID(window->GetID("Apply settings"));
                else
                    ADD_FAILURE() << "the window " << windowTitle << " was not drawn";
            }
            (void)ui->Windows().DrawOpenWindows();
            ImGui::Render();
            harness.Frame();
        }
        ImGui::DestroyContext(context);
        // Frame 4's apply changed the section; only the window's Apply of a followed draft leaves "no change" last.
        const auto* control = harness.Engine.Services().template Find<R::EngineConfigControl>();
        EXPECT_TRUE(control != nullptr &&
                    control->GetEngineConfigControlState().LastApply.Status == R::RuntimeEngineConfigApplyStatus::NoChange)
            << "the window's Apply did not run, or applied other settings";
        return R::ActivePluginSection<Section>();
    }
}
