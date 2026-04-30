#pragma once

#include <imgui.h>

namespace TestSupport
{
    struct ImGuiFrameScope
    {
        ImGuiFrameScope()
        {
            IMGUI_CHECKVERSION();
            Context = ImGui::CreateContext();
            ImGui::SetCurrentContext(Context);
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.DisplaySize = ImVec2(800.0f, 600.0f);

            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            (void)pixels;
            (void)width;
            (void)height;

            ImGui::NewFrame();
        }

        ~ImGuiFrameScope()
        {
            ImGui::EndFrame();
            ImGui::DestroyContext(Context);
        }

        void NextFrame()
        {
            ImGui::EndFrame();
            ImGui::NewFrame();
        }

        ImGuiContext* Context = nullptr;
    };
}

